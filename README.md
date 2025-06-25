# Video Editor with Audio Support

A Windows-based video player application built with C++ and FFmpeg that supports both video playback and multitrack audio functionality.

## Features

### Video Playback
- Load and play various video formats (MP4, AVI, MOV, MKV, WMV, FLV, WebM, M4V, 3GP)
- Play, pause, stop controls
- **Spacebar toggles play/pause** for quick control
- **Drag and drop** a video onto the window to load it
- Seek bar for navigation
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
- Visual Studio 2019 or later
- CMake 3.10 or later
- FFmpeg development libraries

### Build Steps
1. Extract FFmpeg to `C:/Program Files/ffmpeg` (or update CMakeLists.txt path)
2. Create build directory: `mkdir build && cd build`
3. Generate project: `cmake ..`
4. Build: `cmake --build . --config Release`

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

## Troubleshooting

### No Audio Output
- Check Windows audio settings and default playback device
- Ensure FFmpeg DLLs are in the application directory
- Verify the video file contains audio tracks

### Audio Stuttering
- Try adjusting the audio buffer size in the code
- Check system audio latency settings
- Ensure sufficient CPU resources for real-time processing

## Future Enhancements
- Audio effects and filters
- Audio track synchronization controls
- Export audio tracks separately
- Audio waveform visualization
- Surround sound support