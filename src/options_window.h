#ifndef OPTIONS_WINDOW_H
#define OPTIONS_WINDOW_H

#include <windows.h>
#include <string>

// Option identifiers used in the options window
#define ID_RADIO_ENCODER_LIBX264 1021
#define ID_RADIO_ENCODER_NVENC  1022
#define ID_CHECKBOX_ENABLE_LOG  1023
#define ID_BUTTON_B2_CONFIG     1024

// B2 config control identifiers
#define ID_EDIT_B2_KEY_ID       2001
#define ID_EDIT_B2_APP_KEY      2002
#define ID_EDIT_B2_BUCKET_ID    2003
#define ID_EDIT_B2_BUCKET_NAME  2004
#define ID_CHECKBOX_AUTO_UPLOAD 2005

extern bool g_useNvenc;
extern bool g_logToFile;

extern std::wstring g_b2KeyId;
extern std::wstring g_b2AppKey;
extern std::wstring g_b2BucketId;
extern std::wstring g_b2BucketName;
extern bool g_autoUpload;

void ShowB2ConfigWindow(HWND parent);
LRESULT CALLBACK B2ConfigProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

void ShowOptionsWindow(HWND parent);
LRESULT CALLBACK OptionsProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
void LoadSettings();
void SaveSettings();

#endif // OPTIONS_WINDOW_H
