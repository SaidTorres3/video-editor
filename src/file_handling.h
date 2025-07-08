#pragma once

#include <windows.h>
#include <string>

void OpenVideoFile(HWND hwnd);
void LoadVideoFile(HWND hwnd, const std::wstring& filename);
