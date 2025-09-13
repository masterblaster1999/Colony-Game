// ============================================================================
// SingleInstance.h  —  Windows-only single-instance helper with activation IPC
// Version: 1.2.0
// Requires: C++17+, UNICODE build recommended
// License: Public domain / Unlicense (adjust to your repo's needs)
//
// What this provides
// ------------------
// 1) Robust single-instance guard using a named mutex.
// 2) Scope control: Per-session (Local\), Per-user (Local\ + SID), or Global\.
// 3) Optional activation channel: secondary instance can send a short payload
//    (e.g., its command line) to the primary and exit. The primary runs an
//    activation listener that receives that payload and can, for example,
//    bring its window to the foreground.
// 4) Foreground handoff: secondary calls AllowSetForegroundWindow(primaryPid)
//    so the primary can legitimately steal focus when handling activation.
//
// Typical usage (Launcher)
// ------------------------
//   #include "SingleInstance.h"
//   using colony::win::SingleInstance;
//   int APIENTRY wWinMain(...) {
//       SingleInstance inst(L"ColonyGame-{A3183C74-6DDF-4C68-BF2C-BA5D2E29C1E2}",
//                           SingleInstance::Scope::PerSessionLocal);
//       auto res = inst.acquire();
//       if (res == SingleInstance::AcquireResult::SecondaryExists) {
//           // Optional: forward our command line to the primary
//           inst.notify_primary(GetCommandLineW());
//           return 0;
//       } else if (res == SingleInstance::AcquireResult::Error) {
//           MessageBoxW(nullptr, inst.error_message().c_str(), L"Colony-Game", MB_OK | MB_ICONERROR);
//           // Fall through or exit depending on your policy.
//       }
//
//       // We are the primary instance. Start listening for activations.
//       inst.start_activation_listener([&](const std::wstring& payload) {
//           // Bring our window to front (replace with your game window handle or PID).
//           SingleInstance::bring_process_to_foreground(GetCurrentProcessId());
//           // Optionally parse 'payload' (e.g., file paths, URLs, args).
//       });
//
//       // ... continue normal launcher/game init ...
//   }
//
// Notes
// -----
// - The "payload" buffer is limited (default 4096 wchar_t). Keep it small.
// - The activation queue is *not* lossless if multiple secondaries fire at
//   exactly the same time; last write wins for a single event tick. In real
//   use (occasional double-clicks), this is perfectly fine.
// - Scope::Global may require SeCreateGlobalPrivilege in services/TS.
// ============================================================================

#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#  define NOMINMAX
#endif

#include <windows.h>
#include <sddl.h>       // ConvertSidToStringSidW
#include <tlhelp32.h>
#include <string>
#include <functional>
#include <thread>
#include <atomic>
#include <vector>
#include <cstdint>
#include <memory>

#pragma comment(lib, "Advapi32.lib")
#pragma comment(lib, "User32.lib")

namespace colony {
namespace win {

class SingleInstance {
public:
    // Where to register the named kernel objects.
    enum class Scope {
        PerSessionLocal,  // "Local\..." (default)
        PerUser,          // "Local\..._{SID}"
        Global            // "Global\..."  (may require privilege in TS)
    };

    enum class AcquireResult {
        PrimaryAcquired,  // This process is now the primary
        SecondaryExists,  // Another process is primary; we are a secondary
        Error             // Failed to create/open the mutex
    };

    using ActivateCallback = std::function<void(const std::wstring& payload)>;

    // Configuration knobs for the activation shared memory.
    struct Config {
        // Max wchar_t count for payload (including null terminator).
        // Must be >= 2. Default 4096 chars (~8 KB).
        uint32_t maxPayloadWchars = 4096;

        // If true, we create a named "Ready" event and set it signaled in primary.
        // Secondaries can call wait_for_primary_ready() before notifying.
        bool createReadyEvent = true;

        // Named-object base prefix. Change only if you need stricter name hygiene.
        std::wstring namePrefix = L"ColonyInstance";
    };

    // Construct with your stable AppID (recommended: reverse-DNS or GUID).
    // Example ID: L"ColonyGame-{A3183C74-6DDF-4C68-BF2C-BA5D2E29C1E2}"
    explicit SingleInstance(std::wstring appId,
                            Scope scope = Scope::PerSessionLocal,
                            Config cfg = {})
        : _appId(std::move(appId)),
          _scope(scope),
          _cfg(std::move(cfg)),
          _isPrimary(false),
          _ownerPid(0),
          _lastErr(0),
          _hMutex(nullptr),
          _hMap(nullptr),
          _pShared(nullptr),
          _hEvtActivate(nullptr),
          _hEvtReady(nullptr),
          _hEvtStop(nullptr),
          _listenerRunning(false)
    {
        sanitize_id();
        build_object_names();
    }

