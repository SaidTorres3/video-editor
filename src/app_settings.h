#pragma once

#include <string>

enum class EncoderSelection : int {
    Libx264 = 0,
    Nvenc   = 1,
    Amf     = 2,
};

extern EncoderSelection g_encoderSelection;
extern bool g_logToFile;
extern bool g_autoPlay;

extern std::wstring g_b2KeyId;
extern std::wstring g_b2AppKey;
extern std::wstring g_b2BucketId;
extern std::wstring g_b2BucketName;
extern std::wstring g_b2CustomUrl;
extern bool g_autoUpload;
extern bool g_useCatbox;
extern bool g_useB2;
extern std::wstring g_catboxUserHash;

void LoadSettings();
void SaveSettings();

