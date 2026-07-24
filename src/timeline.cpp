#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "timeline.h"
#include "video_player.h"
#include "ui_updates.h"
#include "ui_controls.h"
#include "options_window.h"
#include "editing.h"
#include "ten_vad_embedded.h"
#include <windowsx.h>
#include <algorithm>
#include <cmath>
#include <climits>
#include <limits>

// Forward declarations
void UpdateControls();
void UpdateCutInfoLabel(HWND hwnd);
void UpdateCutTimeEdits();

// Global variables
extern VideoPlayer *g_videoPlayer;
extern HWND g_hTimeline;
extern double g_cutStartTime, g_cutEndTime;
extern bool g_isTimelineDragging;
extern bool g_wasPlayingBeforeDrag;
extern bool g_resumePlayAfterSeek;
enum class DragMode { None, Cursor, StartMarker, EndMarker, Keyframe };
extern DragMode g_timelineDragMode;
extern double g_draggedKeyframeTime;
double g_previewSeekTime = -1.0; // For immediate timeline feedback

// Hover tooltip tracking
static int g_timelineHoverX = -1;
static bool g_timelineMouseTracking = false;
static constexpr int TIMELINE_MIN_HEIGHT = 30;
static constexpr int TIMELINE_DEFAULT_HEIGHT = 50;
static int g_preferredTimelineHeight = TIMELINE_DEFAULT_HEIGHT;
static bool g_isTimelineHeightDragging = false;
static int g_timelineResizeStartScreenY = 0;
static int g_timelineResizeStartHeight = TIMELINE_DEFAULT_HEIGHT;
static HWND g_timecodeTooltipWnd = nullptr;

int GetPreferredTimelineHeight()
{
    return g_preferredTimelineHeight;
}

LRESULT CALLBACK TimelineResizeBarProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_SETCURSOR:
        SetCursor(LoadCursor(nullptr, IDC_SIZENS));
        return TRUE;
    case WM_LBUTTONDOWN:
    {
        POINT pt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        ClientToScreen(hwnd, &pt);
        g_isTimelineHeightDragging = true;
        g_timelineResizeStartScreenY = pt.y;
        g_timelineResizeStartHeight = g_preferredTimelineHeight;
        SetCapture(hwnd);
        if (g_timecodeTooltipWnd)
            ShowWindow(g_timecodeTooltipWnd, SW_HIDE);
        return 0;
    }
    case WM_MOUSEMOVE:
        if (g_isTimelineHeightDragging)
        {
            POINT pt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            ClientToScreen(hwnd, &pt);
            RECT parentRect{};
            GetClientRect(GetParent(hwnd), &parentRect);
            const int maximumHeight = (std::max)(
                TIMELINE_MIN_HEIGHT, static_cast<int>(parentRect.bottom) - 220);
            const int requestedHeight = g_timelineResizeStartHeight
                + g_timelineResizeStartScreenY - static_cast<int>(pt.y);
            g_preferredTimelineHeight = std::clamp(
                requestedHeight, TIMELINE_MIN_HEIGHT, maximumHeight);
            RepositionTimelineArea(GetParent(hwnd));
        }
        return 0;
    case WM_LBUTTONUP:
        if (g_isTimelineHeightDragging)
        {
            g_isTimelineHeightDragging = false;
            if (GetCapture() == hwnd)
                ReleaseCapture();
        }
        return 0;
    case WM_CAPTURECHANGED:
        g_isTimelineHeightDragging = false;
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);
        HBRUSH background = CreateSolidBrush(RGB(45, 45, 48));
        FillRect(hdc, &rc, background);
        DeleteObject(background);

        const int centerY = rc.bottom / 2;
        const int centerX = static_cast<int>(rc.right) / 2;
        const int gripLeft = (std::max)(0, centerX - 28);
        const int gripRight = (std::min)(static_cast<int>(rc.right), centerX + 28);
        HPEN linePen = CreatePen(PS_SOLID, 1, RGB(92, 92, 96));
        HGDIOBJ oldPen = SelectObject(hdc, linePen);
        MoveToEx(hdc, gripLeft, centerY, nullptr);
        LineTo(hdc, gripRight, centerY);
        SelectObject(hdc, oldPen);
        DeleteObject(linePen);
        EndPaint(hwnd, &ps);
        return 0;
    }
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

// Timecode tooltip popup window
static wchar_t g_timecodeTooltipText[32] = {};
static const wchar_t* TIMECODE_TOOLTIP_CLASS = L"TimelineTimecodeTooltip";

// --- Thumbnail frame preview globals ---
static double g_timelineHoverTime = -1.0; // Quantized hover time for thumbnail lookup

// Thumbnail popup dimensions
static const int THUMB_W          = 160;
static const int THUMB_MAX_H      = 90;
static const int THUMB_TIMECODE_H = 22;

struct ThumbnailData
{
    double               time   = -1.0;
    int                  width  = 0;
    int                  height = 0;
    std::vector<uint8_t> pixels; // BGRA, width*height*4 bytes
};

static std::mutex                 g_thumbCacheMutex;
static std::vector<ThumbnailData> g_thumbCache;
static const int                  THUMB_CACHE_SIZE = 250;

static std::atomic<double>        g_thumbRequestTime{-1.0};
static std::atomic<bool>          g_thumbThreadExit{false};
static HANDLE                     g_thumbRequestEvent = nullptr;
static std::thread                g_thumbThread;

// --- Audio waveform preview globals ---
// A fixed-size envelope is enough to render cleanly at any normal window width,
// while keeping the cache and repaint cost very small.
static const int                  WAVEFORM_BIN_COUNT = 4096;
static const UINT                 WM_AUDIO_WAVEFORM_READY = WM_APP + 20;
struct AudioWaveformTrack
{
    int streamIndex = -1;
    std::vector<float> samples;
    // One entry per waveform bin. A non-zero value means the lightweight
    // neural VAD found a temporally stable speech segment around that point.
    std::vector<uint8_t> probableSpeech;
};
static std::mutex                 g_waveformCacheMutex;
static std::vector<AudioWaveformTrack> g_waveformCache;
static std::mutex                 g_waveformRequestMutex;
static std::wstring               g_waveformRequestFile;
static double                     g_waveformRequestDuration = 0.0;
static std::uint64_t              g_waveformRequestGeneration = 0;
static bool                       g_waveformRequestHighlightSpeech = false;
static std::atomic<std::uint64_t> g_waveformGeneration{0};
static std::atomic<bool>          g_waveformThreadExit{false};
static std::atomic<int>           g_waveformProgress{-1};
static HANDLE                     g_waveformRequestEvent = nullptr;
static std::thread                g_waveformThread;

int GetAudioWaveformProgress()
{
    return g_waveformProgress.load();
}

// Pre-cache queue: populated when a video is loaded.
static std::mutex              g_preCacheMutex;
static std::vector<double>     g_preCacheTimes;

// Public: call after a new video loads to queue up eager pre-cache work.
void TriggerThumbnailPreCache(double duration)
{
    if (duration <= 0.0) return;
    // Compute an interval so we generate at most 48 thumbnails, min 0.5 s apart.
    const int MAX_THUMBS = 200;
    double interval = duration / MAX_THUMBS;
    if (interval < 0.25) interval = 0.25;

    {
        std::lock_guard<std::mutex> lck(g_preCacheMutex);
        g_preCacheTimes.clear();
        for (double t = 0.0; t < duration; t += interval)
            g_preCacheTimes.push_back(t);
        // Also clear old cache since it belongs to a different video.
        std::lock_guard<std::mutex> lck2(g_thumbCacheMutex);
        g_thumbCache.clear();
    }
    if (g_thumbRequestEvent)
        SetEvent(g_thumbRequestEvent);
}

static void ThumbnailThreadFunc()
{
    while (!g_thumbThreadExit.load())
    {
        if (WaitForSingleObject(g_thumbRequestEvent, 200) == WAIT_OBJECT_0)
            ResetEvent(g_thumbRequestEvent);
        if (g_thumbThreadExit.load()) break;

        // Determine what to decode: priority = hover request, fallback = next pre-cache slot.
        double reqTime = g_thumbRequestTime.exchange(-1.0);
        bool isPreCache = false;
        if (reqTime < 0.0) {
            std::lock_guard<std::mutex> lck(g_preCacheMutex);
            if (!g_preCacheTimes.empty()) {
                reqTime = g_preCacheTimes.front();
                g_preCacheTimes.erase(g_preCacheTimes.begin());
                isPreCache = true;
            }
        }
        if (reqTime < 0.0) continue;

        // Skip if already in cache
        {
            std::lock_guard<std::mutex> lck(g_thumbCacheMutex);
            bool found = false;
            for (const auto& e : g_thumbCache)
                if (std::fabs(e.time - reqTime) < 0.3) { found = true; break; }
            if (found) {
                // Signal again to drain pre-cache queue
                if (isPreCache && g_thumbRequestEvent) SetEvent(g_thumbRequestEvent);
                continue;
            }
        }

        if (!g_videoPlayer || !g_videoPlayer->IsLoaded()) continue;

        int fw = g_videoPlayer->frameWidth;
        int fh = g_videoPlayer->frameHeight;
        int tw = THUMB_W, th = THUMB_MAX_H;
        if (fw > 0 && fh > 0)
        {
            th = (int)((double)THUMB_W * fh / fw);
            if (th > THUMB_MAX_H) { th = THUMB_MAX_H; tw = (int)((double)THUMB_MAX_H * fw / fh); }
            if (tw < 1) tw = 1;
            if (th < 1) th = 1;
        }

        ThumbnailData td;
        td.time   = reqTime;
        td.width  = tw;
        td.height = th;
        // Use fast (persistent decoder) path; fall back to slow path if not ready yet.
        bool ok = g_videoPlayer->GetThumbnailPixelsFast(reqTime, tw, th, td.pixels);
        if (!ok)
            ok = g_videoPlayer->GetThumbnailPixels(reqTime, tw, th, td.pixels);
        if (ok)
        {
            {
                std::lock_guard<std::mutex> lck(g_thumbCacheMutex);
                if (g_thumbCache.size() >= THUMB_CACHE_SIZE)
                    g_thumbCache.erase(g_thumbCache.begin());
                g_thumbCache.push_back(std::move(td));
            }
            // Always repaint: pre-cached frames appear immediately on hover
            if (g_timecodeTooltipWnd)
                InvalidateRect(g_timecodeTooltipWnd, nullptr, FALSE);
        }
        // Keep draining pre-cache queue
        if (isPreCache && g_thumbRequestEvent)
            SetEvent(g_thumbRequestEvent);
    }
}