    // Non-copyable, movable.
    SingleInstance(const SingleInstance&) = delete;
    SingleInstance& operator=(const SingleInstance&) = delete;

    SingleInstance(SingleInstance&& other) noexcept { move_from(std::move(other)); }
    SingleInstance& operator=(SingleInstance&& other) noexcept {
        if (this != &other) {
            cleanup();
            move_from(std::move(other));
        }
        return *this;
    }

    ~SingleInstance() {
        stop_activation_listener();
        cleanup();
    }

    // Try to become the primary instance. Creates the named mutex.
    AcquireResult acquire() {
        cleanup_errors();

        _hMutex = CreateMutexW(nullptr, TRUE, _mutexName.c_str());
        if (!_hMutex) {
            record_last_error(GetLastError(), L"CreateMutexW failed");
            return AcquireResult::Error;
        }

        if (GetLastError() == ERROR_ALREADY_EXISTS) {
            // Someone else is primary; we are a secondary.
            _isPrimary = false;
            open_secondary_side_objects(); // try to discover primary PID
            return AcquireResult::SecondaryExists;
        }

        // We created the mutex and own it: we are the primary.
        _isPrimary = true;
        _ownerPid = GetCurrentProcessId();

        if (!create_primary_side_objects()) {
            // We are still primary (mutex held), but activation channel failed.
            // Up to caller whether to proceed. Report as PrimaryAcquired but with error text.
        }
        return AcquireResult::PrimaryAcquired;
    }

    // Are we the primary holder?
    bool is_primary() const noexcept { return _isPrimary; }

    // If known, the primary owner's PID (for primary it's our PID, for secondary we try to read it from shared memory).
    DWORD owner_pid() const noexcept { return _ownerPid; }

    // Returns last Win32 error code and a human-readable message (set when acquire/open/notify failed).
    DWORD error_code()    const noexcept { return _lastErr; }
    const std::wstring& error_message() const noexcept { return _lastErrMsg; }

    // -------------------------- Secondary helpers --------------------------

    // Optional: wait until primary signals its "ready" event (if enabled in Config).
    // Returns true if signaled, false on timeout or failure.
    bool wait_for_primary_ready(DWORD timeoutMs = 10000) {
        if (_isPrimary) return true;
        if (_cfg.createReadyEvent && !_hEvtReady) {
            // Attempt to open if not open yet.
            open_named_handle(_hEvtReady, EVENT_MODIFY_STATE | SYNCHRONIZE, _readyName.c_str());
        }
        if (!_hEvtReady) return false;
        DWORD w = WaitForSingleObject(_hEvtReady, timeoutMs);
        return w == WAIT_OBJECT_0;
    }

    // Notify the primary instance (if any) and optionally pass a payload (e.g., our command line).
    // Returns true if the event was signaled successfully.
    bool notify_primary(const std::wstring& payload = L"", DWORD /*unused*/ = 0) {
        if (_isPrimary) return true; // nothing to notify

        if (!_hEvtActivate || !_pShared) {
            // Try opening lazily (e.g., if acquire() was constructed elsewhere)
            open_secondary_side_objects();
        }
        if (!_hEvtActivate || !_pShared) {
            record_last_error(ERROR_NOT_FOUND, L"Primary activation channel not available");
            return false;
        }

        // Foreground handoff: allow the primary process to set foreground
        if (_ownerPid) {
            AllowSetForegroundWindow(_ownerPid);
        }

        // Write payload (truncate if needed)
        const uint32_t maxW = _pShared->maxPayloadWchars;
        if (maxW >= 2) {
            uint32_t count = static_cast<uint32_t>(payload.size());
            if (count >= maxW) count = maxW - 1;
            wmemcpy(_pShared->payload, payload.data(), count);
            _pShared->payload[count] = L'\0';
            _pShared->payloadLen = count;
            InterlockedIncrement(&_pShared->seq);
        }

        if (!SetEvent(_hEvtActivate)) {
            record_last_error(GetLastError(), L"SetEvent(Activate) failed");
            return false;
        }
        return true;
    }

    // --------------------------- Primary helpers ---------------------------

