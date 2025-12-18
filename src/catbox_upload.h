#pragma once
#include <string>
#include <windows.h>

#include "progress_callback.h"

bool UploadToCatbox(const std::wstring& filePath, std::string& outUrl, HWND progressBar = nullptr, ProgressCallback onProgress = {});
