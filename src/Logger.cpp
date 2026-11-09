#include "Logger.h"
#include <chrono>
#include <ctime>
#include <iostream>
#include <iomanip>
#include <sstream>

namespace {
const char* LevelLabel(Logger::LogLevel level)
{
    switch (level) {
    case Logger::Debug: return "DEBUG";
    case Logger::Info: return "INFO";
    case Logger::Warning: return "WARNING";
    case Logger::Error: return "ERROR";
    default: return "UNKNOWN";
    }
}
}

Logger& Logger::Instance()
{
    static Logger instance;
    return instance;
}

Logger::Logger()
    : m_fileStream("d3d11_log.txt", std::ios::out | std::ios::app)
{
}

Logger::~Logger() = default;

std::string Logger::GetCurrentTime() const
{
    const auto now = std::chrono::system_clock::now();
    const auto time = std::chrono::system_clock::to_time_t(now);
    std::tm localTime{};
    localtime_s(&localTime, &time);
    std::ostringstream out;
    out << std::put_time(&localTime, "%Y-%m-%d %H:%M:%S");
    return out.str();
}

const char* Logger::LevelName(LogLevel level)
{
    return LevelLabel(level);
}

void Logger::Write(LogLevel level, const std::string& message)
{
    const std::string now = GetCurrentTime();
    std::ostringstream line;
    line << '[' << now << "] [" << LevelName(level) << "] " << message;
    const std::string formatted = line.str();

    // Kept for the Console panel. (Callers hold m_mutex: see Log().)
    constexpr size_t kKeptLines = 1000;
    Entry entry;
    entry.index = ++m_counter;
    entry.level = level;
    entry.time = now.size() >= 19 ? now.substr(11, 8) : now;   // "YYYY-MM-DD HH:MM:SS" -> "HH:MM:SS"
    entry.text = message;
    m_recent.push_back(std::move(entry));
    while (m_recent.size() > kKeptLines) m_recent.pop_front();

    std::cerr << formatted << std::endl;
    OutputDebugStringA((formatted + "\n").c_str());
    if (m_fileStream.is_open()) {
        m_fileStream << formatted << std::endl;
    }
}

unsigned long long Logger::CopyNewerThan(unsigned long long lastSeen, std::vector<Entry>& out)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    for (const Entry& entry : m_recent) if (entry.index > lastSeen) out.push_back(entry);
    return m_counter;
}

void Logger::Log(LogLevel level, const std::string& message)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    Write(level, message);
}

void Logger::LogInfo(const std::string& message) { Log(Info, message); }
void Logger::LogDebug(const std::string& message) { Log(Debug, message); }
void Logger::LogWarning(const std::string& message) { Log(Warning, message); }
void Logger::LogError(const std::string& message) { Log(Error, message); }

void Logger::LogErrorWithContext(const std::string& message, const std::string& file,
                                 int line, const std::string& function,
                                 const std::string& context)
{
    std::ostringstream details;
    details << file << ':' << line << " in " << function;
    if (!context.empty()) details << " [" << context << ']';
    details << ": " << message;
    Log(Error, details.str());
}

std::string Logger::D3D11ErrorString(HRESULT hr, ID3D11Device* device)
{
    if (device && (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET)) {
        const HRESULT reason = device->GetDeviceRemovedReason();
        if (FAILED(reason)) hr = reason;
    }

    wchar_t* message = nullptr;
    const DWORD length = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, static_cast<DWORD>(hr), MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPWSTR>(&message), 0, nullptr);

    std::ostringstream out;
    if (length && message) {
        std::wstring wide(message, length);
        LocalFree(message);
        const int bytes = WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()),
                                               nullptr, 0, nullptr, nullptr);
        if (bytes > 0) {
            std::string utf8(static_cast<size_t>(bytes), '\0');
            WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()),
                                utf8.data(), bytes, nullptr, nullptr);
            while (!utf8.empty() && (utf8.back() == '\r' || utf8.back() == '\n' || utf8.back() == ' '))
                utf8.pop_back();
            out << utf8;
        }
    } else {
        out << "Unknown HRESULT";
    }
    out << " (0x" << std::hex << std::uppercase << static_cast<unsigned long>(hr) << ')';
    return out.str();
}

void Logger::LogDXError(HRESULT hr, const std::string& file, int line,
                        const std::string& function, const std::string& message,
                        ID3D11Device* device)
{
    std::ostringstream details;
    details << file << ':' << line << " in " << function << ": ";
    if (!message.empty()) details << message << " - ";
    details << D3D11ErrorString(hr, device);
    Log(Error, details.str());
}