    // Start a background listener. The callback runs on an internal thread
    // whenever a secondary notifies via notify_primary().
    // Returns true if the listener started.
    bool start_activation_listener(ActivateCallback cb) {
        if (!_isPrimary) return false;
        if (!cb) return false;
        if (!_hEvtActivate || !_pShared) return false;
        if (_listenerRunning.load(std::memory_order_acquire)) return true;

        if (!_hEvtStop) {
            _hEvtStop = CreateEventW(nullptr, TRUE, FALSE, nullptr); // manual reset
        }
        if (!_hEvtStop) {
            record_last_error(GetLastError(), L"CreateEvent(Stop) failed");
            return false;
        }

        _listenerRunning.store(true, std::memory_order_release);
        _callback = std::move(cb);
        _listener = std::thread([this] { this->listener_thread_proc(); });
        return true;
    }

    // Stop the listener (safe to call multiple times).
    void stop_activation_listener() {
        if (_listenerRunning.exchange(false, std::memory_order_acq_rel)) {
            if (_hEvtStop) SetEvent(_hEvtStop);
            if (_listener.joinable()) _listener.join();
            if (_hEvtStop) { CloseHandle(_hEvtStop); _hEvtStop = nullptr; }
            _callback = nullptr;
        }
    }

    // ------------------------------ Utilities ------------------------------

    // Bring a process's (PID) main/top-level window to the foreground.
    // Returns true on likely success.
    static bool bring_process_to_foreground(DWORD pid) {
        HWND hwnd = find_top_level_window_for_pid(pid);
        if (!hwnd) return false;
        return bring_window_to_foreground(hwnd);
    }

    // Bring a specific window to foreground (restore if minimized).
    static bool bring_window_to_foreground(HWND hwnd) {
        if (!IsWindow(hwnd)) return false;
        DWORD curThread = GetCurrentThreadId();
        DWORD wndThread = GetWindowThreadProcessId(hwnd, nullptr);
        // Some systems require thread input attachment to reliably set foreground.
        bool attached = false;
        if (curThread != wndThread) {
            attached = AttachThreadInput(curThread, wndThread, TRUE);
        }
        if (IsIconic(hwnd)) ShowWindow(hwnd, SW_RESTORE);
        SetForegroundWindow(hwnd);
        BringWindowToTop(hwnd);
        if (attached) AttachThreadInput(curThread, wndThread, FALSE);
        return true;
    }

private:
    // ---------------------------- Shared layout ----------------------------
    struct SharedData {
        DWORD    primaryPid = 0;
        LONG     seq = 0;                  // incremented by secondaries
        uint32_t maxPayloadWchars = 0;     // capacity including null
        uint32_t payloadLen = 0;           // current length excluding null
        wchar_t  payload[1];               // flexible array; real size decided by mapping
    };

    // ------------------------------ Members --------------------------------
    std::wstring _appId;
    Scope        _scope;
    Config       _cfg;

    bool         _isPrimary;
    DWORD        _ownerPid;

    DWORD        _lastErr;
    std::wstring _lastErrMsg;

    std::wstring _mutexName;
    std::wstring _mapName;
    std::wstring _activateName;
    std::wstring _readyName;

    HANDLE       _hMutex;
    HANDLE       _hMap;
    SharedData*  _pShared;
    HANDLE       _hEvtActivate;
    HANDLE       _hEvtReady;
    HANDLE       _hEvtStop;

    std::thread              _listener;
    std::atomic<bool>        _listenerRunning;
    ActivateCallback         _callback;

    // ---------------------------- Core helpers -----------------------------

    void cleanup() noexcept {
        if (_listenerRunning.load(std::memory_order_acquire)) {
            stop_activation_listener();
        }
        if (_pShared) {
            UnmapViewOfFile(_pShared);
            _pShared = nullptr;
        }
        if (_hMap)         { CloseHandle(_hMap); _hMap = nullptr; }
        if (_hEvtActivate) { CloseHandle(_hEvtActivate); _hEvtActivate = nullptr; }
        if (_hEvtReady)    { CloseHandle(_hEvtReady); _hEvtReady = nullptr; }
        if (_hMutex)       { // Releasing the mutex handle effectively ends primary ownership.
            ReleaseMutex(_hMutex); // Safe even if we didn't create initial owner (no-op on non-owner).
            CloseHandle(_hMutex);
            _hMutex = nullptr;
        }
    }

