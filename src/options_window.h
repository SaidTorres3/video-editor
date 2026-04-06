#ifndef OPTIONS_WINDOW_H
#define OPTIONS_WINDOW_H

#include <windows.h>
#include <string>

// Option identifiers used in the options window
#define ID_COMBO_ENCODER        1021
#define ID_COMBO_LOG_LEVEL      1022
#define ID_CHECKBOX_ENABLE_LOG  1023
#define ID_BUTTON_UPLOAD_CONFIG 1024
#define ID_BUTTON_CATBOX_CONFIG 1031
#define ID_BUTTON_B2_SETTINGS   1032
#define ID_CHECKBOX_AUTO_PLAY   1033
#define ID_CHECKBOX_HOVER_PREVIEW 1034
#define ID_CHECKBOX_IMPROVE_SEEK  1035
#define ID_TAB_GENERAL          1040
#define ID_TAB_ENCODING         1041
#define ID_TAB_UPLOAD           1042
#define ID_TAB_CONTROL          1043
#define ID_COMBO_QUALITY        1044
#define ID_PANEL_GENERAL        1046
#define ID_PANEL_ENCODING       1047
#define ID_PANEL_UPLOAD         1048

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

// Exportation settings control identifiers
#define ID_TAB_EXPORT           1049
#define ID_PANEL_EXPORT         1050
#define ID_EDIT_EXPORT_NAME     2010
#define ID_BUTTON_EXPORT_FOLDER 2011
#define ID_CHECKBOX_AUTO_EXPORT 2012
#define ID_COMBO_DEFAULT_CODEC  2013
#define ID_BUTTON_EXPORT_FOLDER_CLEAR 2014

enum class EncoderSelection : int {
    Libx264 = 0,
    Nvenc   = 1,
    Amf     = 2,
};

extern EncoderSelection g_encoderSelection;
extern bool g_logToFile;
extern bool g_autoPlay;
extern bool g_showVideoPreviewOnHover;
extern bool g_improveSeekPerformance;
extern std::wstring g_qualityPreset;

extern std::wstring g_b2KeyId;
extern std::wstring g_b2AppKey;
extern std::wstring g_b2BucketId;
extern std::wstring g_b2BucketName;
extern std::wstring g_b2CustomUrl;
extern bool g_autoUpload;
extern bool g_useCatbox;
extern bool g_useB2;
extern std::wstring g_catboxUserHash;

// Exportation settings
extern std::wstring g_exportSaveName;
extern std::wstring g_exportDefaultFolder;
extern bool g_exportAutoSave;
extern bool g_exportDefaultCodecH264; // true = H264, false = Copy codec

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
