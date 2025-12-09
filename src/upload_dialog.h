#pragma once
#include <windows.h>
#include <string>

void ShowUrlCopyDialog(HWND parent, const std::wstring& message, const std::wstring& url1, const std::wstring& url2 = L"");
LRESULT CALLBACK UrlCopyProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
void ShowManualUploadDialog(HWND parent, const std::wstring& exportPath, bool allowCatbox, bool allowB2);
LRESULT CALLBACK ManualUploadProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
