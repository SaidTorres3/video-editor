#pragma once
#include <string>
#include <windows.h>

bool UploadToB2(const std::wstring& filePath, std::string& outUrl, HWND progressBar = nullptr);
