cmake_minimum_required(VERSION 3.10)

# Policy for MSVC runtime
if(POLICY CMP0091)
    cmake_policy(SET CMP0091 NEW)
endif()

project(VideoEditor C CXX)

if(USE_STATIC_FFMPEG)
    message(STATUS "🔗 Linking FFmpeg statically")
else()
    message(STATUS "🔗 Linking FFmpeg dynamically")
endif()

# If using MSVC and static FFmpeg, force static runtime and enable LTO
if(MSVC AND USE_STATIC_FFMPEG)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
endif()

# Require C++17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Enable UNICODE on Win32
add_compile_definitions(UNICODE _UNICODE)

# Default paths (can override with -DFFMPEG_ROOT=... or -DCURL_ROOT=...)
set(FFMPEG_ROOT "${CMAKE_SOURCE_DIR}/third_party/ffmpeg" CACHE PATH "Path to FFmpeg installation")
set(CURL_ROOT   "${CMAKE_SOURCE_DIR}/vendor/libcurl" CACHE PATH "Path to libcurl installation")

# ==== FIND FFmpeg ====
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
    message(FATAL_ERROR "❌ libcurl.lib does not exist in ${CURL_ROOT}/lib")
endif()
message(STATUS ">> Using vendored libcurl: ${CURL_ROOT}/lib/libcurl.lib")

# ==== IMPORTED TARGET: zlib (shared build only) ====
if(NOT USE_STATIC_FFMPEG)
    add_library(VENDOR_ZLIB STATIC IMPORTED GLOBAL)
    message(STATUS ">> Using vendored zlib: ${CURL_ROOT}/lib/zlib.lib")
    set_target_properties(VENDOR_ZLIB PROPERTIES
        IMPORTED_LOCATION_DEBUG   "${CURL_ROOT}/lib/zlib.lib"
        IMPORTED_LOCATION_RELEASE "${CURL_ROOT}/lib/zlib.lib"
    )
    if(NOT EXISTS "${CURL_ROOT}/lib/zlib.lib")
        message(FATAL_ERROR "❌ zlib.lib does not exist in ${CURL_ROOT}/lib")
    endif()
elseif(USE_STATIC_FFMPEG)
    # If static, use zlib from FFmpeg
    set(VENDOR_ZLIB_INCLUDE_DIR "${FFMPEG_ROOT}/include")
    set(VENDOR_ZLIB_LIBRARIES "${FFMPEG_ROOT}/lib/zlib.lib")
    add_library(VENDOR_ZLIB INTERFACE)
    target_include_directories(VENDOR_ZLIB INTERFACE ${VENDOR_ZLIB_INCLUDE_DIR})
    target_link_libraries(VENDOR_ZLIB INTERFACE ${VENDOR_ZLIB_LIBRARIES})
    message(STATUS ">> Using FFmpeg's zlib: ${VENDOR_ZLIB_LIBRARIES}")
endif()

# ==== EXECUTABLE AND SOURCE FILES ====
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
    src/noise_reducer.cpp
    src/options_window.cpp
    src/progress_window.cpp
    src/b2_upload.cpp
    vendor/kissfft/kiss_fft.c
    vendor/kissfft/kiss_fftr.c
)

# ==== INCLUDE DIRECTORIES ====
target_include_directories(VideoEditor PRIVATE
    src
    vendor/kissfft
    ${FFMPEG_INCLUDE_DIR}
)

# ==== WINDOWS PLATFORM LIBS ====
set(PLATFORM_LIBS
    user32 gdi32 d2d1 comctl32 comdlg32 ole32 winmm
    dwmapi uxtheme shell32 ws2_32 bcrypt secur32 mfplat
    mf mfuuid strmiids crypt32 advapi32
)

# ==== FFMPEG LIBRARIES ====
set(FFMPEG_LIBS
    ${AVCODEC_LIBRARY}
    ${AVFORMAT_LIBRARY}
    ${AVUTIL_LIBRARY}
    ${SWSCALE_LIBRARY}
    ${SWRESAMPLE_LIBRARY}
)
if(USE_STATIC_FFMPEG)
    # Also link all extra .lib files for static build
    file(GLOB _all_ffmpeg_libs "${FFMPEG_ROOT}/lib/*.lib")
    list(FILTER _all_ffmpeg_libs EXCLUDE REGEX "avcodec|avformat|avutil|swscale|swresample")
    list(APPEND FFMPEG_LIBS ${_all_ffmpeg_libs})
endif()

# ==== FINAL LINKING ====
target_link_libraries(VideoEditor PRIVATE
    ${PLATFORM_LIBS}
    ${FFMPEG_LIBS}
    VENDOR_LIBCURL
    $<$<BOOL:NOT USE_STATIC_FFMPEG>:VENDOR_ZLIB>
)

# ==== COPY FFmpeg DLLS (dynamic build) ====
if(WIN32 AND NOT USE_STATIC_FFMPEG)
    add_custom_command(TARGET VideoEditor POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${FFMPEG_ROOT}/bin/avcodec-62.dll"   "$<TARGET_FILE_DIR:VideoEditor>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${FFMPEG_ROOT}/bin/avformat-62.dll"  "$<TARGET_FILE_DIR:VideoEditor>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${FFMPEG_ROOT}/bin/avutil-60.dll"    "$<TARGET_FILE_DIR:VideoEditor>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${FFMPEG_ROOT}/bin/swscale-9.dll"    "$<TARGET_FILE_DIR:VideoEditor>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${FFMPEG_ROOT}/bin/swresample-6.dll" "$<TARGET_FILE_DIR:VideoEditor>"
        COMMENT "Copying dynamic FFmpeg DLLs..."
    )
endif()