    void move_from(SingleInstance&& o) noexcept {
        _appId          = std::move(o._appId);
        _scope          = o._scope;
        _cfg            = std::move(o._cfg);
        _isPrimary      = o._isPrimary;
        _ownerPid       = o._ownerPid;
        _lastErr        = o._lastErr;
        _lastErrMsg     = std::move(o._lastErrMsg);
        _mutexName      = std::move(o._mutexName);
        _mapName        = std::move(o._mapName);
        _activateName   = std::move(o._activateName);
        _readyName      = std::move(o._readyName);

        _hMutex         = o._hMutex;        o._hMutex = nullptr;
        _hMap           = o._hMap;          o._hMap = nullptr;
        _pShared        = o._pShared;       o._pShared = nullptr;
        _hEvtActivate   = o._hEvtActivate;  o._hEvtActivate = nullptr;
        _hEvtReady      = o._hEvtReady;     o._hEvtReady = nullptr;
        _hEvtStop       = o._hEvtStop;      o._hEvtStop = nullptr;

        _listenerRunning.store(o._listenerRunning.load(std::memory_order_acquire), std::memory_order_release);
        _callback       = std::move(o._callback);
        _listener       = std::move(o._listener);
    }

    void cleanup_errors() noexcept {
        _lastErr = 0;
        _lastErrMsg.clear();
    }

    void record_last_error(DWORD code, const wchar_t* context) {
        _lastErr = code;
        _lastErrMsg = format_win_error(code, context);
    }

    // ------------------------- Named object names --------------------------
    static std::wstring sanitize(const std::wstring& s) {
        std::wstring out; out.reserve(s.size());
        for (wchar_t ch : s) {
            if ((ch >= L'0' && ch <= L'9') ||
                (ch >= L'A' && ch <= L'Z') ||
                (ch >= L'a' && ch <= L'z') ||
                ch == L'_' || ch == L'-' || ch == L'.' || ch == L'{' || ch == L'}')
                out.push_back(ch);
            else
                out.push_back(L'_');
        }
        return out;
    }

    void sanitize_id() {
        _appId = sanitize(_appId);
        if (_appId.empty()) _appId = L"App";
    }

    static std::wstring get_current_user_sid() {
        std::wstring sidStr;
        HANDLE hTok = nullptr;
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hTok)) return sidStr;

        DWORD len = 0;
        GetTokenInformation(hTok, TokenUser, nullptr, 0, &len);
        if (GetLastError() != ERROR_INSUFFICIENT_BUFFER) { CloseHandle(hTok); return sidStr; }

        std::vector<BYTE> buf(len);
        if (!GetTokenInformation(hTok, TokenUser, buf.data(), len, &len)) { CloseHandle(hTok); return sidStr; }

        PTOKEN_USER tu = reinterpret_cast<PTOKEN_USER>(buf.data());
        LPWSTR tmp = nullptr;
        if (ConvertSidToStringSidW(tu->User.Sid, &tmp)) {
            sidStr.assign(tmp);
            LocalFree(tmp);
        }
        CloseHandle(hTok);
        return sidStr;
    }

    void build_object_names() {
        std::wstring scopePrefix = L"Local\\";
        if (_scope == Scope::Global) {
            scopePrefix = L"Global\\";
        }

        std::wstring base = _cfg.namePrefix + L"-" + _appId;
        if (_scope == Scope::PerUser) {
            auto sid = get_current_user_sid();
            if (!sid.empty()) base += L"-U{" + sid + L"}";
        }

        _mutexName    = scopePrefix + base + L".mtx";
        _mapName      = scopePrefix + base + L".map";
        _activateName = scopePrefix + base + L".evt";
        _readyName    = scopePrefix + base + L".rdy";
    }

    // ----------------------- Primary-side initialization -------------------
    bool create_primary_side_objects() {
        // Create shared memory (file mapping) sized for SharedData + payload buffer
        const uint32_t capW = (_cfg.maxPayloadWchars >= 2) ? _cfg.maxPayloadWchars : 4096u;
        const size_t   bytes = sizeof(SharedData) + (static_cast<size_t>(capW) - 1) * sizeof(wchar_t);

        _hMap = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
                                   0, static_cast<DWORD>(bytes), _mapName.c_str());
        if (!_hMap) {
            record_last_error(GetLastError(), L"CreateFileMappingW failed");
            return false;
        }

        _pShared = static_cast<SharedData*>(MapViewOfFile(_hMap, FILE_MAP_ALL_ACCESS, 0, 0, bytes));
        if (!_pShared) {
            record_last_error(GetLastError(), L"MapViewOfFile failed");
            return false;
        }

        // Initialize header
        _pShared->primaryPid        = GetCurrentProcessId();
        _pShared->seq               = 0;
        _pShared->maxPayloadWchars  = capW;
        _pShared->payloadLen        = 0;
        _pShared->payload[0]        = L'\0';
        _ownerPid                   = _pShared->primaryPid;

