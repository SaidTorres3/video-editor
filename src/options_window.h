#ifndef OPTIONS_WINDOW_H
#define OPTIONS_WINDOW_H

#include <windows.h>

// Option identifiers used in the options window
#define ID_RADIO_ENCODER_LIBX264 1021
#define ID_RADIO_ENCODER_NVENC  1022
#define ID_CHECKBOX_ENABLE_LOG  1023

extern bool g_useNvenc;
extern bool g_logToFile;

void ShowOptionsWindow(HWND parent);
LRESULT CALLBACK OptionsProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
void LoadSettings();
void SaveSettings();

#endif // OPTIONS_WINDOW_H
