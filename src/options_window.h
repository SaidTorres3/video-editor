#ifndef OPTIONS_WINDOW_H
#define OPTIONS_WINDOW_H

#include <windows.h>
#include <string>

// Option identifiers used in the options window
#define ID_RADIO_ENCODER_LIBX264 1021
#define ID_RADIO_ENCODER_NVENC  1022
#define ID_CHECKBOX_ENABLE_LOG  1023
#define ID_BUTTON_UPLOAD_CONFIG 1024
#define ID_BUTTON_CATBOX_CONFIG 1031
#define ID_BUTTON_B2_SETTINGS   1032

// B2 config control identifiers
#define ID_EDIT_B2_KEY_ID       2001
#define ID_EDIT_B2_APP_KEY      2002
#define ID_EDIT_B2_BUCKET_ID    2003
#define ID_EDIT_B2_BUCKET_NAME  2004
#define ID_CHECKBOX_AUTO_UPLOAD 2005
#define ID_EDIT_B2_CUSTOM_URL   2006
#define ID_EDIT_CATBOX_HASH     2007
#define ID_CHECKBOX_USE_CATBOX  2008
#define ID_CHECKBOX_USE_B2      2009

extern bool g_useNvenc;
extern bool g_logToFile;

extern std::wstring g_b2KeyId;
extern std::wstring g_b2AppKey;
extern std::wstring g_b2BucketId;
extern std::wstring g_b2BucketName;
extern std::wstring g_b2CustomUrl;
extern bool g_autoUpload;
extern bool g_useCatbox;
extern bool g_useB2;
extern std::wstring g_catboxUserHash;

void ShowUploadWindow(HWND parent);
LRESULT CALLBACK UploadProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
void ShowB2ConfigWindow(HWND parent);
LRESULT CALLBACK B2ConfigProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
void ShowCatboxConfigWindow(HWND parent);
LRESULT CALLBACK CatboxConfigProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

void ShowOptionsWindow(HWND parent);
LRESULT CALLBACK OptionsProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
void LoadSettings();
void SaveSettings();

#endif // OPTIONS_WINDOW_H