        // Activation event: auto-reset, initially non-signaled
        _hEvtActivate = CreateEventW(nullptr, FALSE, FALSE, _activateName.c_str());
        if (!_hEvtActivate) {
            record_last_error(GetLastError(), L"CreateEvent(Activate) failed");
            return false;
        }

        if (_cfg.createReadyEvent) {
            // Manual reset "ready" event; set signaled to indicate primary is up.
            _hEvtReady = CreateEventW(nullptr, TRUE, TRUE, _readyName.c_str());
            if (!_hEvtReady) {
                record_last_error(GetLastError(), L"CreateEvent(Ready) failed");
                return false;
            }
        }
        return true;
    }

    // ---------------------- Secondary-side open helpers --------------------
    void open_secondary_side_objects() {
        // Try to open in any order; it's okay if some fail on older versions.
        open_named_handle(_hEvtActivate, EVENT_MODIFY_STATE | SYNCHRONIZE, _activateName.c_str());
        open_map_for_secondary();
        if (_pShared) {
            _ownerPid = _pShared->primaryPid;
        } else {
            _ownerPid = 0;
        }
        if (_cfg.createReadyEvent) {
            open_named_handle(_hEvtReady, SYNCHRONIZE, _readyName.c_str());
        }
    }

    void open_map_for_secondary() {
        if (_pShared) return;
        HANDLE h = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, _mapName.c_str());
        if (!h) return;
        // We don't know the exact size; map the whole section.
        LPVOID p = MapViewOfFile(h, FILE_MAP_ALL_ACCESS, 0, 0, 0);
        if (!p) { CloseHandle(h); return; }
        _hMap = h;
        _pShared = static_cast<SharedData*>(p);
    }

    template<typename HandleT>
    void open_named_handle(HandleT& out, DWORD desired, const wchar_t* name) {
        if (out) return;
        HANDLE h = OpenEventW(desired, FALSE, name);
        if (!h) return;
        out = h;
    }

    // -------------------------- Listener thread ----------------------------
    void listener_thread_proc() {
        HANDLE waitHandles[2] = { _hEvtStop, _hEvtActivate };
        for (;;) {
            DWORD w = WaitForMultipleObjects(2, waitHandles, FALSE, INFINITE);
            if (w == WAIT_OBJECT_0) {
                // Stop requested
                break;
            }
            if (w == WAIT_OBJECT_0 + 1) {
                // Activation received
                std::wstring payload;
                if (_pShared) {
                    const uint32_t capW = _pShared->maxPayloadWchars ? _pShared->maxPayloadWchars : 1u;
                    const uint32_t lenW = (_pShared->payloadLen < capW) ? _pShared->payloadLen : (capW - 1);
                    payload.assign(_pShared->payload, _pShared->payload + lenW);
                }
                if (_callback) {
                    // Defensive: run user callback and swallow exceptions across DLL boundaries.
                    try { _callback(payload); }
                    catch (...) { /* ignore */ }
                }
            }
        }
    }

    // ------------------------------ Utilities ------------------------------

    static HWND find_top_level_window_for_pid(DWORD pid) {
        struct Ctx { DWORD pid; HWND found; };
        Ctx ctx{ pid, nullptr };
        EnumWindows([](HWND h, LPARAM p) -> BOOL {
            auto* c = reinterpret_cast<Ctx*>(p);
            DWORD wpid = 0;
            GetWindowThreadProcessId(h, &wpid);
            if (wpid != c->pid) return TRUE; // continue
            // Ignore tool/owner/minimized-only/non-visible windows:
            if (!IsWindowVisible(h)) return TRUE;
            LONG_PTR ex = GetWindowLongPtrW(h, GWL_EXSTYLE);
            if (ex & WS_EX_TOOLWINDOW) return TRUE;
            // Found a likely candidate
            c->found = h;
            return FALSE; // stop
        }, reinterpret_cast<LPARAM>(&ctx));
        return ctx.found;
    }

    static std::wstring format_win_error(DWORD code, const wchar_t* context) {
        LPWSTR buf = nullptr;
        DWORD len = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                                   nullptr, code, 0 /*LANG_USER_DEFAULT*/, (LPWSTR)&buf, 0, nullptr);
        std::wstring msg;
        if (context) {
            msg.append(L"[");
            msg.append(context);
            msg.append(L"] ");
        }
        if (len && buf) {
            msg.append(buf, len);
            LocalFree(buf);
        } else {
            msg.append(L"Win32 error ");
            msg.append(std::to_wstring(code));
        }
        return msg;
    }
};

} // namespace win
} // namespace colony
