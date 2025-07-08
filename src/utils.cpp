#include "utils.h"
#include <uxtheme.h>

extern HFONT g_hFont;

std::wstring FormatTime(double totalSeconds, bool showMilliseconds)
{
    if (totalSeconds < 0) totalSeconds = 0;
    int hours = static_cast<int>(totalSeconds) / 3600;
    int minutes = (static_cast<int>(totalSeconds) % 3600) / 60;
    int seconds = static_cast<int>(totalSeconds) % 60;

    wchar_t buffer[64];
    if (showMilliseconds)
    {
        int milliseconds = static_cast<int>((totalSeconds - static_cast<int>(totalSeconds)) * 100);
        if (hours > 0)
            swprintf_s(buffer, _countof(buffer), L"%d:%02d:%02d.%02d", hours, minutes, seconds, milliseconds);
        else
            swprintf_s(buffer, _countof(buffer), L"%02d:%02d.%02d", minutes, seconds, milliseconds);
    }
    else
    {
        if (hours > 0)
            swprintf_s(buffer, _countof(buffer), L"%d:%02d:%02d", hours, minutes, seconds);
        else
            swprintf_s(buffer, _countof(buffer), L"%02d:%02d", minutes, seconds);
    }
    return std::wstring(buffer);
}

double ParseTimeString(const std::wstring& str)
{
    int h = 0, m = 0;
    double s = 0.0;
    if (swscanf(str.c_str(), L"%d:%d:%lf", &h, &m, &s) == 3)
        return h * 3600 + m * 60 + s;
    if (swscanf(str.c_str(), L"%d:%lf", &m, &s) == 2)
        return m * 60 + s;
    return -1.0;
}

void ApplyDarkTheme(HWND hwnd)
{
    if (g_hFont)
        SendMessage(hwnd, WM_SETFONT, (WPARAM)g_hFont, TRUE);
    SetWindowTheme(hwnd, L"DarkMode_Explorer", nullptr);
}
