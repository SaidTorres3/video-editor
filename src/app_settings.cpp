#include "app_settings.h"

#include <windows.h>

EncoderSelection g_encoderSelection = EncoderSelection::Libx264;
bool g_logToFile = true;
bool g_autoPlay = true;

std::wstring g_b2KeyId;
std::wstring g_b2AppKey;
std::wstring g_b2BucketId;
std::wstring g_b2BucketName;
std::wstring g_b2CustomUrl;
bool g_autoUpload = false;
bool g_useCatbox = false;
bool g_useB2 = true;
std::wstring g_catboxUserHash;

void LoadSettings()
{
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\VideoEditor", 0, KEY_READ, &hKey) == ERROR_SUCCESS)
    {
        DWORD val;
        DWORD size = sizeof(val);
        if (RegQueryValueExW(hKey, L"EncoderType", nullptr, nullptr, (LPBYTE)&val, &size) == ERROR_SUCCESS)
        {
            if (val <= static_cast<DWORD>(EncoderSelection::Amf))
                g_encoderSelection = static_cast<EncoderSelection>(val);
        }
        else if (RegQueryValueExW(hKey, L"UseNvenc", nullptr, nullptr, (LPBYTE)&val, &size) == ERROR_SUCCESS)
        {
            g_encoderSelection = (val != 0) ? EncoderSelection::Nvenc : EncoderSelection::Libx264;
        }

        size = sizeof(val);
        if (RegQueryValueExW(hKey, L"EnableLogFile", nullptr, nullptr, (LPBYTE)&val, &size) == ERROR_SUCCESS)
            g_logToFile = (val != 0);

        size = sizeof(val);
        if (RegQueryValueExW(hKey, L"AutoPlay", nullptr, nullptr, (LPBYTE)&val, &size) == ERROR_SUCCESS)
            g_autoPlay = (val != 0);

        wchar_t buf[256];
        DWORD sz = sizeof(buf);
        if (RegQueryValueExW(hKey, L"B2KeyId", nullptr, nullptr, (LPBYTE)buf, &sz) == ERROR_SUCCESS)
            g_b2KeyId = buf;
        sz = sizeof(buf);
        if (RegQueryValueExW(hKey, L"B2AppKey", nullptr, nullptr, (LPBYTE)buf, &sz) == ERROR_SUCCESS)
            g_b2AppKey = buf;
        sz = sizeof(buf);
        if (RegQueryValueExW(hKey, L"B2BucketId", nullptr, nullptr, (LPBYTE)buf, &sz) == ERROR_SUCCESS)
            g_b2BucketId = buf;
        sz = sizeof(buf);
        if (RegQueryValueExW(hKey, L"B2BucketName", nullptr, nullptr, (LPBYTE)buf, &sz) == ERROR_SUCCESS)
            g_b2BucketName = buf;
        sz = sizeof(buf);
        if (RegQueryValueExW(hKey, L"B2CustomUrl", nullptr, nullptr, (LPBYTE)buf, &sz) == ERROR_SUCCESS)
            g_b2CustomUrl = buf;

        sz = sizeof(DWORD);
        val = 0;
        if (RegQueryValueExW(hKey, L"AutoUpload", nullptr, nullptr, (LPBYTE)&val, &sz) == ERROR_SUCCESS)
            g_autoUpload = val != 0;

        sz = sizeof(DWORD);
        val = 0;
        if (RegQueryValueExW(hKey, L"UseCatbox", nullptr, nullptr, (LPBYTE)&val, &sz) == ERROR_SUCCESS)
            g_useCatbox = val != 0;

        sz = sizeof(DWORD);
        val = 1;
        if (RegQueryValueExW(hKey, L"UseB2", nullptr, nullptr, (LPBYTE)&val, &sz) == ERROR_SUCCESS)
            g_useB2 = val != 0;

        sz = sizeof(buf);
        if (RegQueryValueExW(hKey, L"CatboxHash", nullptr, nullptr, (LPBYTE)buf, &sz) == ERROR_SUCCESS)
            g_catboxUserHash = buf;

        RegCloseKey(hKey);
    }
}

void SaveSettings()
{
    HKEY hKey;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\VideoEditor", 0, nullptr, 0, KEY_WRITE, nullptr, &hKey, nullptr) ==
        ERROR_SUCCESS)
    {
        DWORD val = static_cast<DWORD>(g_encoderSelection);
        RegSetValueExW(hKey, L"EncoderType", 0, REG_DWORD, (const BYTE*)&val, sizeof(val));

        val = g_encoderSelection == EncoderSelection::Nvenc ? 1 : 0;
        RegSetValueExW(hKey, L"UseNvenc", 0, REG_DWORD, (const BYTE*)&val, sizeof(val));

        val = g_logToFile ? 1 : 0;
        RegSetValueExW(hKey, L"EnableLogFile", 0, REG_DWORD, (const BYTE*)&val, sizeof(val));

        val = g_autoPlay ? 1 : 0;
        RegSetValueExW(hKey, L"AutoPlay", 0, REG_DWORD, (const BYTE*)&val, sizeof(val));

        RegSetValueExW(hKey, L"B2KeyId", 0, REG_SZ, (const BYTE*)g_b2KeyId.c_str(),
                       (DWORD)((g_b2KeyId.size() + 1) * sizeof(wchar_t)));
        RegSetValueExW(hKey, L"B2AppKey", 0, REG_SZ, (const BYTE*)g_b2AppKey.c_str(),
                       (DWORD)((g_b2AppKey.size() + 1) * sizeof(wchar_t)));
        RegSetValueExW(hKey, L"B2BucketId", 0, REG_SZ, (const BYTE*)g_b2BucketId.c_str(),
                       (DWORD)((g_b2BucketId.size() + 1) * sizeof(wchar_t)));
        RegSetValueExW(hKey, L"B2BucketName", 0, REG_SZ, (const BYTE*)g_b2BucketName.c_str(),
                       (DWORD)((g_b2BucketName.size() + 1) * sizeof(wchar_t)));
        RegSetValueExW(hKey, L"B2CustomUrl", 0, REG_SZ, (const BYTE*)g_b2CustomUrl.c_str(),
                       (DWORD)((g_b2CustomUrl.size() + 1) * sizeof(wchar_t)));

        val = g_autoUpload ? 1 : 0;
        RegSetValueExW(hKey, L"AutoUpload", 0, REG_DWORD, (const BYTE*)&val, sizeof(val));

        val = g_useCatbox ? 1 : 0;
        RegSetValueExW(hKey, L"UseCatbox", 0, REG_DWORD, (const BYTE*)&val, sizeof(val));

        val = g_useB2 ? 1 : 0;
        RegSetValueExW(hKey, L"UseB2", 0, REG_DWORD, (const BYTE*)&val, sizeof(val));

        RegSetValueExW(hKey, L"CatboxHash", 0, REG_SZ, (const BYTE*)g_catboxUserHash.c_str(),
                       (DWORD)((g_catboxUserHash.size() + 1) * sizeof(wchar_t)));

        RegCloseKey(hKey);
    }
}

