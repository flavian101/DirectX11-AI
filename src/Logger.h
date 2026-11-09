#pragma once
#include <windows.h>
#include <d3d11.h>
#include <fstream>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

class Logger {
public:
    enum LogLevel { Debug, Info, Warning, Error };
    static Logger& Instance();
    void Log(LogLevel level, const std::string& message);
    void LogInfo(const std::string& message);
    void LogDebug(const std::string& message);
    void LogWarning(const std::string& message);
    void LogError(const std::string& message);
    void LogErrorWithContext(const std::string& message, const std::string& file, int line,
                             const std::string& function, const std::string& context);
    void LogDXError(HRESULT hr, const std::string& file, int line,
                    const std::string& function, const std::string& message,
                    ID3D11Device* device = nullptr);
    // The most recent lines are also kept in memory (the last 1000), for the editor's Console panel.
    struct Entry {
        unsigned long long index = 0;   // 1, 2, 3 ... in the order written
        LogLevel level = Info;
        std::string time;               // "14:03:59"
        std::string text;               // the message, without the time and level
    };
    // Appends every line written after number `lastSeen` (0 = all that are still kept) to `out`. Returns the number of
    // the newest line, to pass in next time.
    unsigned long long CopyNewerThan(unsigned long long lastSeen, std::vector<Entry>& out);
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;
private:
    Logger();
    ~Logger();
    std::string GetCurrentTime() const;
    static const char* LevelName(LogLevel level);
    std::string D3D11ErrorString(HRESULT hr, ID3D11Device* device);
    void Write(LogLevel level, const std::string& message);
    std::ofstream m_fileStream;
    std::mutex m_mutex;
    std::deque<Entry> m_recent;
    unsigned long long m_counter = 0;
};

#define LOG_DEBUG(msg)   Logger::Instance().LogDebug(msg)
#define LOG_INFO(msg)    Logger::Instance().LogInfo(msg)
#define LOG_WARNING(msg) Logger::Instance().LogWarning(msg)
#define LOG_ERROR(msg)   Logger::Instance().LogErrorWithContext(msg, __FILE__, __LINE__, __FUNCTION__, "")
#define LOG_HR(hr, msg)  Logger::Instance().LogDXError(hr, __FILE__, __LINE__, __FUNCTION__, msg)
