cmake_minimum_required(VERSION 3.10)

# Policy for MSVC runtime
if(POLICY CMP0091)
    cmake_policy(SET CMP0091 NEW)
endif()

# Policy for imported targets missing location property
if(POLICY CMP0111)
    cmake_policy(SET CMP0111 NEW)
endif()

project(VideoEditor CXX)

if(USE_STATIC_FFMPEG)
    message(STATUS "🔗 Linking FFmpeg statically")
else()
    message(STATUS "🔗 Linking FFmpeg dynamically")
endif()

# If using MSVC and static FFmpeg, force static runtime and enable LTO
if(MSVC AND USE_STATIC_FFMPEG)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
    # Define CURL_STATICLIB for static libcurl
    add_compile_definitions(CURL_STATICLIB)
elseif(MSVC)
    # For dynamic builds, use dynamic runtime library to avoid conflicts
    # Let CMake use the default dynamic runtime library
    # set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")  # Commented out to use default
endif()

# Require C++17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Qt (frontend)
set(CMAKE_AUTOMOC ON)
# This project does not use Qt Designer `.ui` files. Leaving UIC enabled causes
# collisions with existing headers like `ui_updates.h` (CMake treats `ui_*.h` as
# generated from a matching `.ui`).
set(CMAKE_AUTOUIC OFF)
set(CMAKE_AUTORCC OFF)

if (MSVC)
    # If a MinGW Qt was previously configured in this build directory, it will be
    # cached as Qt*_DIR and break MSVC builds. Clear it so CMake can locate a
    # matching MSVC Qt (if installed).
    if (DEFINED Qt6_DIR AND Qt6_DIR MATCHES "mingw")
        message(STATUS "Clearing cached Qt6_DIR (MinGW): ${Qt6_DIR}")
        unset(Qt6_DIR CACHE)
    endif()
    if (DEFINED Qt5_DIR AND Qt5_DIR MATCHES "mingw")
        message(STATUS "Clearing cached Qt5_DIR (MinGW): ${Qt5_DIR}")
        unset(Qt5_DIR CACHE)
    endif()
endif()

find_package(Qt6 COMPONENTS Widgets QUIET)
if (Qt6_FOUND)
    if (MSVC AND Qt6_DIR MATCHES "mingw")
        message(FATAL_ERROR "Found a MinGW Qt at: ${Qt6_DIR} but you are building with MSVC. Install the MSVC Qt build (e.g. .../msvc2022_64) and set CMAKE_PREFIX_PATH or Qt6_DIR accordingly, or switch to a MinGW generator/toolchain.")
    endif()
    set(VIDEOEDITOR_QT_LIBS Qt6::Widgets)
else()
    find_package(Qt5 COMPONENTS Widgets QUIET)
    if (Qt5_FOUND)
        if (MSVC AND Qt5_DIR MATCHES "mingw")
            message(FATAL_ERROR "Found a MinGW Qt at: ${Qt5_DIR} but you are building with MSVC. Install the MSVC Qt build and set CMAKE_PREFIX_PATH or Qt5_DIR accordingly, or switch to a MinGW generator/toolchain.")
        endif()
        set(VIDEOEDITOR_QT_LIBS Qt5::Widgets)
    else()
        message(FATAL_ERROR "Qt not found. Install Qt (6 or 5) and set CMAKE_PREFIX_PATH (or Qt6_DIR/Qt5_DIR) to the Qt 'lib/cmake' directory. If you are building with MSBuild/MSVC, you must install the MSVC Qt kit (e.g. .../msvc2022_64), not the MinGW kit (e.g. .../mingw_64).")
    endif()
endif()

# Enable UNICODE on Win32
add_compile_definitions(UNICODE _UNICODE)

# Default paths (can override with -DFFMPEG_ROOT=... or -DCURL_ROOT=...)
set(FFMPEG_ROOT "${CMAKE_SOURCE_DIR}/third_party/ffmpeg" CACHE PATH "Path to FFmpeg installation")
set(CURL_ROOT   "${CMAKE_SOURCE_DIR}/vendor/libcurl" CACHE PATH "Path to libcurl installation")
set(RNNOISE_ROOT "${CMAKE_SOURCE_DIR}/vendor/rnnoise" CACHE PATH "Path to RNNoise installation")

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

# ==== FIND RNNoise ====
find_path(RNNOISE_INCLUDE_DIR
    NAMES rnnoise.h
    PATHS ${RNNOISE_ROOT}/include
    NO_DEFAULT_PATH
)
find_library(RNNOISE_LIBRARY NAMES rnnoise PATHS ${RNNOISE_ROOT}/lib NO_DEFAULT_PATH)

message(STATUS ">> RNNoise include: ${RNNOISE_INCLUDE_DIR}")
message(STATUS ">> RNNoise lib:     ${RNNOISE_LIBRARY}")

