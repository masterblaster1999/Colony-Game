#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace colony::game::util {

enum class NotifySeverity : std::uint8_t
{
    Info = 0,
    Warning,
    Error,
};

[[nodiscard]] inline const char* NotifySeverityName(NotifySeverity s) noexcept
{
    switch (s)
    {
    case NotifySeverity::Info: return "INFO";
    case NotifySeverity::Warning: return "WARN";
    case NotifySeverity::Error: return "ERROR";
    }
    return "?";
}

struct NotifyTarget
{
    enum class Kind : std::uint8_t
    {
        None = 0,
        Tile,
        WorldPos,
        Colonist,
    };

    Kind kind = Kind::None;

    // Tile focus
    int tileX = 0;
    int tileY = 0;

    // World focus (tile coordinates + fractional offset)
    float worldX = 0.0f;
    float worldY = 0.0f;

    // Colonist focus
    int colonistId = -1;

    [[nodiscard]] static NotifyTarget None() noexcept { return {}; }

    [[nodiscard]] static NotifyTarget Tile(int x, int y) noexcept
    {
        NotifyTarget t{};
        t.kind = Kind::Tile;
        t.tileX = x;
        t.tileY = y;
        return t;
    }

    [[nodiscard]] static NotifyTarget World(float x, float y) noexcept
    {
        NotifyTarget t{};
        t.kind = Kind::WorldPos;
        t.worldX = x;
        t.worldY = y;
        return t;
    }

    [[nodiscard]] static NotifyTarget Colonist(int id) noexcept
    {
        NotifyTarget t{};
        t.kind = Kind::Colonist;
        t.colonistId = id;
        return t;
    }
};

struct NotificationEntry
{
    double timeSeconds = 0.0;
    NotifySeverity severity = NotifySeverity::Info;
    std::string text;
    NotifyTarget target;
};

struct ToastEntry
{
    NotificationEntry entry;
    float ttlSeconds = 0.0f;
};

// Small, dependency-free notification log with optional expiring "toast" messages.
//
// Notes:
//  - The log is bounded (drop oldest on overflow).
//  - Toasts are bounded and expire by TTL.
class NotificationLog
{
public:
    [[nodiscard]] std::size_t maxLogEntries() const noexcept { return m_maxLogEntries; }
    void setMaxLogEntries(std::size_t n) noexcept
    {
        m_maxLogEntries = std::max<std::size_t>(1u, n);
        trimLog();
    }

    [[nodiscard]] std::size_t maxToasts() const noexcept { return m_maxToasts; }
    void setMaxToasts(std::size_t n) noexcept
    {
        m_maxToasts = std::max<std::size_t>(1u, n);
        trimToasts();
    }

    void clearLog() noexcept { m_log.clear(); }
    void clearToasts() noexcept { m_toasts.clear(); }
    void clearAll() noexcept
    {
        clearLog();
        clearToasts();
    }

    [[nodiscard]] const std::vector<NotificationEntry>& log() const noexcept { return m_log; }
    [[nodiscard]] const std::vector<ToastEntry>& toasts() const noexcept { return m_toasts; }

    // Push a notification into the persistent log. Optionally also create a toast.
    void push(NotificationEntry e, float toastTtlSeconds, bool pushToast) noexcept
    {
        // Persistent log always records the event.
        m_log.emplace_back(std::move(e));
        trimLog();

        if (pushToast && toastTtlSeconds > 0.0f)
        {
            ToastEntry t{};
            t.entry = m_log.back(); // copy the most-recent entry
            t.ttlSeconds = toastTtlSeconds;
            m_toasts.emplace_back(std::move(t));
            trimToasts();
        }
    }

    // Convenience overload.
    void push(std::string text,
              NotifySeverity severity,
              double timeSeconds,
              float toastTtlSeconds = 0.0f,
              NotifyTarget target = NotifyTarget::None(),
              bool pushToast = true) noexcept
    {
        NotificationEntry e{};
        e.timeSeconds = timeSeconds;
        e.severity = severity;
        e.text = std::move(text);
        e.target = target;
        push(std::move(e), toastTtlSeconds, pushToast);
    }

    // Advance toast timers and delete expired ones.
    void tick(float dtSeconds) noexcept
    {
        if (!(dtSeconds > 0.0f))
            return;

        for (auto& t : m_toasts)
            t.ttlSeconds -= dtSeconds;

        m_toasts.erase(std::remove_if(m_toasts.begin(), m_toasts.end(),
                                      [](const ToastEntry& t) { return t.ttlSeconds <= 0.0f; }),
                       m_toasts.end());
    }

private:
    void trimLog() noexcept
    {
        if (m_log.size() <= m_maxLogEntries)
            return;
        const std::size_t drop = m_log.size() - m_maxLogEntries;
        m_log.erase(m_log.begin(), m_log.begin() + static_cast<std::ptrdiff_t>(drop));
    }

    void trimToasts() noexcept
    {
        if (m_toasts.size() <= m_maxToasts)
            return;
        const std::size_t drop = m_toasts.size() - m_maxToasts;
        m_toasts.erase(m_toasts.begin(), m_toasts.begin() + static_cast<std::ptrdiff_t>(drop));
    }

    std::size_t m_maxLogEntries = 200;
    std::size_t m_maxToasts     = 6;

    std::vector<NotificationEntry> m_log;
    std::vector<ToastEntry> m_toasts;
};

} // namespace colony::game::util
