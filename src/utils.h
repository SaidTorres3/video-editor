#pragma once

#include <windows.h>
#include <string>

std::wstring FormatTime(double totalSeconds, bool showMilliseconds = false);
double ParseTimeString(const std::wstring& str);
void ApplyDarkTheme(HWND hwnd);
