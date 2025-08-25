#pragma once

#include <windows.h>
#include <string>

void OpenVideoFile(HWND hwnd);
void LoadVideoFile(HWND hwnd, const std::wstring& filename);

extern std::wstring g_lastOpenDir;
extern std::wstring g_lastSaveDir;
