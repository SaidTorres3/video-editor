# Video Editor with Audio Support

A Windows-based video player application built with C++ and FFmpeg that supports both video playback and multitrack audio functionality.

All source files now reside under the `src/` directory to keep the project organized.

![Video Editor Showcase](https://raw.githubusercontent.com/SaidTorres3/video-editor/refs/heads/master/.readme-assets/showcase-v002.png)

## Features

### Video Playback

- Load and play various video formats (MP4, AVI, MOV, MKV, WMV, FLV, WebM, M4V, 3GP)
- Play, pause, stop controls
- **Spacebar toggles play/pause** for quick control
- **Drag and drop** a video onto the window to load it
- Custom timeline bar for navigation with a red time cursor
- Click anywhere on the timeline to jump directly to that point or hold and drag to scrub through the video in real time. Seeking now lands on the exact frame for smoother editing.
- Keyboard shortcuts for quick navigation (Left/Right arrows skip 5s, J/L skip 10s, K pauses, ',' and '.' step frames)
- Frame-by-frame playback control
- Hardware-accelerated rendering using Direct2D

### Audio Playback (NEW!)

- **Multitrack Audio Support**: Automatically detects and loads all audio tracks from video files
- **Individual Track Control**: Each audio track can be controlled independently
- **Mute/Unmute Tracks**: Toggle audio tracks on/off without affecting other tracks
- **Per-Track Volume Control**: Adjust volume for each audio track individually (0-200%)
- **Master Volume Control**: Control overall audio volume for all tracks
- **Real-time Audio Mixing**: Multiple audio tracks are mixed together in real-time
- **Audio Track Names**: Displays track names from metadata when available

### Audio Controls Interface

- **Audio Tracks List**: Shows all available audio tracks with their names and mute status
- **Mute/Unmute Button**: Toggle mute state for the selected audio track
- **Track Volume Slider**: Adjust volume for the currently selected track (0-200%)
- **Master Volume Slider**: Control overall volume for all audio tracks

### Cutting and Exporting Clips

- **Set Start/End Points**: Mark the portion of the video to export
- **Merge Audio Tracks**: Combine all unmuted tracks into one output stream
- **Codec Options**: Copy video/audio codecs for a fast cut or convert to H.264
- **Bitrate or Target Size**: When converting to H.264 you can either set a bitrate or specify a desired final size; only the chosen option is shown
- **Progress Window**: A small window shows export progress in real time
- **Optional Cloud Upload**: Exported files can be uploaded automatically to Backblaze B2 or catbox.moe and the download URL is shown

## Technical Implementation

### Video Rendering Architecture

- FFmpeg for video decoding
- Direct2D for rendering frames to the window

### Audio Architecture

- Uses Windows Audio Session API (WASAPI) for low-latency audio output
- FFmpeg's libswresample for audio format conversion and resampling
- Multithreaded audio processing with separate audio thread
- Real-time audio mixing with clipping protection
- Support for various audio formats and sample rates

### Audio Processing Pipeline

1. **Stream Detection**: Automatically finds all audio streams in the video file
2. **Codec Initialization**: Sets up decoders for each audio track
3. **Format Conversion**: Converts audio to standard PCM format (44.1kHz, 16-bit stereo)
4. **Volume Processing**: Applies individual track volumes and master volume
5. **Audio Mixing**: Combines multiple tracks with overflow protection
6. **Output**: Streams mixed audio to Windows audio system

## Building the Project

### Prerequisites

- PowerShell 7 or later

The following dependencies will be installed automatically when running the ``run.ps1`` script:

- Visual Studio 2022 or later with C++ tools
- CMake 3.10 or later
- FFmpeg development libraries ([https://github.com/BtbN/FFmpeg-Builds/releases/download/latest/ffmpeg-master-latest-win64-gpl-shared.zip](https://github.com/BtbN/FFmpeg-Builds/releases/download/latest/ffmpeg-master-latest-win64-gpl-shared.zip))

If you prefer to install them manually, the script will detect and use your existing installations.

The -static build option requires vcpkg to be installed and the FFmpeg and curl libraries to be installed, which are not being installed by this script.

### Build Steps

---

#### 1. Using prebuilt shared FFmpeg binaries (default)

1. Open a windows terminal in the project directory and run the following command using **PowerShell 7 or later**:
   ```powershell
   pwsh .\run.ps1
   ```
2. Accept the installation of all missing dependencies the script will prompt for.
3. If the script fails or stops for any reason, simply run it again.

The script will handle all dependencies automatically. You'll end up with a lightweight  `VideoEditor.exe` and all required DLLs in the output folder—no extra setup needed.

---

#### 2. Building a portable executable with static FFmpeg

1. Make sure you have installed the [vcpkg](https://github.com/Microsoft/vcpkg) package manager and installed in `C:/tools/vcpkg/`
2. Install FFmpeg and curl via vcpkg using the static triplet.  You may enable
   additional codec features if desired:
   ```
   vcpkg install ffmpeg[dav1d,openh264,x264,x265,mp3lame,fdk-aac,opus,zlib,ffmpeg]:x64-windows-static
   vcpkg install curl[core,sspi,ssl,schannel,non-http]:x64-windows-static
   ```
3. Run with powershell 7 or later:

```powershell
   pwsh .\run.ps1 -Static
```

   The resulting `VideoEditor.exe` no longer requires FFmpeg DLLs.

---

The `CutVideo` feature uses the FFmpeg libraries directly, so no external `ffmpeg` executable is needed.
When cutting you can optionally re-encode the video to H.264 and merge all active audio tracks; these operations are handled internally by FFmpeg.
You may enable both options at the same time to shrink the file and maximize compatibility.

Link-time optimization is automatically enabled when `USE_STATIC_FFMPEG` is
used to keep performance similar to the dynamic build.

When `-Static` is used the script looks for FFmpeg in
`C:\tools\vcpkg\installed\x64-windows-static` unless another path is passed
via `-FFmpegPath`.

If the static libraries are not found, or the path contains the regular DLL
distribution of FFmpeg, the script will abort with an error. Install the
`ffmpeg:x64-windows-static` package with vcpkg or provide the correct location
using `-FFmpegPath`.

### FFmpeg Libraries Required

- avcodec (video/audio decoding)
- avformat (container format handling)
- avutil (utility functions)
- swscale (video scaling)
- swresample (audio resampling) - **NEW for audio support**

## Usage

### Loading Videos with Audio

1. Click "Open Video" to select a video file
2. The application will automatically detect and list all audio tracks
3. Audio tracks appear in the "Audio Tracks" list on the right side

### Controlling Audio Tracks

1. **Select Track**: Click on an audio track in the list to select it
2. **Mute/Unmute**: Use the "Mute/Unmute" button to toggle the selected track
3. **Track Volume**: Adjust the "Track Volume" slider for the selected track
4. **Master Volume**: Use the "Master Volume" slider to control overall audio level

### Upload Settings

Open **Options** and click **Upload Settings** to configure cloud uploads. The window lets you enable *Auto upload after export* and open **Catbox Settings** or **Backblaze B2 Settings**. Each provider has its own window with an enable checkbox and credentials. When an upload succeeds a small dialog shows the URL so you can easily copy it to the clipboard. Make sure to paste your Catbox user hash without extra spaces.

### Audio Track Features

- **Multiple Tracks**: All audio tracks play simultaneously by default
- **Individual Control**: Each track can be muted or have its volume adjusted independently
- **Volume Range**: 0-200% volume control (0% = silent, 100% = original, 200% = amplified)
- **Real-time Changes**: All audio adjustments take effect immediately during playback

## Audio Track Display

- Track names are shown in the format: "Audio Track N" or the actual track name from metadata
- Muted tracks are indicated with "(MUTED)" in the track list
- The currently selected track is highlighted in the list

## Supported Audio Formats

The application supports all audio formats that FFmpeg can decode, including:

- AAC, MP3, AC3, DTS, PCM, FLAC, Vorbis, and many others
- Multiple audio tracks in containers like MKV, MP4, AVI
- Various sample rates and channel configurations

## Performance Notes

- Audio processing runs in a separate thread to avoid blocking video playback
- Low-latency audio output using WASAPI shared mode
- Efficient audio mixing with minimal CPU overhead
- Automatic format conversion handles different audio specifications

### Cloud Upload

Configure one or both providers under **Options > Upload Settings**. Enter your Backblaze B2 credentials or Catbox user hash in their respective dialogs. When `Auto upload after export` is enabled, the exported video is uploaded to the selected provider and the download URL is shown in a copyable dialog. Uploads without a Catbox user hash are anonymous and will not appear in your account.

## Troubleshooting

### No Audio Output

- Check Windows audio settings and default playback device
- Ensure FFmpeg DLLs are in the application directory
- Verify the video file contains audio tracks

### Audio Stuttering

- Try adjusting the audio buffer size in the code
- Check system audio latency settings
- Ensure sufficient CPU resources for real-time processing

### Debug Logs

All diagnostic output from the application is written to `debug.log` in the
working directory. Critical errors will also be shown in popup windows during
export operations.

## Future Enhancements

- Audio effects and filters
- Audio track synchronization controls
- Export audio tracks separately
- Audio waveform visualization
- Surround sound support

## License

This project is licensed under the GNU General Public License v3.0.
