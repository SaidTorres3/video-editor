#pragma once

#include <cstddef>
#include <cstdint>

using EmbeddedTenVadHandle = void*;

bool EmbeddedTenVadCreate(EmbeddedTenVadHandle* handle, std::size_t hopSize,
                          float threshold);
bool EmbeddedTenVadProcess(EmbeddedTenVadHandle handle,
                           const std::int16_t* audioData,
                           std::size_t audioDataLength,
                           float* probability, int* speechFlag);
void EmbeddedTenVadDestroy(EmbeddedTenVadHandle* handle);
void ShutdownEmbeddedTenVadRuntime();
