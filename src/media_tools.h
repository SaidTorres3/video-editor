#pragma once
#include <windows.h>
constexpr int ID_MEDIA_ALIGN = 1060;
constexpr int ID_MEDIA_OPENFX = 1061;
void AddMediaToolsMenu(HWND window);
void AlignAudioTracks(HWND window);
void ApplyOpenFx(HWND window);
