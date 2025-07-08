cmake_minimum_required(VERSION 3.10)

# Ensure newer policy for MSVC runtime selection when available
if(POLICY CMP0091)
    cmake_policy(SET CMP0091 NEW)
endif()
project(VideoEditor CXX)

# Option to link FFmpeg statically
option(USE_STATIC_FFMPEG "Link FFmpeg statically" OFF)

if(MSVC AND USE_STATIC_FFMPEG)
    # Link the MSVC runtime statically when building a portable executable
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    # Enable link-time optimization to reduce overhead in the static build
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Define UNICODE for Windows API calls
add_compile_definitions(UNICODE _UNICODE)

# Set FFmpeg path - UPDATE THIS PATH to where you extracted FFmpeg
set(FFMPEG_ROOT "C:/Program Files/ffmpeg" CACHE PATH "Path to FFmpeg")

# Find FFmpeg libraries
find_path(FFMPEG_INCLUDE_DIR 
    NAMES libavcodec/avcodec.h
    PATHS ${FFMPEG_ROOT}/include
    NO_DEFAULT_PATH
)

find_library(AVCODEC_LIBRARY
    NAMES avcodec
    PATHS ${FFMPEG_ROOT}/lib
    NO_DEFAULT_PATH
)

find_library(AVFORMAT_LIBRARY
    NAMES avformat
    PATHS ${FFMPEG_ROOT}/lib
    NO_DEFAULT_PATH
)

find_library(AVUTIL_LIBRARY
    NAMES avutil
    PATHS ${FFMPEG_ROOT}/lib
    NO_DEFAULT_PATH
)

find_library(SWSCALE_LIBRARY
    NAMES swscale
    PATHS ${FFMPEG_ROOT}/lib
    NO_DEFAULT_PATH
)

find_library(SWRESAMPLE_LIBRARY
    NAMES swresample
    PATHS ${FFMPEG_ROOT}/lib
    NO_DEFAULT_PATH
)

# Create the executable
add_executable(VideoEditor WIN32
    src/main.cpp
    src/video_player.cpp
    src/options_window.cpp
)

# Headers live in src/
target_include_directories(VideoEditor PRIVATE src)

# Include FFmpeg headers
if(FFMPEG_INCLUDE_DIR)
    target_include_directories(VideoEditor PRIVATE ${FFMPEG_INCLUDE_DIR})
endif()

# Link libraries
set(FFMPEG_EXTRA_LIBS "")
if(USE_STATIC_FFMPEG)
    # When linking the static variant from vcpkg, FFmpeg depends on several
    # additional libraries (zlib, libxml2, libmp3lame, etc.).  Rather than
    # hardcoding each one, gather all libraries in the FFmpeg lib folder and
    # link them as well.  This prevents unresolved symbols when optional
    # features are enabled.
    file(GLOB _all_ffmpeg_libs "${FFMPEG_ROOT}/lib/*.lib")
    list(FILTER _all_ffmpeg_libs EXCLUDE REGEX "avcodec|avformat|avutil|swscale|swresample")
    set(FFMPEG_EXTRA_LIBS ${_all_ffmpeg_libs})
endif()

target_link_libraries(VideoEditor PRIVATE
    user32
    gdi32
    d2d1
    comctl32
    comdlg32
    ole32
    winmm
    dwmapi
    uxtheme
    shell32
    ws2_32
    bcrypt
    secur32
    mfplat
    mf
    mfuuid
    strmiids
    ${AVCODEC_LIBRARY}
    ${AVFORMAT_LIBRARY}
    ${AVUTIL_LIBRARY}
    ${SWSCALE_LIBRARY}
    ${SWRESAMPLE_LIBRARY}
    ${FFMPEG_EXTRA_LIBS}
)

# Copy FFmpeg DLLs when linking dynamically
if(WIN32 AND NOT USE_STATIC_FFMPEG)
    add_custom_command(TARGET VideoEditor POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${FFMPEG_ROOT}/bin/avcodec-62.dll" "$<TARGET_FILE_DIR:VideoEditor>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${FFMPEG_ROOT}/bin/avformat-62.dll" "$<TARGET_FILE_DIR:VideoEditor>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${FFMPEG_ROOT}/bin/avutil-60.dll" "$<TARGET_FILE_DIR:VideoEditor>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${FFMPEG_ROOT}/bin/swscale-9.dll" "$<TARGET_FILE_DIR:VideoEditor>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${FFMPEG_ROOT}/bin/swresample-6.dll" "$<TARGET_FILE_DIR:VideoEditor>"
        COMMENT "Copying FFmpeg DLLs"
    )
endif()
