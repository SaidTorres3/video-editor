#include "debug_log.h"
#include "options_window.h"
#include <fstream>
#include <Windows.h>

static std::ofstream g_debugFile;

void DebugLog(const std::string& msg, bool popup) {
    if (g_logToFile) {
        if (!g_debugFile.is_open())
            g_debugFile.open("debug.log", std::ios::app);
        if (g_debugFile.is_open())
            g_debugFile << msg << std::endl;
    }
    OutputDebugStringA((msg + "\n").c_str());
    if (popup) {
        MessageBoxA(nullptr, msg.c_str(), "Video Editor Debug", MB_OK | MB_ICONINFORMATION);
    }
}
