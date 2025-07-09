#pragma once
#include <string>
#include <windows.h>

// Uploads the file at filePath to Backblaze B2.
// Returns true on success and sets outUrl to the
// downloadable URL of the uploaded file.
bool UploadToBackblaze(const std::wstring& filePath, std::wstring& outUrl);
