#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "ten_vad_embedded.h"

#include <windows.h>

#include <cstdio>
#include <mutex>
#include <string>

namespace
{
constexpr int TEN_VAD_RESOURCE_ID = 3101;

using TenVadCreateFn =
    int (*)(EmbeddedTenVadHandle*, std::size_t, float);
using TenVadProcessFn =
    int (*)(EmbeddedTenVadHandle, const std::int16_t*, std::size_t, float*,
            int*);
using TenVadDestroyFn = int (*)(EmbeddedTenVadHandle*);

void DeleteDirectoryContents(const std::wstring& dirPath)
{
    std::wstring searchPattern = dirPath + L"\\*";
    WIN32_FIND_DATAW findData;
    HANDLE hFind = FindFirstFileW(searchPattern.c_str(), &findData);
    if (hFind != INVALID_HANDLE_VALUE)
    {
        do
        {
            if (wcscmp(findData.cFileName, L".") != 0 && wcscmp(findData.cFileName, L"..") != 0)
            {
                std::wstring fullPath = dirPath + L"\\" + findData.cFileName;
                if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                {
                    DeleteDirectoryContents(fullPath);
                    RemoveDirectoryW(fullPath.c_str());
                }
                else
                {
                    DeleteFileW(fullPath.c_str());
                }
            }
        } while (FindNextFileW(hFind, &findData));
        FindClose(hFind);
    }
}

void CleanupStaleTempDirectories(const wchar_t* tempPath)
{
    std::wstring searchPattern = std::wstring(tempPath) + L"VideoEditor-ten-vad-*";
    WIN32_FIND_DATAW findData;
    HANDLE hFind = FindFirstFileW(searchPattern.c_str(), &findData);
    if (hFind != INVALID_HANDLE_VALUE)
    {
        do
        {
            if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            {
                std::wstring dirPath = std::wstring(tempPath) + findData.cFileName;
                DeleteDirectoryContents(dirPath);
                RemoveDirectoryW(dirPath.c_str());
            }
        } while (FindNextFileW(hFind, &findData));
        FindClose(hFind);
    }
}

struct EmbeddedTenVadRuntime
{
    HMODULE module = nullptr;
    std::wstring extractedPath;
    std::wstring extractedDirectory;
    TenVadCreateFn create = nullptr;
    TenVadProcessFn process = nullptr;
    TenVadDestroyFn destroy = nullptr;
    bool available = false;

    void Unload()
    {
        if (module)
        {
            FreeLibrary(module);
            module = nullptr;
        }
        if (!extractedPath.empty())
        {
            bool deleted = false;
            for (int retry = 0; retry < 10; ++retry)
            {
                if (DeleteFileW(extractedPath.c_str()))
                {
                    deleted = true;
                    break;
                }
                Sleep(20);
            }
            if (!deleted)
            {
                MoveFileExW(extractedPath.c_str(), nullptr,
                            MOVEFILE_DELAY_UNTIL_REBOOT);
            }
            extractedPath.clear();
        }
        if (!extractedDirectory.empty())
        {
            for (int retry = 0; retry < 10; ++retry)
            {
                if (RemoveDirectoryW(extractedDirectory.c_str()))
                {
                    break;
                }
                Sleep(20);
            }
            extractedDirectory.clear();
        }
        create = nullptr;
        process = nullptr;
        destroy = nullptr;
        available = false;
    }

