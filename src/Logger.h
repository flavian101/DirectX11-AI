#pragma once
#include <windows.h>
#include <d3d11.h>
#include <fstream>
#include <mutex>
#include <string>

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
};

#define LOG_DEBUG(msg)   Logger::Instance().LogDebug(msg)
#define LOG_INFO(msg)    Logger::Instance().LogInfo(msg)
#define LOG_WARNING(msg) Logger::Instance().LogWarning(msg)
#define LOG_ERROR(msg)   Logger::Instance().LogErrorWithContext(msg, __FILE__, __LINE__, __FUNCTION__, "")
#define LOG_HR(hr, msg)  Logger::Instance().LogDXError(hr, __FILE__, __LINE__, __FUNCTION__, msg)
