// src/io/AtomicFileWin.cpp
#include <windows.h>
#include <filesystem>
#include <fstream>
#include "io/AtomicFile.h"

namespace cg::io {
using fs = std::filesystem;

static bool replace_atomic(const fs::path& final_path, const fs::path& tmp_path, bool make_backup) {
  const fs::path bak = final_path.wstring() + L".bak";
  if (make_backup) {
    if (ReplaceFileW(final_path.c_str(), tmp_path.c_str(), bak.c_str(),
                     REPLACEFILE_IGNORE_MERGE_ERRORS | REPLACEFILE_IGNORE_ACL_ERRORS,
                     nullptr, nullptr)) return true;
  }
  if (MoveFileExW(tmp_path.c_str(), final_path.c_str(),
                  MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    return true;
  DeleteFileW(tmp_path.c_str());
  return false;
}

bool write_atomic(const fs::path& final_path, const std::string& bytes, std::string* err, bool make_backup) {
  std::error_code ec;
  fs::create_directories(final_path.parent_path(), ec);
  wchar_t tmpName[MAX_PATH];
  swprintf(tmpName, MAX_PATH, L".%s.tmp.%u_%llu",
           final_path.filename().c_str(), GetCurrentProcessId(),
           static_cast<unsigned long long>(GetTickCount64()));
  const fs::path tmp = final_path.parent_path() / tmpName;

  HANDLE h = CreateFileW(tmp.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
                         FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_WRITE_THROUGH, nullptr);
  if (h == INVALID_HANDLE_VALUE) { if (err) *err = "CreateFileW(tmp) failed"; return false; }

  const BYTE* p = reinterpret_cast<const BYTE*>(bytes.data());
  size_t left = bytes.size(); BOOL ok = TRUE;
  while (left && ok) {
    const DWORD chunk = static_cast<DWORD>(std::min(left, size_t(1u<<20)));
    DWORD w = 0; ok = WriteFile(h, p, chunk, &w, nullptr); if (!ok || w != chunk) break;
    p += chunk; left -= chunk;
  }
  if (ok) ok = FlushFileBuffers(h);
  CloseHandle(h);
  if (!ok) { DeleteFileW(tmp.c_str()); if (err) *err = "Write/flush failed"; return false; }

  if (!replace_atomic(final_path, tmp, make_backup)) {
    if (err) *err = "ReplaceFileW/MoveFileExW failed";
    return false;
  }
  return true;
}

bool read_all(const fs::path& path, std::string& out, std::string* err) {
  std::ifstream in(path, std::ios::binary);
  if (!in) { if (err) *err = "open failed"; return false; }
  out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
  return true;
}
} // namespace cg::io
