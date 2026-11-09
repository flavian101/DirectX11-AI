#pragma once
#include "Logger.h"
#include <string>
#include <vector>

// The Console: everything the engine writes with LOG_INFO / LOG_WARNING / LOG_ERROR, live, in the editor. The same lines
// still go to d3d11_log.txt, so nothing is lost when the editor is closed.
class ConsolePanel {
public:
    void Draw(bool* open);

private:
    struct Line {
        Logger::LogLevel level = Logger::Info;
        std::string text;      // "14:03:59  message"
        std::string lowered;   // for the filter
    };
    void Pull();   // takes the lines written since last frame

    std::vector<Line> lines_;
    unsigned long long lastSeen_ = 0;
    bool showDebug_ = false;
    bool showInfo_ = true;
    bool showWarnings_ = true;
    bool showErrors_ = true;
    bool autoScroll_ = true;
    char filter_[96] = "";
    int counts_[4] = {};   // per level, for the buttons
};