static double ReadNormalizedAudioSample(const AVFrame* frame, int channel, int sample)
{
    if (!frame || !frame->extended_data)
        return 0.0;

    const AVSampleFormat format = static_cast<AVSampleFormat>(frame->format);
    const AVSampleFormat packedFormat = av_get_packed_sample_fmt(format);
    const bool planar = av_sample_fmt_is_planar(format) != 0;
    const int channels = std::max(1, frame->ch_layout.nb_channels);
    const int plane = planar ? channel : 0;
    const int index = planar ? sample : (sample * channels + channel);
    const uint8_t* data = frame->extended_data[plane];
    if (!data)
        return 0.0;

    switch (packedFormat)
    {
    case AV_SAMPLE_FMT_U8:
        return (static_cast<double>(reinterpret_cast<const uint8_t*>(data)[index]) - 128.0) / 128.0;
    case AV_SAMPLE_FMT_S16:
        return static_cast<double>(reinterpret_cast<const int16_t*>(data)[index]) / 32768.0;
    case AV_SAMPLE_FMT_S32:
        return static_cast<double>(reinterpret_cast<const int32_t*>(data)[index]) / 2147483648.0;
    case AV_SAMPLE_FMT_FLT:
        return static_cast<double>(reinterpret_cast<const float*>(data)[index]);
    case AV_SAMPLE_FMT_DBL:
        return reinterpret_cast<const double*>(data)[index];
    case AV_SAMPLE_FMT_S64:
        return static_cast<double>(reinterpret_cast<const int64_t*>(data)[index]) / 9223372036854775808.0;
    default:
        return 0.0;
    }
}