    ~EmbeddedTenVadRuntime() { Unload(); }
};

EmbeddedTenVadRuntime& GetRuntime()
{
    static EmbeddedTenVadRuntime runtime;
    return runtime;
}

bool WriteAll(HANDLE file, const void* data, DWORD byteCount)
{
    const auto* bytes = static_cast<const unsigned char*>(data);
    DWORD writtenTotal = 0;
    while (writtenTotal < byteCount)
    {
        DWORD written = 0;
        if (!WriteFile(file, bytes + writtenTotal, byteCount - writtenTotal,
                       &written, nullptr) ||
            written == 0)
        {
            return false;
        }
        writtenTotal += written;
    }
    return true;
}

bool LoadEmbeddedRuntime()
{
    EmbeddedTenVadRuntime& runtime = GetRuntime();

    // Try loading ten_vad.dll directly from the application executable folder first (e.g. dynamic build)
    wchar_t exePath[MAX_PATH] = {};
    if (GetModuleFileNameW(nullptr, exePath, MAX_PATH) > 0)
    {
        wchar_t* lastSlash = wcsrchr(exePath, L'\\');
        if (lastSlash)
        {
            *lastSlash = L'\0';
            std::wstring localDllPath = std::wstring(exePath) + L"\\ten_vad.dll";
            DWORD attrib = GetFileAttributesW(localDllPath.c_str());
            if (attrib != INVALID_FILE_ATTRIBUTES && !(attrib & FILE_ATTRIBUTE_DIRECTORY))
            {
                runtime.module = LoadLibraryExW(
                    localDllPath.c_str(), nullptr,
                    LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
                if (runtime.module)
                {
                    runtime.create = reinterpret_cast<TenVadCreateFn>(
                        GetProcAddress(runtime.module, "ten_vad_create"));
                    runtime.process = reinterpret_cast<TenVadProcessFn>(
                        GetProcAddress(runtime.module, "ten_vad_process"));
                    runtime.destroy = reinterpret_cast<TenVadDestroyFn>(
                        GetProcAddress(runtime.module, "ten_vad_destroy"));
                    if (runtime.create && runtime.process && runtime.destroy)
                    {
                        runtime.available = true;
                        runtime.extractedDirectory.clear();
                        runtime.extractedPath.clear();
                        return true;
                    }
                    FreeLibrary(runtime.module);
                    runtime.module = nullptr;
                }
            }
        }
    }

    HMODULE executable = GetModuleHandleW(nullptr);
    HRSRC resource = FindResourceW(
        executable, MAKEINTRESOURCEW(TEN_VAD_RESOURCE_ID), RT_RCDATA);
    if (!resource)
        return false;

    HGLOBAL loadedResource = LoadResource(executable, resource);
    const DWORD resourceSize = SizeofResource(executable, resource);
    const void* resourceData =
        loadedResource ? LockResource(loadedResource) : nullptr;
    if (!resourceData || resourceSize == 0)
        return false;

    wchar_t tempPath[MAX_PATH] = {};
    if (GetTempPathW(MAX_PATH, tempPath) == 0)
        return false;

    CleanupStaleTempDirectories(tempPath);

    wchar_t directoryName[96] = {};
    swprintf_s(directoryName, L"VideoEditor-ten-vad-%lu-%llu",
               GetCurrentProcessId(),
               static_cast<unsigned long long>(GetTickCount64()));
    runtime.extractedDirectory =
        std::wstring(tempPath) + directoryName;
    if (!CreateDirectoryW(runtime.extractedDirectory.c_str(), nullptr) &&
        GetLastError() != ERROR_ALREADY_EXISTS)
    {
        runtime.extractedDirectory.clear();
        return false;
    }

    runtime.extractedPath =
        runtime.extractedDirectory + L"\\ten_vad.dll";
    HANDLE file = CreateFileW(
        runtime.extractedPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ,
        nullptr, CREATE_ALWAYS,
        FILE_ATTRIBUTE_TEMPORARY | FILE_ATTRIBUTE_NOT_CONTENT_INDEXED,
        nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return false;

    const bool written = WriteAll(file, resourceData, resourceSize);
    if (written)
        FlushFileBuffers(file);
    CloseHandle(file);
    if (!written)
        return false;

    runtime.module = LoadLibraryExW(
        runtime.extractedPath.c_str(), nullptr,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!runtime.module)
        return false;

    runtime.create = reinterpret_cast<TenVadCreateFn>(
        GetProcAddress(runtime.module, "ten_vad_create"));
    runtime.process = reinterpret_cast<TenVadProcessFn>(
        GetProcAddress(runtime.module, "ten_vad_process"));
    runtime.destroy = reinterpret_cast<TenVadDestroyFn>(
        GetProcAddress(runtime.module, "ten_vad_destroy"));
    if (!runtime.create || !runtime.process || !runtime.destroy)
        return false;

    runtime.available = true;
    return true;
}

bool EnsureRuntimeLoaded()
{
    static std::once_flag loadOnce;
    std::call_once(loadOnce, [] {
        if (!LoadEmbeddedRuntime())
            OutputDebugStringW(L"Unable to load embedded TEN VAD runtime.\n");
    });
    return GetRuntime().available;
}
} // namespace

bool EmbeddedTenVadCreate(EmbeddedTenVadHandle* handle, std::size_t hopSize,
                          float threshold)
{
    return handle && EnsureRuntimeLoaded() &&
           GetRuntime().create(handle, hopSize, threshold) == 0;
}

bool EmbeddedTenVadProcess(EmbeddedTenVadHandle handle,
                           const std::int16_t* audioData,
                           std::size_t audioDataLength,
                           float* probability, int* speechFlag)
{
    return handle && audioData && probability && speechFlag &&
           EnsureRuntimeLoaded() &&
           GetRuntime().process(handle, audioData, audioDataLength,
                                probability, speechFlag) == 0;
}

void EmbeddedTenVadDestroy(EmbeddedTenVadHandle* handle)
{
    if (handle && *handle && EnsureRuntimeLoaded())
        GetRuntime().destroy(handle);
}

void ShutdownEmbeddedTenVadRuntime()
{
    GetRuntime().Unload();
}
