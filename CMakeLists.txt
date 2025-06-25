cmake_minimum_required(VERSION 3.10)
project(VideoEditor CXX)

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
add_executable(VideoEditor WIN32 main.cpp video_player.cpp video_player.h)

# Include FFmpeg headers
if(FFMPEG_INCLUDE_DIR)
    target_include_directories(VideoEditor PRIVATE ${FFMPEG_INCLUDE_DIR})
endif()

# Link libraries
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
    ${AVCODEC_LIBRARY}
    ${AVFORMAT_LIBRARY}
    ${AVUTIL_LIBRARY}
    ${SWSCALE_LIBRARY}
    ${SWRESAMPLE_LIBRARY}
)

# Copy FFmpeg DLLs to output directory after build
if(WIN32)
    add_custom_command(TARGET VideoEditor POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${FFMPEG_ROOT}/bin/avcodec-62.dll" "$<TARGET_FILE_DIR:VideoEditor>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${FFMPEG_ROOT}/bin/avformat-62.dll" "$<TARGET_FILE_DIR:VideoEditor>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${FFMPEG_ROOT}/bin/avutil-60.dll" "$<TARGET_FILE_DIR:VideoEditor>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${FFMPEG_ROOT}/bin/swscale-9.dll" "$<TARGET_FILE_DIR:VideoEditor>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${FFMPEG_ROOT}/bin/swresample-6.dll" "$<TARGET_FILE_DIR:VideoEditor>"
        COMMENT "Copying FFmpeg DLLs"
    )
endif()