#pragma once

#include <windows.h>
#include <string>

void OnSetStartClicked(HWND hwnd);
void OnSetEndClicked(HWND hwnd);
void OnCutClicked(HWND hwnd);
void OnExportClicked(HWND hwnd);

extern bool g_lastOperationWasExport;
extern bool g_uploadSuccess;
extern std::wstring g_uploadedUrl;
