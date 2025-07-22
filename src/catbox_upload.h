#pragma once
#include <string>
#include <windows.h>

bool UploadToCatbox(const std::wstring& filePath, std::string& outUrl, HWND progressBar = nullptr);