static bool BuildAudioWaveforms(const std::wstring& filename, double duration,
                                std::uint64_t generation,
                                bool highlightSpeech,
                                std::vector<AudioWaveformTrack>& result)
{
    if (filename.empty() || duration <= 0.0)
        return false;

    int utf8Size = WideCharToMultiByte(CP_UTF8, 0, filename.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (utf8Size <= 0)
        return false;
    std::string utf8Filename(static_cast<size_t>(utf8Size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, filename.c_str(), -1,
                        utf8Filename.data(), utf8Size, nullptr, nullptr);

    AVFormatContext* formatContext = nullptr;
    if (avformat_open_input(&formatContext, utf8Filename.c_str(), nullptr, nullptr) < 0)
        return false;
    if (avformat_find_stream_info(formatContext, nullptr) < 0)
    {
        avformat_close_input(&formatContext);
        return false;
    }

    struct WaveformDecoder
    {
        struct VadObservation
        {
            float time = 0.0f;
            float probability = 0.0f;
        };

        int streamIndex = -1;
        AVCodecContext* codecContext = nullptr;
        double nextTime = 0.0;
        std::vector<double> energy;
        std::vector<std::uint64_t> sampleCounts;
        EmbeddedTenVadHandle vad = nullptr;
        SwrContext* vadResampler = nullptr;
        std::vector<int16_t> vadResampleBuffer;
        std::vector<int16_t> vadSampleQueue;
        size_t vadSampleOffset = 0;
        std::vector<VadObservation> vadObservations;
        double nextVadTime = 0.0;
        bool hasVadTime = false;
    };
    std::vector<WaveformDecoder> decoders;

    double startTimeOffset = 0.0;
    double minStart = std::numeric_limits<double>::max();
    for (unsigned i = 0; i < formatContext->nb_streams; ++i)
    {
        AVStream* stream = formatContext->streams[i];
        if (stream->start_time != AV_NOPTS_VALUE)
            minStart = std::min(minStart, stream->start_time * av_q2d(stream->time_base));

        if (stream->codecpar->codec_type != AVMEDIA_TYPE_AUDIO)
            continue;

        const AVCodec* codec = avcodec_find_decoder(stream->codecpar->codec_id);
        if (!codec)
            continue;

        AVCodecContext* codecContext = avcodec_alloc_context3(codec);
        if (!codecContext)
            continue;
        if (avcodec_parameters_to_context(codecContext, stream->codecpar) < 0 ||
            avcodec_open2(codecContext, codec, nullptr) < 0)
        {
            avcodec_free_context(&codecContext);
            continue;
        }

        WaveformDecoder decoder;
        decoder.streamIndex = static_cast<int>(i);
        decoder.codecContext = codecContext;
        decoder.energy.assign(WAVEFORM_BIN_COUNT, 0.0);
        decoder.sampleCounts.assign(WAVEFORM_BIN_COUNT, 0);
        // TEN VAD is a small neural detector optimized for 16 kHz / 256-sample
        // frames. A high threshold deliberately favors precision here: this is
        // a visual hint, so missing a marginal syllable is preferable to
        // coloring sound effects as speech.
        if (highlightSpeech &&
            !EmbeddedTenVadCreate(&decoder.vad, 256, 0.70f))
        {
            decoder.vad = nullptr;
        }
        decoders.push_back(decoder);
    }
    if (minStart != std::numeric_limits<double>::max())
        startTimeOffset = minStart;

    if (decoders.empty())
    {
        avformat_close_input(&formatContext);
        return false;
    }

    AVPacket* packet = av_packet_alloc();
    AVFrame* audioFrame = av_frame_alloc();
    bool cancelled = !packet || !audioFrame;
    double maxProcessedTime = 0.0;

    auto consumeFrames = [&](WaveformDecoder& decoder, AVPacket* inputPacket)
    {
        if (avcodec_send_packet(decoder.codecContext, inputPacket) < 0)
            return;

        while (!cancelled && avcodec_receive_frame(decoder.codecContext, audioFrame) == 0)
        {
            if (g_waveformThreadExit.load() || g_waveformGeneration.load() != generation)
            {
                cancelled = true;
                break;
            }

            AVStream* stream = formatContext->streams[decoder.streamIndex];
            int sampleRate = audioFrame->sample_rate > 0
                                 ? audioFrame->sample_rate
                                 : decoder.codecContext->sample_rate;
            int channels = std::max(1, audioFrame->ch_layout.nb_channels);
            if (sampleRate <= 0 || audioFrame->nb_samples <= 0)
            {
                av_frame_unref(audioFrame);
                continue;
            }

            int64_t timestamp = audioFrame->best_effort_timestamp;
            if (timestamp == AV_NOPTS_VALUE)
                timestamp = audioFrame->pts;
            double frameStart = timestamp == AV_NOPTS_VALUE
                                    ? decoder.nextTime
                                    : timestamp * av_q2d(stream->time_base) - startTimeOffset;
            decoder.nextTime = frameStart + audioFrame->nb_samples / static_cast<double>(sampleRate);
            if (decoder.nextTime > maxProcessedTime && duration > 0.0)
            {
                maxProcessedTime = decoder.nextTime;
                if (g_waveformGeneration.load() == generation)
                {
                    int pct = std::clamp(static_cast<int>((maxProcessedTime / duration) * 100.0), 0, 99);
                    g_waveformProgress.store(pct);
                }
            }

            // Neural speech detection runs in the waveform decoding pass, so
            // it adds no second read/decode of the media.
            if (decoder.vad)
            {
                if (!decoder.vadResampler)
                {
                    AVChannelLayout inputLayout = audioFrame->ch_layout;
                    if (inputLayout.nb_channels <= 0)
                        av_channel_layout_default(&inputLayout, channels);
                    AVChannelLayout monoLayout;
                    av_channel_layout_default(&monoLayout, 1);

                    decoder.vadResampler = swr_alloc();
                    if (decoder.vadResampler)
                    {
                        av_opt_set_chlayout(decoder.vadResampler, "in_chlayout",
                                           &inputLayout, 0);
                        av_opt_set_chlayout(decoder.vadResampler, "out_chlayout",
                                           &monoLayout, 0);
                        av_opt_set_int(decoder.vadResampler, "in_sample_rate",
                                      sampleRate, 0);
                        av_opt_set_int(decoder.vadResampler, "out_sample_rate",
                                      16000, 0);
                        av_opt_set_sample_fmt(
                            decoder.vadResampler, "in_sample_fmt",
                            static_cast<AVSampleFormat>(audioFrame->format), 0);
                        av_opt_set_sample_fmt(decoder.vadResampler,
                                              "out_sample_fmt",
                                              AV_SAMPLE_FMT_S16, 0);
                        if (swr_init(decoder.vadResampler) < 0)
                            swr_free(&decoder.vadResampler);
                    }
                    av_channel_layout_uninit(&monoLayout);
                }

                if (decoder.vadResampler)
                {
                    if (!decoder.hasVadTime ||
                        std::fabs(frameStart - decoder.nextVadTime) > 0.15)
                    {
                        decoder.vadSampleQueue.clear();
                        decoder.vadSampleOffset = 0;
                        decoder.nextVadTime = frameStart;
                        decoder.hasVadTime = true;
                    }

                    int outputCapacity = swr_get_out_samples(
                        decoder.vadResampler, audioFrame->nb_samples);
                    if (outputCapacity > 0)
                    {
                        decoder.vadResampleBuffer.resize(
                            static_cast<size_t>(outputCapacity));
                        int16_t* output = decoder.vadResampleBuffer.data();
                        int converted = swr_convert(
                            decoder.vadResampler,
                            reinterpret_cast<uint8_t**>(&output),
                            outputCapacity,
                            const_cast<const uint8_t**>(
                                audioFrame->extended_data),
                            audioFrame->nb_samples);
                        if (converted > 0)
                        {
                            decoder.vadSampleQueue.insert(
                                decoder.vadSampleQueue.end(), output,
                                output + converted);
                        }

                        constexpr int VAD_FRAME_SAMPLES = 256;
                        constexpr double VAD_FRAME_SECONDS = 0.016;
                        while (decoder.vadSampleQueue.size() -
                                   decoder.vadSampleOffset >=
                               VAD_FRAME_SAMPLES)
                        {
                            float probability = 0.0f;
                            int decision = 0;
                            if (EmbeddedTenVadProcess(
                                    decoder.vad,
                                    decoder.vadSampleQueue.data() +
                                        decoder.vadSampleOffset,
                                    VAD_FRAME_SAMPLES, &probability,
                                    &decision))
                            {
                                WaveformDecoder::VadObservation observation;
                                observation.time =
                                    static_cast<float>(decoder.nextVadTime);
                                observation.probability =
                                    std::clamp(probability, 0.0f, 1.0f);
                                decoder.vadObservations.push_back(observation);
                            }
                            decoder.vadSampleOffset += VAD_FRAME_SAMPLES;
                            decoder.nextVadTime += VAD_FRAME_SECONDS;
                        }
                        if (decoder.vadSampleOffset ==
                            decoder.vadSampleQueue.size())
                        {
                            decoder.vadSampleQueue.clear();
                            decoder.vadSampleOffset = 0;
                        }
                        else if (decoder.vadSampleOffset >= 8192)
                        {
                            decoder.vadSampleQueue.erase(
                                decoder.vadSampleQueue.begin(),
                                decoder.vadSampleQueue.begin() +
                                    decoder.vadSampleOffset);
                            decoder.vadSampleOffset = 0;
                        }
                    }
                }
            }

            // About 48 measurements per bin preserve transients without doing
            // unnecessary per-sample work on long recordings.
            int stride = std::max(1, static_cast<int>(
                (duration * sampleRate) / (WAVEFORM_BIN_COUNT * 48.0)));
            for (int sample = 0; sample < audioFrame->nb_samples; sample += stride)
            {
                double time = frameStart + sample / static_cast<double>(sampleRate);
                if (time < 0.0 || time >= duration)
                    continue;

                double channelEnergy = 0.0;
                for (int channel = 0; channel < channels; ++channel)
                {
                    double value = ReadNormalizedAudioSample(audioFrame, channel, sample);
                    channelEnergy += value * value;
                }
                channelEnergy /= channels;

                int bin = std::clamp(static_cast<int>(
                    time * WAVEFORM_BIN_COUNT / duration), 0, WAVEFORM_BIN_COUNT - 1);
                decoder.energy[bin] += channelEnergy;
                decoder.sampleCounts[bin]++;
            }
            av_frame_unref(audioFrame);
        }
    };

    while (!cancelled && av_read_frame(formatContext, packet) >= 0)
    {
        if (g_waveformThreadExit.load() || g_waveformGeneration.load() != generation)
        {
            cancelled = true;
            av_packet_unref(packet);
            break;
        }

        if (packet->pts != AV_NOPTS_VALUE && duration > 0.0 && packet->stream_index < static_cast<int>(formatContext->nb_streams))
        {
            double packetTime = packet->pts * av_q2d(formatContext->streams[packet->stream_index]->time_base) - startTimeOffset;
            if (packetTime > maxProcessedTime && packetTime <= duration)
            {
                maxProcessedTime = packetTime;
                if (g_waveformGeneration.load() == generation)
                {
                    int pct = std::clamp(static_cast<int>((maxProcessedTime / duration) * 100.0), 0, 99);
                    g_waveformProgress.store(pct);
                }
            }
        }

        for (auto& decoder : decoders)
        {
            if (decoder.streamIndex == packet->stream_index)
            {
                consumeFrames(decoder, packet);
                break;
            }
        }
        av_packet_unref(packet);
    }

    if (!cancelled)
    {
        for (auto& decoder : decoders)
            consumeFrames(decoder, nullptr);
        if (g_waveformGeneration.load() == generation)
        {
            g_waveformProgress.store(100);
        }
    }

    if (cancelled)
    {
        av_frame_free(&audioFrame);
        av_packet_free(&packet);
        for (auto& decoder : decoders)
        {
            swr_free(&decoder.vadResampler);
            if (decoder.vad)
                EmbeddedTenVadDestroy(&decoder.vad);
            avcodec_free_context(&decoder.codecContext);
        }
        avformat_close_input(&formatContext);
        return false;
    }

    result.clear();
    result.reserve(decoders.size());
    for (auto& decoder : decoders)
    {
        AudioWaveformTrack track;
        track.streamIndex = decoder.streamIndex;
        track.samples.assign(WAVEFORM_BIN_COUNT, 0.0f);
        track.probableSpeech.assign(WAVEFORM_BIN_COUNT, 0);

        std::vector<float> nonSilent;
        nonSilent.reserve(WAVEFORM_BIN_COUNT);
        for (int i = 0; i < WAVEFORM_BIN_COUNT; ++i)
        {
            if (decoder.sampleCounts[i] == 0)
                continue;
            track.samples[i] = static_cast<float>(
                std::sqrt(decoder.energy[i] / decoder.sampleCounts[i]));
            if (track.samples[i] > 0.00001f)
                nonSilent.push_back(track.samples[i]);
        }

        if (!nonSilent.empty())
        {
            std::sort(nonSilent.begin(), nonSilent.end());
            size_t percentileIndex = static_cast<size_t>((nonSilent.size() - 1) * 0.95);
            float referenceLevel = std::max(0.00001f, nonSilent[percentileIndex]);
            for (float& value : track.samples)
            {
                float normalized = std::min(1.0f, value / (referenceLevel * 1.15f));
                value = std::sqrt(normalized);
            }

            // A short smoothing pass keeps compact, one-pixel traces readable.
            std::vector<float> smoothed(track.samples.size(), 0.0f);
            for (size_t i = 0; i < track.samples.size(); ++i)
            {
                float previous = i > 0 ? track.samples[i - 1] : track.samples[i];
                float next = i + 1 < track.samples.size() ? track.samples[i + 1] : track.samples[i];
                smoothed[i] = previous * 0.25f + track.samples[i] * 0.5f + next * 0.25f;
            }
            track.samples.swap(smoothed);
        }

        if (!nonSilent.empty() && !decoder.vadObservations.empty())
        {
            // Smooth only 80 ms of probabilities. A click can produce a high
            // single-frame score, but it cannot dominate this short weighted
            // neighborhood as sustained speech can.
            constexpr double VAD_FRAME_SECONDS = 0.016;
            constexpr double MAX_CONTIGUOUS_STEP = 0.05;
            constexpr float SPEECH_PROBABILITY = 0.70f;
            const auto& observations = decoder.vadObservations;
            std::vector<uint8_t> speech(observations.size(), 0);

            for (size_t i = 0; i < observations.size(); ++i)
            {
                double weightedProbability = 0.0;
                double totalWeight = 0.0;
                const size_t first = i > 2 ? i - 2 : 0;
                const size_t last =
                    std::min(observations.size() - 1, i + 2);
                for (size_t j = first; j <= last; ++j)
                {
                    if (std::fabs(observations[j].time -
                                  observations[i].time) >
                        MAX_CONTIGUOUS_STEP * 2.0)
                        continue;
                    const size_t distance = i > j ? i - j : j - i;
                    const double weight =
                        3.0 - static_cast<double>(distance);
                    weightedProbability +=
                        observations[j].probability * weight;
                    totalWeight += weight;
                }
                speech[i] =
                    totalWeight > 0.0 &&
                    weightedProbability / totalWeight >= SPEECH_PROBABILITY;
            }
            const std::vector<uint8_t> directSpeech = speech;

            // Bridge pauses up to 112 ms inside otherwise continuous speech.
            // Timestamp checks prevent joining across seeks or stream gaps.
            constexpr double MAXIMUM_SPEECH_GAP = 0.112;
            for (size_t start = 1; start + 1 < speech.size();)
            {
                if (speech[start])
                {
                    ++start;
                    continue;
                }
                size_t end = start;
                while (end < speech.size() && !speech[end])
                    ++end;
                if (speech[start - 1] && end < speech.size() &&
                    observations[end].time - observations[start - 1].time <=
                        MAXIMUM_SPEECH_GAP + VAD_FRAME_SECONDS)
                {
                    std::fill(speech.begin() + start, speech.begin() + end, 1);
                }
                start = end;
            }

            // Require both a 320 ms segment and at least 208 ms of direct
            // neural evidence. This is independent of timeline/bin resolution,
            // so a transient cannot become "speech" merely because the video
            // is long and each visible bin spans several seconds.
            constexpr double MINIMUM_SEGMENT_TIME = 0.320;
            constexpr double MINIMUM_DIRECT_SPEECH_TIME = 0.208;
            constexpr double MINIMUM_DIRECT_RATIO = 0.55;
            for (size_t start = 0; start < speech.size();)
            {
                if (!speech[start])
                {
                    ++start;
                    continue;
                }

                size_t end = start + 1;
                while (end < speech.size() && speech[end] &&
                       observations[end].time -
                               observations[end - 1].time <=
                           MAX_CONTIGUOUS_STEP)
                    ++end;

                size_t directFrames = 0;
                for (size_t i = start; i < end; ++i)
                    directFrames += directSpeech[i] ? 1 : 0;
                const double segmentTime =
                    observations[end - 1].time - observations[start].time +
                    VAD_FRAME_SECONDS;
                const double directTime =
                    directFrames * VAD_FRAME_SECONDS;
                const double directRatio =
                    directFrames / static_cast<double>(end - start);

                if (segmentTime >= MINIMUM_SEGMENT_TIME &&
                    directTime >= MINIMUM_DIRECT_SPEECH_TIME &&
                    directRatio >= MINIMUM_DIRECT_RATIO)
                {
                    for (size_t i = start; i < end; ++i)
                    {
                        const double frameStart = observations[i].time;
                        const double frameEnd =
                            frameStart + VAD_FRAME_SECONDS;
                        if (frameEnd <= 0.0 || frameStart >= duration)
                            continue;
                        const int firstBin = std::clamp(static_cast<int>(
                            frameStart * WAVEFORM_BIN_COUNT / duration),
                            0, WAVEFORM_BIN_COUNT - 1);
                        const int lastBin = std::clamp(static_cast<int>(
                            frameEnd * WAVEFORM_BIN_COUNT / duration),
                            firstBin, WAVEFORM_BIN_COUNT - 1);
                        std::fill(track.probableSpeech.begin() + firstBin,
                                  track.probableSpeech.begin() + lastBin + 1,
                                  1);
                    }
                }
                start = end;
            }
        }

        result.push_back(std::move(track));
    }

    av_frame_free(&audioFrame);
    av_packet_free(&packet);
    for (auto& decoder : decoders)
    {
        swr_free(&decoder.vadResampler);
        if (decoder.vad)
            EmbeddedTenVadDestroy(&decoder.vad);
        avcodec_free_context(&decoder.codecContext);
    }
    avformat_close_input(&formatContext);
    return true;
}

static void WaveformThreadFunc()
{
    while (!g_waveformThreadExit.load())
    {
        if (WaitForSingleObject(g_waveformRequestEvent, INFINITE) != WAIT_OBJECT_0)
            continue;
        if (g_waveformThreadExit.load())
            break;

        std::wstring filename;
        double duration = 0.0;
        std::uint64_t generation = 0;
        bool highlightSpeech = false;
        {
            std::lock_guard<std::mutex> lock(g_waveformRequestMutex);
            filename = g_waveformRequestFile;
            duration = g_waveformRequestDuration;
            generation = g_waveformRequestGeneration;
            highlightSpeech = g_waveformRequestHighlightSpeech;
        }

        // Optional speech coloring is derived in the same pass. With it
        // disabled, no neural runtime or 16 kHz speech resampler is created.
        std::vector<AudioWaveformTrack> waveforms;
        bool built = BuildAudioWaveforms(
            filename, duration, generation, highlightSpeech, waveforms);
        if (built && !g_waveformThreadExit.load() &&
            g_waveformGeneration.load() == generation)
        {
            {
                std::lock_guard<std::mutex> lock(g_waveformCacheMutex);
                g_waveformCache = std::move(waveforms);
            }
            g_waveformProgress.store(-1);
            if (g_hTimeline)
                PostMessage(g_hTimeline, WM_AUDIO_WAVEFORM_READY, 0, 0);
        }
        else
        {
            g_waveformProgress.store(-1);
        }
    }
}

void RefreshAudioWaveformPreview()
{
    std::uint64_t generation = g_waveformGeneration.fetch_add(1) + 1;
    {
        std::lock_guard<std::mutex> lock(g_waveformCacheMutex);
        g_waveformCache.clear();
    }
    if (g_hTimeline)
        InvalidateRect(g_hTimeline, nullptr, FALSE);

    if (!g_showAudioWaveform || !g_videoPlayer || !g_videoPlayer->IsLoaded() ||
        g_videoPlayer->GetAudioTrackCount() <= 0 || !g_waveformRequestEvent)
    {
        g_waveformProgress.store(-1);
        return;
    }

    g_waveformProgress.store(0);

    {
        std::lock_guard<std::mutex> lock(g_waveformRequestMutex);
        g_waveformRequestFile = g_videoPlayer->loadedFilename;
        g_waveformRequestDuration = g_videoPlayer->GetDuration();
        g_waveformRequestGeneration = generation;
        g_waveformRequestHighlightSpeech = g_highlightSpeechWaveforms;
    }
    SetEvent(g_waveformRequestEvent);
}



static LRESULT CALLBACK TimecodeTooltipWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);

        // Dark background
        HBRUSH bg = CreateSolidBrush(RGB(30, 30, 30));
        FillRect(hdc, &rc, bg);
        DeleteObject(bg);

        if (!g_showVideoPreviewOnHover)
        {
            // Timecode-only mode: centre text in the whole window
            HFONT font = CreateFont(13, 0, 0, 0, FW_NORMAL, 0, 0, 0,
                                    DEFAULT_CHARSET, 0, 0, 0, 0, L"Arial");
            HGDIOBJ oldFont = SelectObject(hdc, font);
            SetTextColor(hdc, RGB(255, 255, 255));
            SetBkMode(hdc, TRANSPARENT);
            DrawTextW(hdc, g_timecodeTooltipText, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            SelectObject(hdc, oldFont);
            DeleteObject(font);
            EndPaint(hwnd, &ps);
            return 0;
        }

        // Copy the nearest cached thumbnail (any distance) — always show something
        std::vector<uint8_t> pixels;
        int thumbW = 0, thumbH = 0;
        {
            std::lock_guard<std::mutex> lck(g_thumbCacheMutex);
            double bestDist = 1e300;
            for (const auto& e : g_thumbCache)
            {
                if (e.pixels.empty()) continue;
                double d = std::fabs(e.time - g_timelineHoverTime);
                if (d < bestDist)
                {
                    bestDist = d;
                    pixels  = e.pixels;
                    thumbW  = e.width;
                    thumbH  = e.height;
                }
            }
        }

        const int tcH      = THUMB_TIMECODE_H;
        int       thumbAreaH = rc.bottom - tcH;

        if (!pixels.empty() && thumbW > 0 && thumbH > 0)
        {
            BITMAPINFO bmi          = {};
            bmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
            bmi.bmiHeader.biWidth       = thumbW;
            bmi.bmiHeader.biHeight      = -thumbH; // top-down DIB
            bmi.bmiHeader.biPlanes      = 1;
            bmi.bmiHeader.biBitCount    = 32;
            bmi.bmiHeader.biCompression = BI_RGB;
            int drawX = (rc.right  - thumbW) / 2;
            int drawY = (thumbAreaH - thumbH) / 2;
            SetDIBitsToDevice(hdc, drawX, drawY, thumbW, thumbH,
                              0, 0, 0, thumbH, pixels.data(), &bmi, DIB_RGB_COLORS);
        }

        // Separator line
        HBRUSH sep     = CreateSolidBrush(RGB(60, 60, 60));
        RECT   sepRect = { 0, thumbAreaH, rc.right, thumbAreaH + 1 };
        FillRect(hdc, &sepRect, sep);
        DeleteObject(sep);

        // Timecode text in bottom strip
        RECT tcRect = { 0, thumbAreaH, rc.right, rc.bottom };
        HFONT font = CreateFont(13, 0, 0, 0, FW_NORMAL, 0, 0, 0,
                                DEFAULT_CHARSET, 0, 0, 0, 0, L"Arial");
        HGDIOBJ oldFont = SelectObject(hdc, font);
        SetTextColor(hdc, RGB(255, 255, 255));
        SetBkMode(hdc, TRANSPARENT);
        DrawTextW(hdc, g_timecodeTooltipText, -1, &tcRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(hdc, oldFont);
        DeleteObject(font);

        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

// Timeline zoom variables
double g_timelineZoomLevel = 1.0;  // 1.0 = full video, 2.0 = 2x zoom, etc.
double g_timelineScrollOffset = 0.0;  // Horizontal scroll offset in seconds

// Timeline scroll arrow state
enum class ScrollArrowState { None, LeftArrow, RightArrow };
ScrollArrowState g_scrollArrowPressed = ScrollArrowState::None;
const int SCROLL_ARROW_TIMER_ID = 1001;
const int SCROLL_ARROW_TIMER_INTERVAL = 50;  // milliseconds

// Context-menu keyframe move state (move follows cursor until next click).
bool g_isContextKeyframeMoveMode = false;
double g_contextMovingKeyframeTime = -1.0;

// Helper function to convert pixel X coordinate to time, accounting for zoom and scroll
inline double PixelToTime(int x, RECT &rc, double duration)
{
    if (rc.right <= 0 || duration <= 0)
        return 0.0;
    double ratio = x / (double)rc.right;
    double timeRange = duration / g_timelineZoomLevel;
    return g_timelineScrollOffset + (ratio * timeRange);
}

// Helper function to convert time to pixel X coordinate, accounting for zoom and scroll
inline int TimeToPixel(double time, RECT &rc, double duration)
{
    if (duration <= 0 || g_timelineZoomLevel <= 0)
        return 0;
    if (time < g_timelineScrollOffset)
        return -1000;
    double relativeTime = time - g_timelineScrollOffset;
    double timeRange = duration / g_timelineZoomLevel;
    if (relativeTime > timeRange)
        return rc.right + 1000;
    double ratio = relativeTime / timeRange;
    return (int)(ratio * rc.right);
}

inline double ClampTimelineTimeFromMouseX(int x, RECT& rc, double duration)
{
    if (rc.right <= 0 || duration <= 0.0)
        return 0.0;

    if (x < 0)
        x = 0;
    else if (x > rc.right)
        x = rc.right;

    double seekTime = PixelToTime(x, rc, duration);
    if (seekTime < 0.0)
        seekTime = 0.0;
    if (seekTime >= duration)
    {
        // Keep the keyframe on the last displayable frame instead of placing it past EOF.
        double frameTime = g_videoPlayer->frameRate > 0 ? (1.0 / g_videoPlayer->frameRate) : 0.033;
        seekTime = duration - frameTime;
        if (seekTime < 0.0)
            seekTime = 0.0;
    }

    return seekTime;
}

LRESULT CALLBACK TimelineProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_CREATE:
    {
        WNDCLASSEXW wc = { sizeof(wc) };
        wc.lpfnWndProc = TimecodeTooltipWndProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = TIMECODE_TOOLTIP_CLASS;
        RegisterClassExW(&wc); // Ignore error if already registered
        g_timecodeTooltipWnd = CreateWindowExW(
            WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
            TIMECODE_TOOLTIP_CLASS, L"",
            WS_POPUP | WS_BORDER,
            0, 0, THUMB_W, THUMB_MAX_H + THUMB_TIMECODE_H,
            nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        // Start background thumbnail decode thread
        g_thumbThreadExit.store(false);
        g_thumbRequestTime.store(-1.0);
        g_thumbRequestEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        g_thumbThread = std::thread(ThumbnailThreadFunc);
        g_waveformThreadExit.store(false);
        g_waveformRequestEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (g_waveformRequestEvent)
            g_waveformThread = std::thread(WaveformThreadFunc);
        return 0;
    }
    case WM_DESTROY:
        g_waveformGeneration.fetch_add(1);
        g_waveformThreadExit.store(true);
        if (g_waveformRequestEvent)
            SetEvent(g_waveformRequestEvent);
        if (g_waveformThread.joinable())
            g_waveformThread.join();
        if (g_waveformRequestEvent)
        {
            CloseHandle(g_waveformRequestEvent);
            g_waveformRequestEvent = nullptr;
        }
        {
            std::lock_guard<std::mutex> lock(g_waveformCacheMutex);
            g_waveformCache.clear();
        }
        ShutdownEmbeddedTenVadRuntime();
        // Stop thumbnail decode thread before destroying the tooltip window
        if (g_thumbRequestEvent)
        {
            g_thumbThreadExit.store(true);
            SetEvent(g_thumbRequestEvent);
        }
        if (g_thumbThread.joinable())
            g_thumbThread.join();
        {
            std::lock_guard<std::mutex> lck(g_thumbCacheMutex);
            g_thumbCache.clear();
        }
        if (g_thumbRequestEvent)
        {
            CloseHandle(g_thumbRequestEvent);
            g_thumbRequestEvent = nullptr;
        }
        if (g_timecodeTooltipWnd)
        {
            DestroyWindow(g_timecodeTooltipWnd);
            g_timecodeTooltipWnd = nullptr;
        }
        return 0;
    case WM_AUDIO_WAVEFORM_READY:
        InvalidateRect(hwnd, nullptr, FALSE);
        UpdateControls();
        return 0;
    case WM_LBUTTONDOWN:
        if (g_videoPlayer && g_videoPlayer->IsLoaded())
        {
            if (g_isContextKeyframeMoveMode)
            {
                RECT rc; GetClientRect(hwnd, &rc);
                double dur = g_videoPlayer->GetDuration();
                if (dur > 0.0 && g_contextMovingKeyframeTime >= 0.0)
                {
                    int x = GET_X_LPARAM(lParam);
                    double targetTime = ClampTimelineTimeFromMouseX(x, rc, dur);
                    g_videoPlayer->MoveCropKeyframe(g_contextMovingKeyframeTime, targetTime);
                }

                g_isContextKeyframeMoveMode = false;
                g_contextMovingKeyframeTime = -1.0;
                if (GetCapture() == hwnd)
                    ReleaseCapture();
                InvalidateRect(hwnd, NULL, FALSE);
                UpdateControls();
                UpdateTimeline();
                return 0;
            }

            SetFocus(hwnd);
            RECT rc; GetClientRect(hwnd, &rc);
            int x = GET_X_LPARAM(lParam);
            
            // Check if clicking on scroll arrows (only show when zoomed in)
            const int ARROW_WIDTH = 20;
            if (g_timelineZoomLevel > 1.0)
            {
                // Left arrow click
                if (x < ARROW_WIDTH && g_timelineScrollOffset > 0)
                {
                    g_scrollArrowPressed = ScrollArrowState::LeftArrow;
                    SetCapture(hwnd);
                    SetTimer(hwnd, SCROLL_ARROW_TIMER_ID, SCROLL_ARROW_TIMER_INTERVAL, NULL);
                    double dur = g_videoPlayer->GetDuration();
                    double timeRange = dur / g_timelineZoomLevel;
                    g_timelineScrollOffset -= timeRange * 0.1;  // Scroll left by 10%
                    if (g_timelineScrollOffset < 0) g_timelineScrollOffset = 0;
                    InvalidateRect(hwnd, NULL, FALSE);
                    UpdateControls();
                    return 0;
                }
                // Right arrow click
                if (x > rc.right - ARROW_WIDTH)
                {
                    g_scrollArrowPressed = ScrollArrowState::RightArrow;
                    SetCapture(hwnd);
                    SetTimer(hwnd, SCROLL_ARROW_TIMER_ID, SCROLL_ARROW_TIMER_INTERVAL, NULL);
                    double dur = g_videoPlayer->GetDuration();
                    double timeRange = dur / g_timelineZoomLevel;
                    double maxOffset = dur - timeRange;
                    g_timelineScrollOffset += timeRange * 0.1;  // Scroll right by 10%
                    if (g_timelineScrollOffset > maxOffset) g_timelineScrollOffset = maxOffset;
                    InvalidateRect(hwnd, NULL, FALSE);
                    UpdateControls();
                    return 0;
                }
            }
            
            if (x < 0) x = 0; if (x > rc.right) x = rc.right;
            double dur = g_videoPlayer->GetDuration();
            double seekTime = PixelToTime(x, rc, dur);

            // Check if clicking on a keyframe to drag it
            if (dur > 0.0 && rc.right > 0)
            {
                auto keys = g_videoPlayer->GetCropKeyframes();
                for (const auto& key : keys)
                {
                    if (key.time < 0.0 || key.time > dur)
                        continue;
                    int px = TimeToPixel(key.time, rc, dur);
                    // If clicking within 8 pixels of a keyframe marker, start dragging it
                    if (std::abs(px - x) <= 8)
                    {
                        g_timelineDragMode = DragMode::Keyframe;
                        g_draggedKeyframeTime = key.time;
                        g_isTimelineDragging = true;
                        SetCapture(hwnd);
                        InvalidateRect(hwnd, NULL, FALSE);
                        UpdateControls();
                        return 0;
                    }
                }
            }

            // Disable moving start/end markers via mouse click/drag to prevent
            // accidental adjustments. Treat clicks near markers the same as a
            // regular cursor click so users must use the numeric inputs to
            // adjust `g_cutStartTime` and `g_cutEndTime`.
            if (g_videoPlayer->IsClipPreviewActive())
            {
                // Clamp to just before the clip end so we don't overshoot
                double clampedSeek = seekTime;
                if (g_cutEndTime >= 0 && clampedSeek >= g_cutEndTime)
                {
                    double frameTime = g_videoPlayer->frameRate > 0 ? (1.0 / g_videoPlayer->frameRate) : 0.033;
                    clampedSeek = g_cutEndTime - frameTime;
                    if (clampedSeek < 0.0) clampedSeek = 0.0;
                }
                g_videoPlayer->PlayClip(clampedSeek, g_cutEndTime);
                InvalidateRect(hwnd, NULL, FALSE);
                UpdateControls();
                return 0;
            }
            g_timelineDragMode = DragMode::Cursor;
            g_wasPlayingBeforeDrag = g_videoPlayer->IsPlaying();
            
            // Immediate UI update
            g_previewSeekTime = seekTime;
            InvalidateRect(hwnd, NULL, FALSE);
            UpdateWindow(hwnd); // Force restart of paint cycle to draw line immediately
            
            if (!g_wasPlayingBeforeDrag)
                g_videoPlayer->SeekToTime(seekTime, INT_MAX, false, false);
            else
                g_videoPlayer->SeekWhilePlaying(seekTime, false);
            // Keep g_previewSeekTime pinned to the target; UpdateTimeline() will
            // clear it automatically once actual currentPts catches up.

            g_isTimelineDragging = true;
            SetCapture(hwnd);
            InvalidateRect(hwnd, NULL, FALSE);
            UpdateControls();
            return 0;
        }
        break;
    case WM_MOUSEMOVE:
        // Update hover position for tooltip
        g_timelineHoverX = GET_X_LPARAM(lParam);
        if (!g_timelineMouseTracking)
        {
            TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwnd, 0 };
            TrackMouseEvent(&tme);
            g_timelineMouseTracking = true;
        }
        // Show timecode tooltip popup above the timeline (always; size depends on preview setting)
        if (g_timecodeTooltipWnd && g_videoPlayer && g_videoPlayer->IsLoaded())
        {
            RECT rc; GetClientRect(hwnd, &rc);
            double dur = g_videoPlayer->GetDuration();
            if (dur > 0.0)
            {
                double hoverTime = PixelToTime(g_timelineHoverX, rc, dur);
                if (hoverTime >= 0.0 && hoverTime < dur)
                {
                    int totalSecs = (int)hoverTime;
                    int hours = totalSecs / 3600;
                    int mins = (totalSecs % 3600) / 60;
                    int secs = totalSecs % 60;
                    if (hours > 0)
                        swprintf(g_timecodeTooltipText, 32, L"%d:%02d:%02d", hours, mins, secs);
                    else
                        swprintf(g_timecodeTooltipText, 32, L"%02d:%02d", mins, secs);

                    int tooltipW, tooltipH;
                    if (g_showVideoPreviewOnHover)
                    {
                        // Size to thumbnail + timecode strip
                        int ttW = THUMB_W, ttH = THUMB_MAX_H;
                        int fw = g_videoPlayer->frameWidth;
                        int fh = g_videoPlayer->frameHeight;
                        if (fw > 0 && fh > 0)
                        {
                            ttH = (int)((double)THUMB_W * fh / fw);
                            if (ttH > THUMB_MAX_H) { ttH = THUMB_MAX_H; ttW = (int)((double)THUMB_MAX_H * fw / fh); }
                            if (ttW < 60) ttW = 60;
                            if (ttH < 20) ttH = 20;
                        }
                        tooltipW = ttW;
                        tooltipH = ttH + THUMB_TIMECODE_H;
                    }
                    else
                    {
                        // Timecode-only: measure text and add small padding
                        HDC hdc = GetDC(g_timecodeTooltipWnd);
                        HFONT font = CreateFont(13, 0, 0, 0, FW_NORMAL, 0, 0, 0,
                                               DEFAULT_CHARSET, 0, 0, 0, 0, L"Arial");
                        HGDIOBJ oldFont = SelectObject(hdc, font);
                        SIZE sz = {};
                        GetTextExtentPoint32W(hdc, g_timecodeTooltipText,
                                              (int)wcslen(g_timecodeTooltipText), &sz);
                        SelectObject(hdc, oldFont);
                        DeleteObject(font);
                        ReleaseDC(g_timecodeTooltipWnd, hdc);
                        const int PAD = 8;
                        tooltipW = sz.cx + PAD * 2;
                        tooltipH = sz.cy + PAD;
                        if (tooltipW < 50) tooltipW = 50;
                    }

                    POINT pt = { g_timelineHoverX, 0 };
                    ClientToScreen(hwnd, &pt);
                    int sx = pt.x - tooltipW / 2;
                    int sy = pt.y - tooltipH - 4;
                    SetWindowPos(g_timecodeTooltipWnd, HWND_TOPMOST, sx, sy, tooltipW, tooltipH,
                                 SWP_NOACTIVATE | SWP_SHOWWINDOW);
                    InvalidateRect(g_timecodeTooltipWnd, nullptr, FALSE);

                    if (g_showVideoPreviewOnHover)
                    {
                        // Request thumbnail decode for the current 0.5 s slot
                        double slotTime = std::floor(hoverTime * 2.0) / 2.0;
                        g_timelineHoverTime = slotTime;
                        bool needDecode = true;
                        {
                            std::lock_guard<std::mutex> lck(g_thumbCacheMutex);
                            for (const auto& e : g_thumbCache)
                                if (std::fabs(e.time - slotTime) < 0.3) { needDecode = false; break; }
                        }
                        if (needDecode && g_thumbRequestEvent)
                        {
                            g_thumbRequestTime.store(slotTime);
                            SetEvent(g_thumbRequestEvent);
                        }
                    }
                }
            }
        }
        if (g_isContextKeyframeMoveMode && g_videoPlayer && g_videoPlayer->IsLoaded())
        {
            RECT rc; GetClientRect(hwnd, &rc);
            double dur = g_videoPlayer->GetDuration();
            if (dur > 0.0 && g_contextMovingKeyframeTime >= 0.0)
            {
                int x = GET_X_LPARAM(lParam);
                double targetTime = ClampTimelineTimeFromMouseX(x, rc, dur);
                if (std::fabs(targetTime - g_contextMovingKeyframeTime) >= 0.001 &&
                    g_videoPlayer->MoveCropKeyframe(g_contextMovingKeyframeTime, targetTime))
                {
                    double oldTime = g_contextMovingKeyframeTime;
                    g_contextMovingKeyframeTime = targetTime;
                    if (!g_videoPlayer->IsPlaying())
                    {
                        double curTime = g_videoPlayer->GetCurrentTime();
                        if ((oldTime <= curTime && targetTime >= curTime) || 
                            (oldTime >= curTime && targetTime <= curTime))
                        {
                            if (g_videoPlayer->UpdateCropForTime(curTime))
                                g_videoPlayer->ForceRedraw();
                        }
                    }
                    InvalidateRect(hwnd, NULL, FALSE);
                    UpdateControls();
                }
            }
            return 0;
        }

        if (g_isTimelineDragging && g_videoPlayer && g_videoPlayer->IsLoaded())
        {
            RECT rc; GetClientRect(hwnd, &rc);
            int x = GET_X_LPARAM(lParam);
            if (x < 0) x = 0; if (x > rc.right) x = rc.right;
            double dur = g_videoPlayer->GetDuration();
            double seekTime = PixelToTime(x, rc, dur);
            
            // Clamp seekTime to valid range [0, duration)
            if (seekTime < 0.0) seekTime = 0.0;
            if (dur > 0.0 && seekTime >= dur) {
                // Clamp to just before the end (last frame)
                double frameTime = g_videoPlayer->frameRate > 0 ? (1.0 / g_videoPlayer->frameRate) : 0.033;
                seekTime = dur - frameTime;
                if (seekTime < 0.0) seekTime = 0.0;
            }

            if (g_timelineDragMode == DragMode::Cursor)
            {
                // Immediate UI update
                g_previewSeekTime = seekTime;
                InvalidateRect(hwnd, NULL, FALSE);
                UpdateWindow(hwnd);
                
                if (g_wasPlayingBeforeDrag)
                    g_videoPlayer->SeekWhilePlaying(seekTime, false);
                else
                    g_videoPlayer->SeekToTime(seekTime, 0);
                // Keep g_previewSeekTime pinned; UpdateTimeline() auto-clears it.
            }
            else if (g_timelineDragMode == DragMode::Keyframe)
            {
                // Move the dragged keyframe to the new time
                if (g_draggedKeyframeTime >= 0.0 && dur > 0.0)
                {
                    double oldTime = g_draggedKeyframeTime;
                    g_videoPlayer->MoveCropKeyframe(g_draggedKeyframeTime, seekTime);
                    g_draggedKeyframeTime = seekTime;  // Update the tracked time
                    
                    if (!g_videoPlayer->IsPlaying())
                    {
                        double curTime = g_videoPlayer->GetCurrentTime();
                        if ((oldTime <= curTime && seekTime >= curTime) || 
                            (oldTime >= curTime && seekTime <= curTime))
                        {
                            if (g_videoPlayer->UpdateCropForTime(curTime))
                                g_videoPlayer->ForceRedraw();
                        }
                    }
                }
            }
            else if (g_timelineDragMode == DragMode::StartMarker)
            {
                if (g_cutEndTime >= 0 && seekTime >= g_cutEndTime)
                    seekTime = g_cutEndTime - 0.01;
                if (seekTime < 0) seekTime = 0;
                g_cutStartTime = seekTime;
                UpdateCutInfoLabel(GetParent(hwnd));
            }
            else if (g_timelineDragMode == DragMode::EndMarker)
            {
                if (g_cutStartTime >= 0 && seekTime <= g_cutStartTime)
                    seekTime = g_cutStartTime + 0.01;
                g_cutEndTime = seekTime;
                UpdateCutInfoLabel(GetParent(hwnd));
            }

            UpdateCutTimeEdits();
            InvalidateRect(hwnd, NULL, FALSE);
            UpdateControls();
            return 0;
        }
        // Hovering without dragging — redraw for tooltip update
        InvalidateRect(hwnd, NULL, FALSE);
        break;
    case WM_LBUTTONUP:
        // Stop arrow scrolling if it was active
        if (g_scrollArrowPressed != ScrollArrowState::None)
        {
            KillTimer(hwnd, SCROLL_ARROW_TIMER_ID);
            ReleaseCapture();
            g_scrollArrowPressed = ScrollArrowState::None;
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }
        
        if (g_isTimelineDragging && g_videoPlayer && g_videoPlayer->IsLoaded())
        {
            ReleaseCapture();
            g_isTimelineDragging = false;
            RECT rc; GetClientRect(hwnd, &rc);
            int x = GET_X_LPARAM(lParam);
            if (x < 0) x = 0; if (x > rc.right) x = rc.right;
            double dur = g_videoPlayer->GetDuration();
            double seekTime = PixelToTime(x, rc, dur);
            
            // Clamp seekTime to valid range [0, duration)
            if (seekTime < 0.0) seekTime = 0.0;
            if (dur > 0.0 && seekTime >= dur) {
                // Clamp to just before the end (last frame)
                double frameTime = g_videoPlayer->frameRate > 0 ? (1.0 / g_videoPlayer->frameRate) : 0.033;
                seekTime = dur - frameTime;
                if (seekTime < 0.0) seekTime = 0.0;
            }

            if (g_timelineDragMode == DragMode::Cursor)
            {
                // Pin cursor to the drop position during the final refinement pass.
                g_previewSeekTime = seekTime;
                if (!g_wasPlayingBeforeDrag)
                    g_videoPlayer->SeekToTime(seekTime, INT_MAX, false, false);
                else
                    g_videoPlayer->SeekWhilePlaying(seekTime);
            }
            else if (g_timelineDragMode == DragMode::Keyframe)
            {
                // Keyframe drag is complete
                g_draggedKeyframeTime = -1.0;
                
                if (!g_videoPlayer->IsPlaying())
                {
                    if (g_videoPlayer->UpdateCropForTime(g_videoPlayer->GetCurrentTime()))
                        g_videoPlayer->ForceRedraw();
                }
            }
            else if (g_timelineDragMode == DragMode::StartMarker)
            {
                if (g_cutEndTime >= 0 && seekTime >= g_cutEndTime)
                    seekTime = g_cutEndTime - 0.01;
                if (seekTime < 0) seekTime = 0;
                g_cutStartTime = seekTime;
                UpdateCutInfoLabel(GetParent(hwnd));
            }
            else if (g_timelineDragMode == DragMode::EndMarker)
            {
                if (g_cutStartTime >= 0 && seekTime <= g_cutStartTime)
                    seekTime = g_cutStartTime + 0.01;
                g_cutEndTime = seekTime;
                UpdateCutInfoLabel(GetParent(hwnd));
            }

            g_timelineDragMode = DragMode::None;
            UpdateCutTimeEdits();
            InvalidateRect(hwnd, NULL, FALSE);
            UpdateControls();
            return 0;
        }
        break;
    case WM_RBUTTONUP:
        if (g_videoPlayer && g_videoPlayer->IsLoaded())
        {
            RECT rc; GetClientRect(hwnd, &rc);
            int x = GET_X_LPARAM(lParam);
            int y = GET_Y_LPARAM(lParam);
            if (x < 0) x = 0; if (x > rc.right) x = rc.right;
            double dur = g_videoPlayer->GetDuration();
            if (dur > 0.0 && rc.right > 0)
            {
                if (g_enableMultiClipEditing && y >= std::max(0L, rc.bottom - 10))
                {
                    int selectedSegment = -1;
                    for (size_t i = 0; i < g_cutSegments.size(); ++i)
                    {
                        int sx = TimeToPixel(g_cutSegments[i].start, rc, dur);
                        int ex = TimeToPixel(g_cutSegments[i].end, rc, dur);
                        int left = std::max(0, sx);
                        int right = std::min(static_cast<int>(rc.right), std::max(sx + 1, ex));
                        if (right >= 0 && left <= rc.right && x >= left && x <= right)
                        {
                            selectedSegment = static_cast<int>(i);
                            break;
                        }
                    }

                    if (selectedSegment >= 0)
                    {
                        POINT pt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
                        ClientToScreen(hwnd, &pt);
                        HMENU menu = CreatePopupMenu();
                        AppendMenuW(menu, MF_STRING, 10, L"Edit");
                        int cmd = TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_RETURNCMD,
                                                 pt.x, pt.y, 0, hwnd, nullptr);
                        DestroyMenu(menu);
                        if (cmd == 10)
                            SelectCutSegmentForEditing(GetParent(hwnd), selectedSegment);
                        return 0;
                    }
                }

                auto keys = g_videoPlayer->GetCropKeyframes();
                double selectedTime = -1.0;
                for (const auto& key : keys)
                {
                    if (key.time < 0.0 || key.time > dur)
                        continue;
                    int px = TimeToPixel(key.time, rc, dur);
                    if (std::abs(px - x) <= 6)
                    {
                        selectedTime = key.time;
                        break;
                    }
                }

                if (selectedTime >= 0.0)
                {
                    POINT pt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
                    ClientToScreen(hwnd, &pt);
                    HMENU menu = CreatePopupMenu();
                    AppendMenu(menu, MF_STRING, 1, L"Edit Keyframe");
                    AppendMenu(menu, MF_STRING, 2, L"Delete Keyframe");
                    AppendMenu(menu, MF_STRING, 3, L"Move Keyframe");
                    int cmd = TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_RETURNCMD, pt.x, pt.y, 0, hwnd, nullptr);
                    DestroyMenu(menu);
                    if (cmd == 1)
                    {
                        // Edit Keyframe: seek to the exact keyframe timestamp
                        g_videoPlayer->SeekToTimeExact(selectedTime);
                        InvalidateRect(hwnd, NULL, FALSE);
                        UpdateControls();
                        UpdateTimeline();
                    }
                    else if (cmd == 2)
                    {
                        // Delete Keyframe
                        if (g_videoPlayer->RemoveCropKeyframe(selectedTime))
                        {
                            g_videoPlayer->UpdateCropForTime(g_videoPlayer->GetCurrentTime());
                            UpdateControls();
                            UpdateTimeline();
                        }
                    }
                    else if (cmd == 3)
                    {
                        g_isContextKeyframeMoveMode = true;
                        g_contextMovingKeyframeTime = selectedTime;
                        SetCapture(hwnd);
                    }
                    return 0;
                }
            }
        }
        break;
    case WM_CAPTURECHANGED:
        if (g_isContextKeyframeMoveMode && (HWND)lParam != hwnd)
        {
            g_isContextKeyframeMoveMode = false;
            g_contextMovingKeyframeTime = -1.0;
            InvalidateRect(hwnd, NULL, FALSE);
            UpdateControls();
        }
        break;
    case WM_MOUSELEAVE:
        g_timelineHoverX = -1;
        g_timelineHoverTime = -1.0;
        g_timelineMouseTracking = false;
        if (g_timecodeTooltipWnd)
            ShowWindow(g_timecodeTooltipWnd, SW_HIDE);
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    case WM_MOUSEWHEEL:
    {
        if (g_videoPlayer && g_videoPlayer->IsLoaded())
        {
            int wheelDelta = GET_WHEEL_DELTA_WPARAM(wParam);
            RECT rc; GetClientRect(hwnd, &rc);
            double dur = g_videoPlayer->GetDuration();
            
            // Get mouse position to zoom around that point
            int x = GET_X_LPARAM(lParam);
            POINT pt = { x, GET_Y_LPARAM(lParam) };
            ScreenToClient(hwnd, &pt);
            
            // Determine zoom anchor: if cursor is very near start/end, anchor to that boundary
            double zoomAnchorPixel = pt.x;
            const double EDGE_THRESHOLD = 0.1;  // 10% of timeline width
            if (pt.x < rc.right * EDGE_THRESHOLD)
            {
                zoomAnchorPixel = 0.0;  // Anchor to start
            }
            else if (pt.x > rc.right * (1.0 - EDGE_THRESHOLD))
            {
                zoomAnchorPixel = rc.right;  // Anchor to end
            }
            
            // Calculate the time at the anchor point before zoom
            double timeAtAnchor = PixelToTime((int)zoomAnchorPixel, rc, dur);
            
            // Adjust zoom level (1.0 = min, 500.0 = max for deep zooming)
            double oldZoom = g_timelineZoomLevel;
            if (wheelDelta > 0)
            {
                g_timelineZoomLevel *= 1.2;  // Zoom in
                if (g_timelineZoomLevel > 500.0) g_timelineZoomLevel = 500.0;
            }
            else
            {
                g_timelineZoomLevel /= 1.2;  // Zoom out
                if (g_timelineZoomLevel < 1.0) g_timelineZoomLevel = 1.0;
            }
            
            // Adjust scroll offset to keep the same time at the anchor point
            if (g_timelineZoomLevel > 1.0)
            {
                double timeRange = dur / g_timelineZoomLevel;
                g_timelineScrollOffset = timeAtAnchor - (zoomAnchorPixel / (double)rc.right) * timeRange;
                
                // Clamp scroll offset to valid range
                double maxOffset = dur - timeRange;
                if (g_timelineScrollOffset < 0) g_timelineScrollOffset = 0;
                if (g_timelineScrollOffset > maxOffset) g_timelineScrollOffset = maxOffset;
            }
            else
            {
                g_timelineScrollOffset = 0.0;
            }
            
            InvalidateRect(hwnd, NULL, FALSE);
            UpdateControls();
            return 0;
        }
        break;
    }
    case WM_TIMER:
    {
        if (wParam == SCROLL_ARROW_TIMER_ID && g_videoPlayer && g_videoPlayer->IsLoaded())
        {
            if (g_scrollArrowPressed == ScrollArrowState::LeftArrow)
            {
                double dur = g_videoPlayer->GetDuration();
                double timeRange = dur / g_timelineZoomLevel;
                g_timelineScrollOffset -= timeRange * 0.1;  // Scroll left by 10%
                if (g_timelineScrollOffset < 0) g_timelineScrollOffset = 0;
                InvalidateRect(hwnd, NULL, FALSE);
                UpdateControls();
                return 0;
            }
            else if (g_scrollArrowPressed == ScrollArrowState::RightArrow)
            {
                double dur = g_videoPlayer->GetDuration();
                double timeRange = dur / g_timelineZoomLevel;
                double maxOffset = dur - timeRange;
                g_timelineScrollOffset += timeRange * 0.1;  // Scroll right by 10%
                if (g_timelineScrollOffset > maxOffset) g_timelineScrollOffset = maxOffset;
                InvalidateRect(hwnd, NULL, FALSE);
                UpdateControls();
                return 0;
            }
        }
        break;
    }
    case WM_ERASEBKGND:
        // WM_PAINT redraws the complete control from an off-screen buffer.
        // Suppressing the separate erase pass prevents playback flicker.
        return 1;
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC paintDC = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);
        if (rc.right <= 0 || rc.bottom <= 0)
        {
            EndPaint(hwnd, &ps);
            return 0;
        }

        HDC hdc = CreateCompatibleDC(paintDC);
        HBITMAP bufferBitmap = CreateCompatibleBitmap(paintDC, rc.right, rc.bottom);
        HGDIOBJ oldBitmap = SelectObject(hdc, bufferBitmap);

        HBRUSH bg = CreateSolidBrush(RGB(70,70,70));
        FillRect(hdc, &rc, bg);
        DeleteObject(bg);
        
        // Draw scroll arrows if zoomed in
        const int ARROW_WIDTH = 20;
        if (g_timelineZoomLevel > 1.0)
        {
            // Draw left arrow if not at start
            if (g_timelineScrollOffset > 0)
            {
                RECT leftArrowRect = { 0, 0, ARROW_WIDTH, rc.bottom };
                HBRUSH arrowBrush = CreateSolidBrush(RGB(150, 150, 150));
                FillRect(hdc, &leftArrowRect, arrowBrush);
                DeleteObject(arrowBrush);
                
                // Draw "<" character
                SetTextColor(hdc, RGB(0, 0, 0));
                SetBkMode(hdc, TRANSPARENT);
                HFONT font = CreateFont(14, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, 0, 0, L"Arial");
                HGDIOBJ oldFont = SelectObject(hdc, font);
                TextOutW(hdc, 5, rc.bottom / 2 - 7, L"<", 1);
                SelectObject(hdc, oldFont);
                DeleteObject(font);
            }
            
            // Draw right arrow if not at end
            double dur = g_videoPlayer && g_videoPlayer->IsLoaded() ? g_videoPlayer->GetDuration() : 0;
            if (dur > 0)
            {
                double timeRange = dur / g_timelineZoomLevel;
                double maxOffset = dur - timeRange;
                if (g_timelineScrollOffset < maxOffset)
                {
                    RECT rightArrowRect = { rc.right - ARROW_WIDTH, 0, rc.right, rc.bottom };
                    HBRUSH arrowBrush = CreateSolidBrush(RGB(150, 150, 150));
                    FillRect(hdc, &rightArrowRect, arrowBrush);
                    DeleteObject(arrowBrush);
                    
                    // Draw ">" character
                    SetTextColor(hdc, RGB(0, 0, 0));
                    SetBkMode(hdc, TRANSPARENT);
                    HFONT font = CreateFont(14, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, 0, 0, L"Arial");
                    HGDIOBJ oldFont = SelectObject(hdc, font);
                    TextOutW(hdc, rc.right - ARROW_WIDTH + 5, rc.bottom / 2 - 7, L">", 1);
                    SelectObject(hdc, oldFont);
                    DeleteObject(font);
                }
            }
        }
        
        if (g_videoPlayer && g_videoPlayer->IsLoaded())
        {
            double dur = g_videoPlayer->GetDuration();

            if (g_showAudioWaveform && dur > 0.0)
            {
                std::vector<AudioWaveformTrack> waveforms;
                {
                    std::lock_guard<std::mutex> lock(g_waveformCacheMutex);
                    waveforms = g_waveformCache;
                }

                int trackCount = g_videoPlayer->GetAudioTrackCount();
                if (!waveforms.empty() && trackCount > 0)
                {
                    const int left = g_timelineZoomLevel > 1.0 ? ARROW_WIDTH : 0;
                    const int right = g_timelineZoomLevel > 1.0 ? rc.right - ARROW_WIDTH : rc.right;
                    const COLORREF trackColors[] = {
                        RGB(94, 169, 154),
                        RGB(213, 116, 101),
                        RGB(111, 143, 207),
                        RGB(190, 163, 91),
                        RGB(159, 119, 184),
                        RGB(108, 173, 104)
                    };

                    for (int trackIndex = 0; trackIndex < trackCount; ++trackIndex)
                    {
                        if (g_videoPlayer->IsAudioTrackMuted(trackIndex))
                            continue;

                        int streamIndex = g_videoPlayer->audioTracks[trackIndex]->streamIndex;
                        auto waveformIt = std::find_if(
                            waveforms.begin(), waveforms.end(),
                            [streamIndex](const AudioWaveformTrack& waveform) {
                                return waveform.streamIndex == streamIndex;
                            });
                        if (waveformIt == waveforms.end() || waveformIt->samples.empty())
                            continue;

                        // Keep tracks in stable, compact lanes. Muting a track
                        // removes its trace without shifting the remaining ones.
                        int laneTop = static_cast<int>(
                            static_cast<long long>(rc.bottom) * trackIndex / trackCount);
                        int laneBottom = static_cast<int>(
                            static_cast<long long>(rc.bottom) * (trackIndex + 1) / trackCount);
                        int centerY = (laneTop + laneBottom) / 2;
                        int maxAmplitude = std::max(1, (laneBottom - laneTop - 2) / 2);

                        std::vector<POINT> points;
                        points.reserve(static_cast<size_t>(std::max(0, right - left) / 2 + 2));
                        for (int px = left; px < right; px += 2)
                        {
                            double time = PixelToTime(px, rc, dur);
                            int sampleIndex = std::clamp(static_cast<int>(
                                time * waveformIt->samples.size() / dur),
                                0, static_cast<int>(waveformIt->samples.size()) - 1);
                            float scaled = std::min(1.0f, waveformIt->samples[sampleIndex]);
                            int amplitude = static_cast<int>(std::round(scaled * maxAmplitude));
                            points.push_back({ px, centerY - amplitude });
                        }
                        if (!points.empty() && points.back().x != right - 1)
                        {
                            double time = PixelToTime(right - 1, rc, dur);
                            int sampleIndex = std::clamp(static_cast<int>(
                                time * waveformIt->samples.size() / dur),
                                0, static_cast<int>(waveformIt->samples.size()) - 1);
                            float scaled = std::min(1.0f, waveformIt->samples[sampleIndex]);
                            int amplitude = static_cast<int>(std::round(scaled * maxAmplitude));
                            points.push_back({ right - 1, centerY - amplitude });
                        }

                        if (points.size() >= 2)
                        {
                            HPEN waveformPen = CreatePen(
                                PS_SOLID, 1,
                                trackColors[trackIndex % _countof(trackColors)]);
                            HGDIOBJ oldPen = SelectObject(hdc, waveformPen);
                            Polyline(hdc, points.data(), static_cast<int>(points.size()));
                            SelectObject(hdc, oldPen);
                            DeleteObject(waveformPen);
                        }

                        if (!waveformIt->probableSpeech.empty())
                        {
                            // Repaint only the waveform sections classified as
                            // probable speech. No extra bars or icons compete
                            // with the audio trace.
                            HPEN speechPen =
                                CreatePen(PS_SOLID, 2, RGB(105, 226, 154));
                            HGDIOBJ oldSpeechPen =
                                SelectObject(hdc, speechPen);
                            size_t speechStart = points.size();
                            for (size_t i = 0; i <= points.size(); ++i)
                            {
                                bool speech = false;
                                if (i < points.size())
                                {
                                    double time =
                                        PixelToTime(points[i].x, rc, dur);
                                    int speechIndex = std::clamp(
                                        static_cast<int>(
                                            time *
                                            waveformIt->probableSpeech.size() /
                                            dur),
                                        0, static_cast<int>(
                                            waveformIt->probableSpeech.size()) -
                                            1);
                                    speech = waveformIt
                                                 ->probableSpeech[speechIndex] !=
                                             0;
                                }

                                if (speech && speechStart == points.size())
                                {
                                    speechStart = i;
                                }
                                else if (!speech &&
                                         speechStart != points.size())
                                {
                                    const size_t pointCount = i - speechStart;
                                    if (pointCount >= 2)
                                    {
                                        Polyline(
                                            hdc, points.data() + speechStart,
                                            static_cast<int>(pointCount));
                                    }
                                    speechStart = points.size();
                                }
                            }
                            SelectObject(hdc, oldSpeechPen);
                            DeleteObject(speechPen);
                        }
                    }
                }
            }

            HBRUSH segmentBrush = CreateSolidBrush(RGB(35, 145, 95));
            HBRUSH selectedSegmentBrush = CreateSolidBrush(RGB(70, 210, 135));
            for (size_t i = 0; g_enableMultiClipEditing && i < g_cutSegments.size(); ++i)
            {
                const auto& segment = g_cutSegments[i];
                int sx = TimeToPixel(segment.start, rc, dur);
                int ex = TimeToPixel(segment.end, rc, dur);
                RECT segmentRect = { sx, std::max(0L, rc.bottom - 8), std::max(sx + 1, ex), rc.bottom };
                FillRect(hdc, &segmentRect,
                         static_cast<int>(i) == g_selectedCutSegment ? selectedSegmentBrush : segmentBrush);
            }
            DeleteObject(segmentBrush);
            DeleteObject(selectedSegmentBrush);

            double cur = (g_previewSeekTime >= 0.0) ? g_previewSeekTime : g_videoPlayer->GetCurrentTime();
            int x = TimeToPixel(cur, rc, dur);
            HPEN pen = CreatePen(PS_SOLID, 2, RGB(200,0,0));
            HGDIOBJ old = SelectObject(hdc, pen);
            MoveToEx(hdc, x, 0, NULL);
            LineTo(hdc, x, rc.bottom);
            SelectObject(hdc, old);
            DeleteObject(pen);

            if (g_cutStartTime >= 0)
            {
                int sx = TimeToPixel(g_cutStartTime, rc, dur);
                pen = CreatePen(PS_SOLID, 1, RGB(0,200,0));
                old = SelectObject(hdc, pen);
                MoveToEx(hdc, sx, 0, NULL);
                LineTo(hdc, sx, rc.bottom);
                SelectObject(hdc, old);
                DeleteObject(pen);
            }
            if (g_cutEndTime >= 0)
            {
                int ex = TimeToPixel(g_cutEndTime, rc, dur);
                pen = CreatePen(PS_SOLID, 1, RGB(0,0,200));
                old = SelectObject(hdc, pen);
                MoveToEx(hdc, ex, 0, NULL);
                LineTo(hdc, ex, rc.bottom);
                SelectObject(hdc, old);
                DeleteObject(pen);
            }

            auto cropKeys = g_videoPlayer->GetCropKeyframes();
            if (!cropKeys.empty())
            {
                HPEN activePen = CreatePen(PS_SOLID, 1, RGB(255,215,0));
                HBRUSH activeBrush = CreateSolidBrush(RGB(255,215,0));
                HPEN disabledPen = CreatePen(PS_SOLID, 1, RGB(180,180,180));
                HGDIOBJ oldPen = SelectObject(hdc, activePen);
                HGDIOBJ oldBrush = SelectObject(hdc, activeBrush);

                for (const auto& key : cropKeys)
                {
                    if (key.time < 0.0 || key.time > dur)
                        continue;
                    int px = TimeToPixel(key.time, rc, dur);
                    if (px >= 0 && px < rc.right)  // Only draw if visible in zoomed view
                    {
                        POINT pts[3] = {
                            { px, 0 },
                            { px - 4, 8 },
                            { px + 4, 8 }
                        };
                        if (key.enabled)
                        {
                            SelectObject(hdc, activePen);
                            SelectObject(hdc, activeBrush);
                            Polygon(hdc, pts, 3);
                        }
                        else
                        {
                            SelectObject(hdc, disabledPen);
                            POINT outline[4] = {
                                { px, 0 },
                                { px - 4, 8 },
                                { px + 4, 8 },
                                { px, 0 }
                            };
                            Polyline(hdc, outline, 4);
                        }
                    }
                }

                SelectObject(hdc, oldPen);
                SelectObject(hdc, oldBrush);
                DeleteObject(activePen);
                DeleteObject(activeBrush);
                DeleteObject(disabledPen);
            }
        }

        BitBlt(paintDC, 0, 0, rc.right, rc.bottom, hdc, 0, 0, SRCCOPY);
        SelectObject(hdc, oldBitmap);
        DeleteObject(bufferBitmap);
        DeleteDC(hdc);
        EndPaint(hwnd, &ps);
        return 0;
    }
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}
