cmake_minimum_required(VERSION 3.10)

# Política para runtime MSVC
if(POLICY CMP0091)
    cmake_policy(SET CMP0091 NEW)
endif()

project(VideoEditor CXX)

if(USE_STATIC_FFMPEG)
    message(STATUS "🔗 Enlazando FFmpeg estáticamente")
endif()
if(NOT USE_STATIC_FFMPEG)
    message(STATUS "🔗 Enlazando FFmpeg dinámicamente")
endif()

# Si usas MSVC y FFmpeg estático, forzamos runtime estático y LTO
if(MSVC AND USE_STATIC_FFMPEG)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
endif()

# C++17 obligatorio
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# UNICODE en Win32
add_compile_definitions(UNICODE _UNICODE)

# Rutas por defecto (puedes sobreescribir con -DFFMPEG_ROOT=... o -DCURL_ROOT=...)
set(FFMPEG_ROOT "C:/Program Files/ffmpeg" CACHE PATH "Path to FFmpeg installation")
set(CURL_ROOT   "${CMAKE_SOURCE_DIR}/vendor/libcurl" CACHE PATH "Path to libcurl installation")

# ==== BUSCAR FFmpeg ====
find_path(FFMPEG_INCLUDE_DIR
    NAMES libavcodec/avcodec.h
    PATHS ${FFMPEG_ROOT}/include
    NO_DEFAULT_PATH
)
find_library(AVCODEC_LIBRARY    NAMES avcodec    PATHS ${FFMPEG_ROOT}/lib NO_DEFAULT_PATH)
find_library(AVFORMAT_LIBRARY   NAMES avformat   PATHS ${FFMPEG_ROOT}/lib NO_DEFAULT_PATH)
find_library(AVUTIL_LIBRARY     NAMES avutil     PATHS ${FFMPEG_ROOT}/lib NO_DEFAULT_PATH)
find_library(SWSCALE_LIBRARY    NAMES swscale    PATHS ${FFMPEG_ROOT}/lib NO_DEFAULT_PATH)
find_library(SWRESAMPLE_LIBRARY NAMES swresample PATHS ${FFMPEG_ROOT}/lib NO_DEFAULT_PATH)

message(STATUS ">> FFmpeg include: ${FFMPEG_INCLUDE_DIR}")
message(STATUS ">> avcodec lib:    ${AVCODEC_LIBRARY}")

# ==== IMPORTED TARGET: libcurl ====
add_library(VENDOR_LIBCURL STATIC IMPORTED GLOBAL)
set_target_properties(VENDOR_LIBCURL PROPERTIES
    IMPORTED_LOCATION_DEBUG   "${CURL_ROOT}/lib/libcurl.lib"
    IMPORTED_LOCATION_RELEASE "${CURL_ROOT}/lib/libcurl.lib"
    INTERFACE_INCLUDE_DIRECTORIES "${CURL_ROOT}/include"
)
if(NOT EXISTS "${CURL_ROOT}/lib/libcurl.lib")
    message(FATAL_ERROR "❌ libcurl.lib no existe en ${CURL_ROOT}/lib")
endif()
message(STATUS ">> Usando libcurl vendorizado: ${CURL_ROOT}/lib/libcurl.lib")

# ==== IMPORTED TARGET: zlib (solo para build dinámico) ====
if(NOT USE_STATIC_FFMPEG)
    add_library(VENDOR_ZLIB STATIC IMPORTED GLOBAL)
    message(STATUS ">> Usando zlib vendorizado: ${CURL_ROOT}/lib/zlib.lib")
    set_target_properties(VENDOR_ZLIB PROPERTIES
        IMPORTED_LOCATION_DEBUG   "${CURL_ROOT}/lib/zlib.lib"
        IMPORTED_LOCATION_RELEASE "${CURL_ROOT}/lib/zlib.lib"
    )
    if(NOT EXISTS "${CURL_ROOT}/lib/zlib.lib")
        message(FATAL_ERROR "❌ zlib.lib no existe en ${CURL_ROOT}/lib")
    endif()
    message(STATUS ">> Usando zlib vendorizado: ${CURL_ROOT}/lib/zlib.lib")
endif()

# ==== EJECUTABLE Y CÓDIGO FUENTE ====
add_executable(VideoEditor WIN32
    src/main.cpp
    src/window_proc.cpp
    src/ui_controls.cpp
    src/file_handling.cpp
    src/ui_updates.cpp
    src/timeline.cpp
    src/editing.cpp
    src/utils.cpp
    src/video_decoder.cpp
    src/audio_player.cpp
    src/video_renderer.cpp
    src/video_cutter.cpp
    src/video_player.cpp
    src/options_window.cpp
    src/progress_window.cpp
    src/b2_upload.cpp
)

# ==== DIRECTORIOS DE INCLUDES ====
target_include_directories(VideoEditor PRIVATE
    src
    ${FFMPEG_INCLUDE_DIR}
)

# ==== LIBS DE PLATAFORMA WINDOWS ====
set(PLATFORM_LIBS
    user32 gdi32 d2d1 comctl32 comdlg32 ole32 winmm
    dwmapi uxtheme shell32 ws2_32 bcrypt secur32 mfplat
    mf mfuuid strmiids crypt32 advapi32
)

# ==== LIBS DE FFMPEG ====
set(FFMPEG_LIBS
    ${AVCODEC_LIBRARY}
    ${AVFORMAT_LIBRARY}
    ${AVUTIL_LIBRARY}
    ${SWSCALE_LIBRARY}
    ${SWRESAMPLE_LIBRARY}
)
if(USE_STATIC_FFMPEG)
    # Enlazamos también todas las .lib extra en modo estático
    file(GLOB _all_ffmpeg_libs "${FFMPEG_ROOT}/lib/*.lib")
    list(FILTER _all_ffmpeg_libs EXCLUDE REGEX "avcodec|avformat|avutil|swscale|swresample")
    list(APPEND FFMPEG_LIBS ${_all_ffmpeg_libs})
endif()

# ==== LINKEO FINAL ====
target_link_libraries(VideoEditor PRIVATE
    ${PLATFORM_LIBS}
    ${FFMPEG_LIBS}
    VENDOR_LIBCURL
    $<$<BOOL:NOT USE_STATIC_FFMPEG>:VENDOR_ZLIB>
)

# ==== COPIA DE DLLS DE FFMPEG (dinámico) ====
if(WIN32 AND NOT USE_STATIC_FFMPEG)
    add_custom_command(TARGET VideoEditor POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${FFMPEG_ROOT}/bin/avcodec-62.dll"   "$<TARGET_FILE_DIR:VideoEditor>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${FFMPEG_ROOT}/bin/avformat-62.dll"  "$<TARGET_FILE_DIR:VideoEditor>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${FFMPEG_ROOT}/bin/avutil-60.dll"    "$<TARGET_FILE_DIR:VideoEditor>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${FFMPEG_ROOT}/bin/swscale-9.dll"    "$<TARGET_FILE_DIR:VideoEditor>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${FFMPEG_ROOT}/bin/swresample-6.dll" "$<TARGET_FILE_DIR:VideoEditor>"
        COMMENT "Copiando DLLs de FFmpeg dinámico..."
    )
endif()
