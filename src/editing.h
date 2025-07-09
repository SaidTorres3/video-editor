#pragma once

#include <windows.h>

void OnSetStartClicked(HWND hwnd);
void OnSetEndClicked(HWND hwnd);
void OnCutClicked(HWND hwnd);
void OnExportClicked(HWND hwnd);

extern bool g_lastOperationWasExport;
extern std::wstring g_lastExportedFilename;
