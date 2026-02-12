// file_handling.cpp - File open/save dialogs (ImGui version)
#include "file_handling.h"
#include "video_player.h"
#include "options_window.h"
#include <commdlg.h>
#include <filesystem>

extern VideoPlayer* g_videoPlayer;

// Remember last directories for open and save dialogs
std::wstring g_lastOpenDir;
std::wstring g_lastSaveDir;

void OpenVideoFile(HWND hwnd)
{
    OPENFILENAMEW ofn;
    wchar_t szFile[260] = {0};

    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile) / sizeof(wchar_t);
    ofn.lpstrFilter = L"Video Files\0*.mp4;*.avi;*.mov;*.mkv;*.wmv;*.flv;*.webm;*.m4v;*.3gp\0All Files\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;
    if (!g_lastOpenDir.empty())
        ofn.lpstrInitialDir = g_lastOpenDir.c_str();

    if (GetOpenFileNameW(&ofn))
    {
        g_lastOpenDir = std::filesystem::path(szFile).parent_path().wstring();
        LoadVideoFile(hwnd, std::wstring(szFile));
    }
}

void LoadVideoFile(HWND hwnd, const std::wstring& filename)
{
    if (g_videoPlayer && g_videoPlayer->LoadVideo(filename))
    {
        g_lastOpenDir = std::filesystem::path(filename).parent_path().wstring();
        std::wstring title = L"Video Editor - " + std::filesystem::path(filename).filename().wstring();
        SetWindowTextW(hwnd, title.c_str());

        if (g_autoPlay)
            g_videoPlayer->Play();
    }
    else
    {
        MessageBoxW(hwnd, L"Failed to load the video file. Please check FFmpeg setup.", L"Error", MB_OK | MB_ICONERROR);
    }
}