# ==== IMPORTED TARGET: libcurl ====
add_library(VENDOR_LIBCURL STATIC IMPORTED GLOBAL)
set_target_properties(VENDOR_LIBCURL PROPERTIES
    IMPORTED_LOCATION_DEBUG       "${CURL_ROOT}/lib/libcurl.lib"
    IMPORTED_LOCATION_RELEASE     "${CURL_ROOT}/lib/libcurl.lib"
    IMPORTED_LOCATION_MINSIZEREL  "${CURL_ROOT}/lib/libcurl.lib"
    IMPORTED_LOCATION_RELWITHDEBINFO "${CURL_ROOT}/lib/libcurl.lib"
    INTERFACE_INCLUDE_DIRECTORIES "${CURL_ROOT}/include"
)
if(NOT EXISTS "${CURL_ROOT}/lib/libcurl.lib")
    message(FATAL_ERROR "❌ libcurl.lib does not exist in ${CURL_ROOT}/lib")
endif()
message(STATUS ">> Using vendored libcurl: ${CURL_ROOT}/lib/libcurl.lib")

# ==== IMPORTED TARGET: zlib ====
if(NOT USE_STATIC_FFMPEG)
    add_library(VENDOR_ZLIB STATIC IMPORTED GLOBAL)
    message(STATUS ">> Using vendored zlib: ${CURL_ROOT}/lib/zlib.lib")
    set_target_properties(VENDOR_ZLIB PROPERTIES
        IMPORTED_LOCATION_DEBUG       "${CURL_ROOT}/lib/zlib.lib"
        IMPORTED_LOCATION_RELEASE     "${CURL_ROOT}/lib/zlib.lib"
        IMPORTED_LOCATION_MINSIZEREL  "${CURL_ROOT}/lib/zlib.lib"
        IMPORTED_LOCATION_RELWITHDEBINFO "${CURL_ROOT}/lib/zlib.lib"
    )
    if(NOT EXISTS "${CURL_ROOT}/lib/zlib.lib")
        message(FATAL_ERROR "❌ zlib.lib does not exist in ${CURL_ROOT}/lib")
    endif()
else()
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
    src/qt_main.cpp
    src/qt_mainwindow.cpp
    src/qt_native_key_filter.cpp
    src/qt_timeline_slider.cpp
    src/qt_options_dialog.cpp
    src/qt_progress_dialog.cpp
    src/qt_upload_dialog.cpp
    src/utils.cpp
    src/video_decoder.cpp
    src/audio_player.cpp
    src/video_renderer.cpp
    src/video_cutter.cpp
    src/video_player.cpp
    src/b2_upload.cpp
    src/catbox_upload.cpp
    src/debug_log.cpp
    src/app_settings.cpp
)

# ==== INCLUDE DIRECTORIES ====
target_include_directories(VideoEditor PRIVATE
    src
    ${FFMPEG_INCLUDE_DIR}
    ${RNNOISE_INCLUDE_DIR}
)

# ==== TARGET-SPECIFIC OPTIONS ====
if(MSVC AND USE_STATIC_FFMPEG)
    # Disable specific warnings for static build
    target_compile_options(VideoEditor PRIVATE /wd4996)
    
    # Fix runtime library conflicts with RNNoise and other static libraries
    target_link_options(VideoEditor PRIVATE
        /NODEFAULTLIB:MSVCRTD    # Exclude debug runtime library
        /NODEFAULTLIB:MSVCRT     # Exclude release runtime library
        /NODEFAULTLIB:libcmtd    # Exclude debug static runtime library (if not needed)
    )
    
    # Suppress specific linker warnings for symbol conflicts
    target_link_options(VideoEditor PRIVATE
        /IGNORE:4286  # Ignore duplicate symbol warnings for C runtime functions
        /IGNORE:4098  # Ignore default library conflicts
    )

    # Force load avcodec to ensure hardware encoders (NVENC, AMF) are registered
    # This prevents the linker from stripping "unused" encoder symbols in static builds
    target_link_options(VideoEditor PRIVATE "/WHOLEARCHIVE:${AVCODEC_LIBRARY}")
elseif(MSVC)
    # For dynamic builds, resolve runtime library conflicts
    target_link_options(VideoEditor PRIVATE
        /NODEFAULTLIB:LIBCMT     # Exclude static runtime library
        /NODEFAULTLIB:LIBCMTD    # Exclude static debug runtime library
        /NODEFAULTLIB:MSVCRTD    # Exclude dynamic debug runtime library (from vendor libs)
    )
endif()

# Qt requires MSVC to report a correct `__cplusplus` value.
if(MSVC)
    target_compile_options(VideoEditor PRIVATE /Zc:__cplusplus)
endif()

# ==== WINDOWS PLATFORM LIBS ====
set(PLATFORM_LIBS
    user32 gdi32 d2d1 comctl32 comdlg32 ole32 winmm
    dwmapi uxtheme shell32 ws2_32 bcrypt secur32 mfplat
    mf mfuuid strmiids crypt32 advapi32 iphlpapi wldap32
    normaliz
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
    ${RNNOISE_LIBRARY}
    VENDOR_LIBCURL
    VENDOR_ZLIB
    ${VIDEOEDITOR_QT_LIBS}
)

# Force static runtime library linking for static builds
if(MSVC AND USE_STATIC_FFMPEG)
    target_link_libraries(VideoEditor PRIVATE
        libcmt      # Static release runtime
        libucrt     # Universal C runtime (static)
    )
endif()

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
