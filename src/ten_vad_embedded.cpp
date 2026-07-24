#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "ten_vad_embedded.h"

#include <windows.h>

#ifdef TEN_VAD_IN_MEMORY
#include "MemoryModule.h"
#endif

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

#ifdef TEN_VAD_IN_MEMORY
using TenVadModule = HMEMORYMODULE;
#else
using TenVadModule = HMODULE;
#endif

struct EmbeddedTenVadRuntime
{
    TenVadModule module = nullptr;
    TenVadCreateFn create = nullptr;
    TenVadProcessFn process = nullptr;
    TenVadDestroyFn destroy = nullptr;
    bool available = false;

    void Unload()
    {
        if (module)
        {
#ifdef TEN_VAD_IN_MEMORY
            MemoryFreeLibrary(module);
#else
            FreeLibrary(module);
#endif
            module = nullptr;
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

FARPROC ResolveSymbol(TenVadModule module, const char* name)
{
#ifdef TEN_VAD_IN_MEMORY
    return MemoryGetProcAddress(module, name);
#else
    return GetProcAddress(module, name);
#endif
}

#ifdef TEN_VAD_IN_MEMORY
TenVadModule LoadTenVadModule()
{
    HMODULE executable = GetModuleHandleW(nullptr);
    HRSRC resource = FindResourceW(
        executable, MAKEINTRESOURCEW(TEN_VAD_RESOURCE_ID), RT_RCDATA);
    if (!resource)
        return nullptr;

    HGLOBAL loadedResource = LoadResource(executable, resource);
    const DWORD resourceSize = SizeofResource(executable, resource);
    const void* resourceData =
        loadedResource ? LockResource(loadedResource) : nullptr;
    if (!resourceData || resourceSize == 0)
        return nullptr;

    return MemoryLoadLibrary(resourceData, resourceSize);
}
#else
TenVadModule LoadTenVadModule()
{
    wchar_t executablePath[MAX_PATH] = {};
    const DWORD pathLength =
        GetModuleFileNameW(nullptr, executablePath, MAX_PATH);
    if (pathLength == 0 || pathLength == MAX_PATH)
        return nullptr;

    wchar_t* lastSlash = wcsrchr(executablePath, L'\\');
    if (!lastSlash)
        return nullptr;
    *lastSlash = L'\0';

    const std::wstring dllPath =
        std::wstring(executablePath) + L"\\ten_vad.dll";
    return LoadLibraryExW(
        dllPath.c_str(), nullptr,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
}
#endif

bool LoadEmbeddedRuntime()
{
    EmbeddedTenVadRuntime& runtime = GetRuntime();
    runtime.module = LoadTenVadModule();
    if (!runtime.module)
        return false;

    runtime.create = reinterpret_cast<TenVadCreateFn>(
        ResolveSymbol(runtime.module, "ten_vad_create"));
    runtime.process = reinterpret_cast<TenVadProcessFn>(
        ResolveSymbol(runtime.module, "ten_vad_process"));
    runtime.destroy = reinterpret_cast<TenVadDestroyFn>(
        ResolveSymbol(runtime.module, "ten_vad_destroy"));
    if (!runtime.create || !runtime.process || !runtime.destroy)
    {
        runtime.Unload();
        return false;
    }

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
