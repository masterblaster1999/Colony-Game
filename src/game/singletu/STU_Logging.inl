// ================================ Logging ====================================

class Logger {
public:
    bool Open(const std::wstring& logfile) {
        f_.open(logfile, std::ios::out | std::ios::app | std::ios::binary);
        return f_.is_open();
    }
    void Line(const std::wstring& s) {
        if (!f_) return;
        auto t = util::NowStampCompact();
        std::wstring w = L"[" + t + L"] " + s + L"\r\n";
        // Write wide characters as characters (not bytes)
        f_.write(w.c_str(), static_cast<std::streamsize>(w.size()));
        f_.flush();
    }
private:
    std::wofstream f_;
};

static Logger g_log;

