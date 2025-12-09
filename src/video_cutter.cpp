#include "video_cutter.h"
#include "video_player.h"
#include "options_window.h"
#include "debug_log.h"
#include "progress_window.h"
#include "rnnoise.h"
#include <iostream>
#include <sstream>
#include <commctrl.h>
#include <libavutil/frame.h>
#include <libavutil/mathematics.h>
#include <libavutil/pixfmt.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <chrono>
#include <string>

#define RNNOISE_FRAME_SIZE 480

namespace {

void FillBlackYuv420(AVFrame* frame)
{
    if (!frame)
        return;

    if (frame->format != AV_PIX_FMT_YUV420P) {
        for (int plane = 0; plane < AV_NUM_DATA_POINTERS && frame->data[plane]; ++plane) {
            int height = plane == 0 ? frame->height : (frame->height + 1) / 2;
            for (int y = 0; y < height; ++y) {
                std::memset(frame->data[plane] + y * frame->linesize[plane], 0, frame->linesize[plane]);
            }
        }
        return;
    }

    for (int y = 0; y < frame->height; ++y) {
        std::memset(frame->data[0] + y * frame->linesize[0], 0, frame->linesize[0]);
    }

    int chromaHeight = (frame->height + 1) / 2;
    for (int y = 0; y < chromaHeight; ++y) {
        std::memset(frame->data[1] + y * frame->linesize[1], 128, frame->linesize[1]);
        std::memset(frame->data[2] + y * frame->linesize[2], 128, frame->linesize[2]);
    }
}

}

VideoCutter::VideoCutter(VideoPlayer* player) : m_player(player), m_lastDisplayedPercent(-1) {}

VideoCutter::~VideoCutter() {}

void VideoCutter::ResetProgressTracking() {
    m_lastDisplayedPercent = -1;
    m_lastUpdateTime = std::chrono::high_resolution_clock::now();
}

bool VideoCutter::CutVideo(const std::wstring& outputFilename, double startTime,
                           double endTime, bool mergeAudio, bool convertH264,
                           EncoderSelection encoder, int maxBitrate, HWND progressBar,
                           std::atomic<bool>* cancelFlag)
{
    ResetProgressTracking();
    
    if (!m_player->isLoaded) {
        DebugLog("CutVideo called but no video loaded", true);
        return false;
    }

    {
        std::ostringstream oss;
        oss << "CutVideo start start=" << startTime << " end=" << endTime
            << " mergeAudio=" << mergeAudio
            << " convertH264=" << convertH264
            << " encoder=" << static_cast<int>(encoder)
            << " maxBitrate=" << maxBitrate;
        DebugLog(oss.str());
    }

    int bufSize = WideCharToMultiByte(CP_UTF8, 0, outputFilename.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string utf8Output(bufSize, 0);
    WideCharToMultiByte(CP_UTF8, 0, outputFilename.c_str(), -1, &utf8Output[0], bufSize, nullptr, nullptr);
    utf8Output.resize(bufSize - 1);

    bufSize = WideCharToMultiByte(CP_UTF8, 0, m_player->loadedFilename.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string utf8Input(bufSize, 0);
    WideCharToMultiByte(CP_UTF8, 0, m_player->loadedFilename.c_str(), -1, &utf8Input[0], bufSize, nullptr, nullptr);
    utf8Input.resize(bufSize - 1);

    std::vector<int> activeTracks;
    for (const auto& track : m_player->audioTracks) {
        if (!track->isMuted)
            activeTracks.push_back(track->streamIndex);
    }
    {
        std::ostringstream oss;
        oss << "Active tracks:";
        for (int idx : activeTracks) oss << ' ' << idx;
        DebugLog(oss.str());
    }

    // Check if any track has Voice Isolation enabled. Previously this forced
    // audio merging which surprised users who did not explicitly request it.
    // Instead we simply note that re-encoding is required for those tracks.
    bool anyVoiceIsolation = false;
    for (const auto& track : m_player->audioTracks) {
        if (!track->isMuted && track->voiceIsolationEnabled) {
            anyVoiceIsolation = true;
            break;
        }
    }

    // When re-encoding or merging audio we need to set up decoder/encoder
    // contexts. The previous implementation only supported stream copying.
    // Build encoder state on demand.
    bool timelineHasCrop = m_player->HasAnyCrop();
    if (timelineHasCrop)
        convertH264 = true;
    bool success = true;
    AVCodecContext* vEncCtx = nullptr;
    AVCodecContext* vDecCtx = nullptr;
    SwsContext*     swsCtx  = nullptr;
    int             swsInWidth = 0;
    int             swsInHeight = 0;
    int             swsOutWidth = 0;
    int             swsOutHeight = 0;
    AVFrame*        encFrame = nullptr;
    AVFrame*        decFrame = nullptr;

    struct MergeTrack {
        int index;
        AVCodecContext* decCtx;
        SwrContext* swrCtx;
        AVFrame* frame;
        std::deque<int16_t> buffer;

        // For voice isolation
        bool voiceIsolationEnabled = false;
        DenoiseState* denoiseState = nullptr;
        SwrContext* voiceIsolationSwrContext = nullptr;
        SwrContext* voiceIsolationBackSwrContext = nullptr;
        std::vector<float> voiceIsolationMonoBuffer;
        std::deque<int16_t> voiceIsolationSampleQueue;
        float volume{1.0f};
    };
    std::vector<MergeTrack> mergeTracks;

    // Tracks that need re-encoding individually (voice isolation without merge)
    struct IsolationTrack {
        int index;
        AVCodecContext* decCtx;
        AVCodecContext* encCtx;
        SwrContext* encSwrCtx;
        AVFrame* frame;
        std::deque<int16_t> buffer;

        bool voiceIsolationEnabled = false;
        DenoiseState* denoiseState = nullptr;
        SwrContext* voiceIsolationSwrContext = nullptr;
        SwrContext* voiceIsolationBackSwrContext = nullptr;
        std::vector<float> voiceIsolationMonoBuffer;
        std::deque<int16_t> voiceIsolationSampleQueue;
        float volume{1.0f};
        int encFrameSamples = 0;
        int outIndex = -1;
        int64_t pts = 0;
    };
    std::vector<IsolationTrack> isoTracks;

    AVCodecContext* aEncCtx = nullptr;
    int encFrameSamples = 0;
    std::vector<int16_t> mixBuffer;
    SwrContext* mixSwr = nullptr;
    bool headerWritten = false;

    bool needReencode = convertH264 || mergeAudio || anyVoiceIsolation;

    AVFormatContext* inputCtx = nullptr;
    if (avformat_open_input(&inputCtx, utf8Input.c_str(), nullptr, nullptr) < 0) {
        DebugLog("Failed to open input file", true);
        return false;
    }
    DebugLog("Input opened");
    if (avformat_find_stream_info(inputCtx, nullptr) < 0) {
        DebugLog("Failed to read stream info", true);
        avformat_close_input(&inputCtx);
        return false;
    }
    {
        std::ostringstream oss;
        oss << "Input streams=" << inputCtx->nb_streams;
        DebugLog(oss.str());
    }

    AVFormatContext* outputCtx = nullptr;
    if (avformat_alloc_output_context2(&outputCtx, nullptr, nullptr, utf8Output.c_str()) < 0) {
        DebugLog("Failed to allocate output context", true);
        avformat_close_input(&inputCtx);
        return false;
    }
    DebugLog("Output context allocated");

    std::vector<int> streamMapping(inputCtx->nb_streams, -1);
    int mergedAudioIndex = -1;
    for (unsigned i = 0; i < inputCtx->nb_streams; ++i) {
        AVStream* inStream = inputCtx->streams[i];
        bool useStream = (inStream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO && i == (unsigned)m_player->videoStreamIndex);
        if (!useStream && inStream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            useStream = std::find(activeTracks.begin(), activeTracks.end(), (int)i) != activeTracks.end();
        }
        if (!useStream)
            continue;

        AVStream* outStream = nullptr;
        if (needReencode && inStream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO && i == (unsigned)m_player->videoStreamIndex && convertH264) {
            const AVCodec* vEnc = nullptr;
            const bool useNvenc = encoder == EncoderSelection::Nvenc;
            const bool useAmf = encoder == EncoderSelection::Amf;
            if (useNvenc)
                vEnc = avcodec_find_encoder_by_name("h264_nvenc");
            else if (useAmf)
                vEnc = avcodec_find_encoder_by_name("h264_amf");
            else
                vEnc = avcodec_find_encoder(AV_CODEC_ID_H264);
            if (!vEnc) {
                DebugLog("H.264 encoder not found", true);
                avformat_free_context(outputCtx);
                avformat_close_input(&inputCtx);
                return false;
            }
            outStream = avformat_new_stream(outputCtx, vEnc);
            vEncCtx = avcodec_alloc_context3(vEnc);
            vEncCtx->codec_id = AV_CODEC_ID_H264;
            int outW = timelineHasCrop ? m_player->GetCropOutputWidth()
                                       : inStream->codecpar->width;
            int outH = timelineHasCrop ? m_player->GetCropOutputHeight()
                                       : inStream->codecpar->height;
            vEncCtx->width = outW;
            vEncCtx->height = outH;
            AVRational guessedFps = av_guess_frame_rate(inputCtx, inStream, nullptr);
            if (guessedFps.num > 0 && guessedFps.den > 0) {
                vEncCtx->framerate = guessedFps;
                vEncCtx->time_base = av_inv_q(guessedFps);
            } else {
                vEncCtx->time_base = inStream->time_base;
            }
            vEncCtx->pix_fmt = AV_PIX_FMT_YUV420P;
            vEncCtx->max_b_frames = 2;
            vEncCtx->gop_size = 12;
            if (maxBitrate > 0) {
                const int64_t targetBitrate = static_cast<int64_t>(maxBitrate) * 1000;
                vEncCtx->bit_rate = targetBitrate;
                if (useNvenc) {
                    // Enforce bitrate for NVENC with a 2s VBV to prevent under-shooting
                    vEncCtx->rc_max_rate = targetBitrate;
                    vEncCtx->rc_min_rate = targetBitrate;
                    vEncCtx->rc_buffer_size = targetBitrate * 2;
                    vEncCtx->rc_initial_buffer_occupancy = vEncCtx->rc_buffer_size * 3 / 4;
                    vEncCtx->bit_rate_tolerance = targetBitrate / 2;
                }
            }
            if (outputCtx->oformat->flags & AVFMT_GLOBALHEADER)
                vEncCtx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
            AVDictionary* encOpts = nullptr;
            if (useNvenc) {
                // Use fastest preset for NVENC as requested
                if (maxBitrate > 0) {
                    std::string br = std::to_string(maxBitrate) + "k";
                    av_dict_set(&encOpts, "b", br.c_str(), 0);
                    av_dict_set(&encOpts, "minrate", br.c_str(), 0);
                    av_dict_set(&encOpts, "maxrate", br.c_str(), 0);
                    std::string buf = std::to_string(maxBitrate * 2) + "k";
                    av_dict_set(&encOpts, "bufsize", buf.c_str(), 0);
                }
                av_dict_set(&encOpts, "preset", "p1", 0);
                av_dict_set(&encOpts, "rc", "cbr", 0);
                av_dict_set(&encOpts, "rc-lookahead", "0", 0);
                av_dict_set(&encOpts, "no-scenecut", "1", 0);
                av_dict_set(&encOpts, "b_adapt", "0", 0);
                av_dict_set(&encOpts, "forced-idr", "1", 0);
                av_dict_set(&encOpts, "spatial-aq", "0", 0);
                av_dict_set(&encOpts, "temporal-aq", "0", 0);
            } else if (useAmf) {
                av_dict_set(&encOpts, "usage", "transcoding", 0);
                av_dict_set(&encOpts, "quality", "speed", 0);
            } else {
                av_dict_set(&encOpts, "preset", "fast", 0);
            }
            if (avcodec_open2(vEncCtx, vEnc, &encOpts) < 0) {
                DebugLog("Failed to open H.264 encoder", true);
                avcodec_free_context(&vEncCtx);
                avformat_free_context(outputCtx);
                avformat_close_input(&inputCtx);
                av_dict_free(&encOpts);
                return false;
            }
            av_dict_free(&encOpts);
            if (avcodec_parameters_from_context(outStream->codecpar, vEncCtx) < 0) {
                DebugLog("Failed to copy encoder parameters", true);
                success = false;
                goto cleanup;
            }
            outStream->time_base = vEncCtx->time_base;
            vDecCtx = avcodec_alloc_context3(avcodec_find_decoder(inStream->codecpar->codec_id));
            if (!vDecCtx ||
                avcodec_parameters_to_context(vDecCtx, inStream->codecpar) < 0) {
                DebugLog("Failed to create video decoder context", true);
                avcodec_free_context(&vEncCtx);
                if (vDecCtx) avcodec_free_context(&vDecCtx);
                avformat_free_context(outputCtx);
                avformat_close_input(&inputCtx);
                return false;
            }
            if (avcodec_open2(vDecCtx, avcodec_find_decoder(inStream->codecpar->codec_id), nullptr) < 0) {
                DebugLog("Failed to open video decoder", true);
                avcodec_free_context(&vEncCtx);
                avcodec_free_context(&vDecCtx);
                avformat_free_context(outputCtx);
                avformat_close_input(&inputCtx);
                return false;
            }
            DebugLog("Video decoder/encoder initialized");
            swsCtx = nullptr; // initialized after first decoded frame
            encFrame = av_frame_alloc();
            decFrame = av_frame_alloc();
            if (!encFrame || !decFrame) {
                DebugLog("Failed to allocate frames", true);
                success = false;
                goto cleanup;
            }
            encFrame->format = vEncCtx->pix_fmt;
            encFrame->width = vEncCtx->width;
            encFrame->height = vEncCtx->height;
            if (av_frame_get_buffer(encFrame, 32) < 0) {
                DebugLog("Failed to allocate buffer for encoder frame", true);
                success = false;
                goto cleanup;
            }
        } else if (needReencode && inStream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            if (mergeAudio) {
                // We'll create a single output audio stream later
                MergeTrack mt{};
                mt.index = i;
                // Preserve the current track volume so export obeys UI settings
                for (const auto& at : m_player->audioTracks) {
                    if (at->streamIndex == i) {
                        mt.volume = at->volume;
                        break;
                    }
                }
                const AVCodec* dec = avcodec_find_decoder(inStream->codecpar->codec_id);
                mt.decCtx = avcodec_alloc_context3(dec);
                avcodec_parameters_to_context(mt.decCtx, inStream->codecpar);
                avcodec_open2(mt.decCtx, dec, nullptr);
                mt.swrCtx = swr_alloc();
                av_opt_set_int(mt.swrCtx, "in_sample_rate", mt.decCtx->sample_rate, 0);
                av_opt_set_int(mt.swrCtx, "out_sample_rate", 44100, 0);
                av_opt_set_sample_fmt(mt.swrCtx, "in_sample_fmt", mt.decCtx->sample_fmt, 0);
                av_opt_set_sample_fmt(mt.swrCtx, "out_sample_fmt", AV_SAMPLE_FMT_S16, 0);
                av_channel_layout_default(&mt.decCtx->ch_layout,
                                          mt.decCtx->ch_layout.nb_channels ?
                                              mt.decCtx->ch_layout.nb_channels : 2);
                AVChannelLayout out_ch{};
                av_channel_layout_default(&out_ch, 2);
                av_opt_set_chlayout(mt.swrCtx, "in_chlayout", &mt.decCtx->ch_layout, 0);
                av_opt_set_chlayout(mt.swrCtx, "out_chlayout", &out_ch, 0);
                swr_init(mt.swrCtx);
                mt.frame = av_frame_alloc();
                mergeTracks.push_back(mt);
                continue; // output stream created later
            } else {
                bool isolate = false;
                float vol = 1.0f;
                for (const auto& at : m_player->audioTracks) {
                    if (at->streamIndex == (int)i) {
                        isolate = at->voiceIsolationEnabled;
                        vol = at->volume;
                        break;
                    }
                }
                if (isolate) {
                    IsolationTrack it{};
                    it.index = i;
                    it.volume = vol;
                    const AVCodec* dec = avcodec_find_decoder(inStream->codecpar->codec_id);
                    it.decCtx = avcodec_alloc_context3(dec);
                    avcodec_parameters_to_context(it.decCtx, inStream->codecpar);
                    avcodec_open2(it.decCtx, dec, nullptr);
                    it.frame = av_frame_alloc();
                    it.voiceIsolationEnabled = true;
                    it.denoiseState = rnnoise_create(nullptr);
                    it.voiceIsolationSwrContext = swr_alloc();
                    av_opt_set_int(it.voiceIsolationSwrContext, "in_sample_rate", it.decCtx->sample_rate, 0);
                    av_opt_set_sample_fmt(it.voiceIsolationSwrContext, "in_sample_fmt", it.decCtx->sample_fmt, 0);
                    av_opt_set_chlayout(it.voiceIsolationSwrContext, "in_chlayout", &it.decCtx->ch_layout, 0);
                    av_opt_set_int(it.voiceIsolationSwrContext, "out_sample_rate", 48000, 0);
                    av_opt_set_sample_fmt(it.voiceIsolationSwrContext, "out_sample_fmt", AV_SAMPLE_FMT_S16, 0);
                    AVChannelLayout mono_layout;
                    av_channel_layout_default(&mono_layout, 1);
                    av_opt_set_chlayout(it.voiceIsolationSwrContext, "out_chlayout", &mono_layout, 0);
                    swr_init(it.voiceIsolationSwrContext);

                    it.voiceIsolationBackSwrContext = swr_alloc();
                    av_opt_set_int(it.voiceIsolationBackSwrContext, "in_sample_rate", 48000, 0);
                    av_opt_set_sample_fmt(it.voiceIsolationBackSwrContext, "in_sample_fmt", AV_SAMPLE_FMT_S16, 0);
                    av_opt_set_chlayout(it.voiceIsolationBackSwrContext, "in_chlayout", &mono_layout, 0);
                    av_opt_set_int(it.voiceIsolationBackSwrContext, "out_sample_rate", 44100, 0);
                    av_opt_set_sample_fmt(it.voiceIsolationBackSwrContext, "out_sample_fmt", AV_SAMPLE_FMT_S16, 0);
                    AVChannelLayout stereo_layout;
                    av_channel_layout_default(&stereo_layout, 2);
                    av_opt_set_chlayout(it.voiceIsolationBackSwrContext, "out_chlayout", &stereo_layout, 0);
                    swr_init(it.voiceIsolationBackSwrContext);

                    const AVCodec* aEnc = avcodec_find_encoder(AV_CODEC_ID_AAC);
                    if (!aEnc) {
                        DebugLog("AAC encoder not found", true);
                        avformat_free_context(outputCtx);
                        avformat_close_input(&inputCtx);
                        return false;
                    }
                    outStream = avformat_new_stream(outputCtx, aEnc);
                    it.encCtx = avcodec_alloc_context3(aEnc);
                    it.encCtx->sample_rate = 44100;
                    av_channel_layout_default(&it.encCtx->ch_layout, 2);
#pragma warning(push)
#pragma warning(disable: 4996) // Suppress deprecation warning for sample_fmts
                    it.encCtx->sample_fmt = aEnc->sample_fmts ? aEnc->sample_fmts[0] : AV_SAMPLE_FMT_FLTP;
#pragma warning(pop)
                    it.encCtx->time_base = {1, it.encCtx->sample_rate};
                    it.encCtx->bit_rate = 128000;
                    if (avcodec_open2(it.encCtx, aEnc, nullptr) < 0) {
                        DebugLog("Failed to open AAC encoder", true);
                        avcodec_free_context(&it.encCtx);
                        avformat_free_context(outputCtx);
                        avformat_close_input(&inputCtx);
                        return false;
                    }
                    if (avcodec_parameters_from_context(outStream->codecpar, it.encCtx) < 0) {
                        DebugLog("Failed to copy AAC encoder parameters", true);
                        avcodec_free_context(&it.encCtx);
                        avformat_free_context(outputCtx);
                        avformat_close_input(&inputCtx);
                        return false;
                    }
                    outStream->time_base = it.encCtx->time_base;
                    it.encFrameSamples = it.encCtx->frame_size > 0 ? it.encCtx->frame_size : 1024;
                    it.encSwrCtx = swr_alloc();
                    AVChannelLayout stereo;
                    av_channel_layout_default(&stereo, 2);
                    av_opt_set_int   (it.encSwrCtx, "in_sample_rate", 44100, 0);
                    av_opt_set_sample_fmt(it.encSwrCtx, "in_sample_fmt", AV_SAMPLE_FMT_S16, 0);
                    av_opt_set_chlayout  (it.encSwrCtx, "in_chlayout", &stereo, 0);
                    av_opt_set_int   (it.encSwrCtx, "out_sample_rate", it.encCtx->sample_rate, 0);
                    av_opt_set_sample_fmt(it.encSwrCtx, "out_sample_fmt", it.encCtx->sample_fmt, 0);
                    av_opt_set_chlayout  (it.encSwrCtx, "out_chlayout", &it.encCtx->ch_layout, 0);
                    if (swr_init(it.encSwrCtx) < 0) {
                        DebugLog("Failed to init isolation resampler", true);
                        swr_free(&it.encSwrCtx);
                        avcodec_free_context(&it.encCtx);
                        avformat_free_context(outputCtx);
                        avformat_close_input(&inputCtx);
                        return false;
                    }
                    it.outIndex = outStream->index;
                    isoTracks.push_back(it);
                    streamMapping[i] = it.outIndex;
                    continue; // done for this stream
                }
                // If this track doesn't require isolation, just copy it
                outStream = avformat_new_stream(outputCtx, nullptr);
                if (avcodec_parameters_copy(outStream->codecpar, inStream->codecpar) < 0) {
                    DebugLog("Failed to copy codec parameters", true);
                    avformat_free_context(outputCtx);
                    avformat_close_input(&inputCtx);
                    return false;
                }
                outStream->codecpar->codec_tag = 0;
                outStream->time_base = inStream->time_base;
            }
            // fall through to stream copy if no isolation processing required
        } else {
            outStream = avformat_new_stream(outputCtx, nullptr);
            if (avcodec_parameters_copy(outStream->codecpar, inStream->codecpar) < 0) {
                DebugLog("Failed to copy codec parameters", true);
                avformat_free_context(outputCtx);
                avformat_close_input(&inputCtx);
                return false;
            }
            outStream->codecpar->codec_tag = 0;
            outStream->time_base = inStream->time_base;
        }
        streamMapping[i] = outStream ? outStream->index : -1;
    }

    for (auto& mt : mergeTracks) {
        for (const auto& playerTrack : m_player->audioTracks) {
            if (playerTrack->streamIndex == mt.index) {
                mt.voiceIsolationEnabled = playerTrack->voiceIsolationEnabled;
                if (mt.voiceIsolationEnabled) {
                    mt.denoiseState = rnnoise_create(nullptr);
                    // Initialize resamplers for RNNoise (to 48kHz mono)
                    mt.voiceIsolationSwrContext = swr_alloc();
                    av_opt_set_int(mt.voiceIsolationSwrContext, "in_sample_rate", mt.decCtx->sample_rate, 0);
                    av_opt_set_sample_fmt(mt.voiceIsolationSwrContext, "in_sample_fmt", mt.decCtx->sample_fmt, 0);
                    av_opt_set_chlayout(mt.voiceIsolationSwrContext, "in_chlayout", &mt.decCtx->ch_layout, 0);
                    av_opt_set_int(mt.voiceIsolationSwrContext, "out_sample_rate", 48000, 0);
                    av_opt_set_sample_fmt(mt.voiceIsolationSwrContext, "out_sample_fmt", AV_SAMPLE_FMT_S16, 0);
                    AVChannelLayout mono_layout;
                    av_channel_layout_default(&mono_layout, 1);
                    av_opt_set_chlayout(mt.voiceIsolationSwrContext, "out_chlayout", &mono_layout, 0);
                    swr_init(mt.voiceIsolationSwrContext);

                    // Initialize resampler back to final mix format
                    mt.voiceIsolationBackSwrContext = swr_alloc();
                    av_opt_set_int(mt.voiceIsolationBackSwrContext, "in_sample_rate", 48000, 0);
                    av_opt_set_sample_fmt(mt.voiceIsolationBackSwrContext, "in_sample_fmt", AV_SAMPLE_FMT_S16, 0);
                    av_opt_set_chlayout(mt.voiceIsolationBackSwrContext, "in_chlayout", &mono_layout, 0);
                    av_opt_set_int(mt.voiceIsolationBackSwrContext, "out_sample_rate", 44100, 0);
                    av_opt_set_sample_fmt(mt.voiceIsolationBackSwrContext, "out_sample_fmt", AV_SAMPLE_FMT_S16, 0);
                    AVChannelLayout stereo_layout;
                    av_channel_layout_default(&stereo_layout, 2);
                    av_opt_set_chlayout(mt.voiceIsolationBackSwrContext, "out_chlayout", &stereo_layout, 0);
                    swr_init(mt.voiceIsolationBackSwrContext);
                }
                break;
            }
        }
    }

    if (mergeAudio && !mergeTracks.empty()) {
        const AVCodec* aEnc = avcodec_find_encoder(AV_CODEC_ID_AAC);
        if (!aEnc) {
            DebugLog("AAC encoder not found", true);
            avformat_free_context(outputCtx);
            avformat_close_input(&inputCtx);
            return false;
        }
        AVStream* aOut = avformat_new_stream(outputCtx, aEnc);
        aEncCtx = avcodec_alloc_context3(aEnc);
        if (!aEncCtx) {
            DebugLog("Failed to allocate AAC encoder context", true);
            avformat_free_context(outputCtx);
            avformat_close_input(&inputCtx);
            return false;
        }
        aEncCtx->sample_rate = 44100;
        av_channel_layout_default(&aEncCtx->ch_layout, 2);
#pragma warning(push)
#pragma warning(disable: 4996) // Suppress deprecation warning for sample_fmts
        aEncCtx->sample_fmt = aEnc->sample_fmts ? aEnc->sample_fmts[0] : AV_SAMPLE_FMT_FLTP;
#pragma warning(pop)
        aEncCtx->time_base = {1, aEncCtx->sample_rate};
        aEncCtx->bit_rate = 128000; // match ffmpeg default
        if (avcodec_open2(aEncCtx, aEnc, nullptr) < 0) {
            DebugLog("Failed to open AAC encoder", true);
            avcodec_free_context(&aEncCtx);
            avformat_free_context(outputCtx);
            avformat_close_input(&inputCtx);
            return false;
        }
        DebugLog("AAC encoder initialized");
        if (avcodec_parameters_from_context(aOut->codecpar, aEncCtx) < 0) {
            DebugLog("Failed to copy AAC encoder parameters", true);
            success = false;
            goto cleanup;
        }
        aOut->time_base = aEncCtx->time_base;
        encFrameSamples = aEncCtx->frame_size > 0 ? aEncCtx->frame_size : 1024;
        if (aEncCtx->ch_layout.nb_channels <= 0) {
            DebugLog("Invalid channel count in AAC encoder context", true);
            success = false;
            goto cleanup;
        }
        mixBuffer.resize(encFrameSamples * aEncCtx->ch_layout.nb_channels);
        mixSwr = swr_alloc();
        AVChannelLayout stereo;
        av_channel_layout_default(&stereo, 2);
        av_opt_set_int   (mixSwr, "in_sample_rate", 44100, 0);
        av_opt_set_sample_fmt(mixSwr, "in_sample_fmt", AV_SAMPLE_FMT_S16, 0);
        av_opt_set_chlayout  (mixSwr, "in_chlayout", &stereo, 0);
        av_opt_set_int   (mixSwr, "out_sample_rate", aEncCtx->sample_rate, 0);
        av_opt_set_sample_fmt(mixSwr, "out_sample_fmt", aEncCtx->sample_fmt, 0);
        av_opt_set_chlayout  (mixSwr, "out_chlayout", &aEncCtx->ch_layout, 0);
        if (swr_init(mixSwr) < 0) {
            DebugLog("Failed to init mix resampler", true);
            success = false;
            goto cleanup;
        }
        mergedAudioIndex = aOut->index;
    }

    if (!(outputCtx->oformat->flags & AVFMT_NOFILE)) {
        if (avio_open(&outputCtx->pb, utf8Output.c_str(), AVIO_FLAG_WRITE) < 0) {
            DebugLog("Could not open output file", true);
            avformat_free_context(outputCtx);
            avformat_close_input(&inputCtx);
            return false;
        }
    }

    if (avformat_write_header(outputCtx, nullptr) < 0) {
        DebugLog("Failed to write header", true);
        if (!(outputCtx->oformat->flags & AVFMT_NOFILE))
            avio_closep(&outputCtx->pb);
        avformat_free_context(outputCtx);
        avformat_close_input(&inputCtx);
        return false;
    }
    DebugLog("Header written");
    headerWritten = true;
    DebugLog("Beginning packet processing");

    int64_t startPts = (int64_t)(startTime * AV_TIME_BASE);
    int64_t endPts = (int64_t)(endTime * AV_TIME_BASE);
    if (av_seek_frame(inputCtx, -1, startPts, AVSEEK_FLAG_BACKWARD) < 0) {
        DebugLog("Seek failed", true);
    }

    AVPacket pkt, outPkt;
#pragma warning(push)
#pragma warning(disable: 4996) // Suppress deprecation warning for av_init_packet
    av_init_packet(&pkt);
    av_init_packet(&outPkt); // ensure fields are zeroed before use
#pragma warning(pop)
    int64_t audioPts = 0;
    while (av_read_frame(inputCtx, &pkt) >= 0) {
        if (cancelFlag && *cancelFlag) { success = false; goto cleanup; }
        bool handled = false;
        AVStream* inStream = inputCtx->streams[pkt.stream_index];
        int64_t pktPtsUs = av_rescale_q(pkt.pts, inStream->time_base, AV_TIME_BASE_Q);
        if (pktPtsUs < startPts) { av_packet_unref(&pkt); continue; }
        if (pktPtsUs > endPts) { av_packet_unref(&pkt); break; }

        if (convertH264 && pkt.stream_index == m_player->videoStreamIndex) {
            avcodec_send_packet(vDecCtx, &pkt);
            while (avcodec_receive_frame(vDecCtx, decFrame) == 0) {
                if (timelineHasCrop)
                {
                    double rawTime = 0.0;
                    if (decFrame->best_effort_timestamp != AV_NOPTS_VALUE)
                        rawTime = decFrame->best_effort_timestamp * av_q2d(inStream->time_base);
                    else if (decFrame->pts != AV_NOPTS_VALUE)
                        rawTime = decFrame->pts * av_q2d(inStream->time_base);
                    else
                        rawTime = pktPtsUs / (double)AV_TIME_BASE;

                    double frameTime = rawTime - m_player->startTimeOffset;
                    if (frameTime < 0.0)
                        frameTime = 0.0;

                    RECT frameCrop;
                    bool haveCrop = m_player->GetCropRectForTime(frameTime, frameCrop);
                    if (haveCrop)
                    {
                        decFrame->crop_left = frameCrop.left;
                        decFrame->crop_top = frameCrop.top;
                        decFrame->crop_right = vDecCtx->width - frameCrop.right;
                        decFrame->crop_bottom = vDecCtx->height - frameCrop.bottom;
                        av_frame_apply_cropping(decFrame, 0);
                    }
                }
                double widthScale = static_cast<double>(vEncCtx->width) / std::max(1, decFrame->width);
                double heightScale = static_cast<double>(vEncCtx->height) / std::max(1, decFrame->height);
                double scale = std::min(widthScale, heightScale);
                int scaledWidth = std::max(1, static_cast<int>(std::lround(decFrame->width * scale)));
                int scaledHeight = std::max(1, static_cast<int>(std::lround(decFrame->height * scale)));
                if (scaledWidth > vEncCtx->width)
                    scaledWidth = vEncCtx->width;
                if (scaledHeight > vEncCtx->height)
                    scaledHeight = vEncCtx->height;

                if (!swsCtx || decFrame->width != swsInWidth || decFrame->height != swsInHeight ||
                    scaledWidth != swsOutWidth || scaledHeight != swsOutHeight) {
                    if (swsCtx)
                        sws_freeContext(swsCtx);
                    swsCtx = sws_getContext(decFrame->width, decFrame->height,
                                            (AVPixelFormat)decFrame->format,
                                            scaledWidth, scaledHeight,
                                            vEncCtx->pix_fmt, SWS_BILINEAR,
                                            nullptr, nullptr, nullptr);
                    if (!swsCtx) {
                        DebugLog("Failed to create scaling context", true);
                        av_packet_unref(&pkt);
                        success = false;
                        goto cleanup;
                    }
                    swsInWidth = decFrame->width;
                    swsInHeight = decFrame->height;
                    swsOutWidth = scaledWidth;
                    swsOutHeight = scaledHeight;
                }

                if (av_frame_make_writable(encFrame) < 0) {
                    DebugLog("Failed to make encoder frame writable", true);
                    av_packet_unref(&pkt);
                    success = false;
                    goto cleanup;
                }

                FillBlackYuv420(encFrame);

                int offsetX = (vEncCtx->width - scaledWidth) / 2;
                int offsetY = (vEncCtx->height - scaledHeight) / 2;
                uint8_t* dstData[4] = { encFrame->data[0], encFrame->data[1], encFrame->data[2], encFrame->data[3] };
                int dstLinesize[4] = { encFrame->linesize[0], encFrame->linesize[1], encFrame->linesize[2], encFrame->linesize[3] };
                dstData[0] += offsetY * dstLinesize[0] + offsetX;
                int chromaOffsetX = offsetX / 2;
                int chromaOffsetY = offsetY / 2;
                if (dstData[1])
                    dstData[1] += chromaOffsetY * dstLinesize[1] + chromaOffsetX;
                if (dstData[2])
                    dstData[2] += chromaOffsetY * dstLinesize[2] + chromaOffsetX;

                sws_scale(swsCtx, decFrame->data, decFrame->linesize, 0, decFrame->height, dstData, dstLinesize);
                encFrame->pts = av_rescale_q(decFrame->pts - av_rescale_q(startPts, AV_TIME_BASE_Q, inStream->time_base), inStream->time_base, vEncCtx->time_base);
                avcodec_send_frame(vEncCtx, encFrame);
                while (avcodec_receive_packet(vEncCtx, &outPkt) == 0) {
                    av_packet_rescale_ts(&outPkt, vEncCtx->time_base, outputCtx->streams[streamMapping[pkt.stream_index]]->time_base);
                    outPkt.stream_index = streamMapping[pkt.stream_index];
                    av_interleaved_write_frame(outputCtx, &outPkt);
                    av_packet_unref(&outPkt);
                }
                av_frame_unref(decFrame);
            }
            handled = true;
        } else if (mergeAudio) {
            for (auto &mt : mergeTracks) {
                if (mt.index == pkt.stream_index) {
                    avcodec_send_packet(mt.decCtx, &pkt);
                    while (avcodec_receive_frame(mt.decCtx, mt.frame) == 0) {
                        if (mt.voiceIsolationEnabled && mt.denoiseState) {
                            // Resample to 48kHz mono for RNNoise
                            int guess = swr_get_out_samples(mt.voiceIsolationSwrContext, mt.frame->nb_samples);
                            std::vector<int16_t> mono_tmp(std::max(guess, mt.frame->nb_samples));
                            uint8_t* out_ptr = reinterpret_cast<uint8_t*>(mono_tmp.data());
                            int got = swr_convert(mt.voiceIsolationSwrContext, &out_ptr, (int)mono_tmp.size(), (const uint8_t**)mt.frame->data, mt.frame->nb_samples);
                            if (got < 0) got = 0;

                            // Accumulate into 48k mono queue and process exact 480-sample frames
                            for (int n = 0; n < got; ++n) mt.voiceIsolationSampleQueue.push_back(mono_tmp[n]);

                            std::vector<int16_t> processed_mono;
                            while ((int)mt.voiceIsolationSampleQueue.size() >= RNNOISE_FRAME_SIZE) {
                                // Fill RNNoise frame
                                mt.voiceIsolationMonoBuffer.resize(RNNOISE_FRAME_SIZE);
                                for (int j = 0; j < RNNOISE_FRAME_SIZE; ++j) {
                                    int16_t s = mt.voiceIsolationSampleQueue.front();
                                    mt.voiceIsolationSampleQueue.pop_front();
                                    mt.voiceIsolationMonoBuffer[j] = static_cast<float>(s);
                                }
                                // Denoise in place
                                rnnoise_process_frame(mt.denoiseState, mt.voiceIsolationMonoBuffer.data(), mt.voiceIsolationMonoBuffer.data());
                                // Back to int16_t scale
                                for (int j = 0; j < RNNOISE_FRAME_SIZE; ++j) {
                                    float v = mt.voiceIsolationMonoBuffer[j];
                                    if (v > 32767.0f) v = 32767.0f;
                                    if (v < -32768.0f) v = -32768.0f;
                                    processed_mono.push_back(static_cast<int16_t>(v));
                                }
                            }

                            // Resample processed mono to final mix format (44.1kHz Stereo S16)
                            if (!processed_mono.empty()) {
                                int back_guess = swr_get_out_samples(mt.voiceIsolationBackSwrContext, (int)processed_mono.size());
                                std::vector<int16_t> processed_stereo(std::max(back_guess, (int)processed_mono.size()) * 2);
                                uint8_t* back_out_ptr = reinterpret_cast<uint8_t*>(processed_stereo.data());
                                const uint8_t* in_ptr = reinterpret_cast<const uint8_t*>(processed_mono.data());
                                int back_got = swr_convert(mt.voiceIsolationBackSwrContext, &back_out_ptr, (int)processed_stereo.size() / 2, &in_ptr, (int)processed_mono.size());
                                if (back_got > 0) {
                                    mt.buffer.insert(mt.buffer.end(), processed_stereo.begin(), processed_stereo.begin() + back_got * 2);
                                }
                            }
                        } else {
                            // Standard resampling to mix format
                            int outSamples = swr_get_out_samples(mt.swrCtx, mt.frame->nb_samples);
                            std::vector<int16_t> tmp(outSamples * 2);
                            uint8_t* outArr[1] = { reinterpret_cast<uint8_t*>(tmp.data()) };
                            int conv = swr_convert(mt.swrCtx, outArr, outSamples,
                                                  (const uint8_t**)mt.frame->data,
                                                  mt.frame->nb_samples);
                            mt.buffer.insert(mt.buffer.end(), tmp.begin(),
                                              tmp.begin() + conv * 2);
                        }
                    }
                    handled = true;
                    break;
                }
            }
        } else if (!isoTracks.empty()) {
            for (auto &it : isoTracks) {
                if (it.index == pkt.stream_index) {
                    avcodec_send_packet(it.decCtx, &pkt);
                    while (avcodec_receive_frame(it.decCtx, it.frame) == 0) {
                        // Resample to 48kHz mono for RNNoise
                        int guess = swr_get_out_samples(it.voiceIsolationSwrContext, it.frame->nb_samples);
                        std::vector<int16_t> mono_tmp(std::max(guess, it.frame->nb_samples));
                        uint8_t* out_ptr = reinterpret_cast<uint8_t*>(mono_tmp.data());
                        int got = swr_convert(it.voiceIsolationSwrContext, &out_ptr, (int)mono_tmp.size(), (const uint8_t**)it.frame->data, it.frame->nb_samples);
                        if (got < 0) got = 0;
                        for (int n = 0; n < got; ++n) it.voiceIsolationSampleQueue.push_back(mono_tmp[n]);

                        std::vector<int16_t> processed_mono;
                        while ((int)it.voiceIsolationSampleQueue.size() >= RNNOISE_FRAME_SIZE) {
                            it.voiceIsolationMonoBuffer.resize(RNNOISE_FRAME_SIZE);
                            for (int j = 0; j < RNNOISE_FRAME_SIZE; ++j) {
                                int16_t s = it.voiceIsolationSampleQueue.front();
                                it.voiceIsolationSampleQueue.pop_front();
                                it.voiceIsolationMonoBuffer[j] = static_cast<float>(s);
                            }
                            rnnoise_process_frame(it.denoiseState, it.voiceIsolationMonoBuffer.data(), it.voiceIsolationMonoBuffer.data());
                            for (int j = 0; j < RNNOISE_FRAME_SIZE; ++j) {
                                float v = it.voiceIsolationMonoBuffer[j];
                                if (v > 32767.0f) v = 32767.0f;
                                if (v < -32768.0f) v = -32768.0f;
                                processed_mono.push_back(static_cast<int16_t>(v));
                            }
                        }

                        if (!processed_mono.empty()) {
                            int back_guess = swr_get_out_samples(it.voiceIsolationBackSwrContext, (int)processed_mono.size());
                            std::vector<int16_t> processed_stereo(std::max(back_guess, (int)processed_mono.size()) * 2);
                            uint8_t* back_out_ptr = reinterpret_cast<uint8_t*>(processed_stereo.data());
                            const uint8_t* in_ptr = reinterpret_cast<const uint8_t*>(processed_mono.data());
                            int back_got = swr_convert(it.voiceIsolationBackSwrContext, &back_out_ptr, (int)processed_stereo.size() / 2, &in_ptr, (int)processed_mono.size());
                            if (back_got > 0) {
                                for (int j = 0; j < back_got * 2; ++j) {
                                    int val = static_cast<int>(processed_stereo[j] * it.volume);
                                    if (val > 32767) val = 32767;
                                    if (val < -32768) val = -32768;
                                    it.buffer.push_back(static_cast<int16_t>(val));
                                }
                            }
                        }
                    }
                    handled = true;
                    break;
                }
            }
        }

        if (!handled) {
            if (pkt.stream_index >= (int)streamMapping.size() || streamMapping[pkt.stream_index] < 0) {
                av_packet_unref(&pkt);
                continue;
            }
            AVStream* outStream = outputCtx->streams[streamMapping[pkt.stream_index]];
            pkt.pts = av_rescale_q(pkt.pts - av_rescale_q(startPts, AV_TIME_BASE_Q, inStream->time_base), inStream->time_base, outStream->time_base);
            pkt.dts = av_rescale_q(pkt.dts - av_rescale_q(startPts, AV_TIME_BASE_Q, inStream->time_base), inStream->time_base, outStream->time_base);
            if (pkt.duration > 0)
                pkt.duration = av_rescale_q(pkt.duration, inStream->time_base, outStream->time_base);
            pkt.pos = -1;
            pkt.stream_index = outStream->index;
            av_interleaved_write_frame(outputCtx, &pkt);
        }

        av_packet_unref(&pkt);

        // check if we can encode audio frame
        if (mergeAudio && !mergeTracks.empty()) {
            bool ready = true;
            for (auto &mt : mergeTracks)
                if ((int)mt.buffer.size() < encFrameSamples * 2) { ready = false; break; }
            if (ready) {
                for (int i = 0; i < encFrameSamples * 2; ++i) {
                    int sum = 0;
                    for (auto &mt : mergeTracks) {
                        sum += static_cast<int>(mt.buffer.front() * mt.volume);
                        mt.buffer.pop_front();
                    }
                    if (sum > 32767) sum = 32767;
                    if (sum < -32768) sum = -32768;
                    mixBuffer[i] = static_cast<int16_t>(sum);
                }
                AVFrame* af = av_frame_alloc();
                af->nb_samples = encFrameSamples;
                av_channel_layout_copy(&af->ch_layout, &aEncCtx->ch_layout);
                af->format = aEncCtx->sample_fmt;
                af->sample_rate = aEncCtx->sample_rate;
                if (av_frame_get_buffer(af, 0) < 0) {
                    DebugLog("Failed to allocate audio frame buffer", true);
                    av_frame_free(&af);
                    success = false;
                    goto cleanup;
                }
                const uint8_t* inBuf[1] = { (const uint8_t*)mixBuffer.data() };
                if (swr_convert(mixSwr, af->data, encFrameSamples, inBuf, encFrameSamples) < 0) {
                    DebugLog("Failed to convert mixed samples", true);
                    av_frame_free(&af);
                    success = false;
                    goto cleanup;
                }
                af->pts = audioPts;
                audioPts += encFrameSamples;
                avcodec_send_frame(aEncCtx, af);
                while (avcodec_receive_packet(aEncCtx, &outPkt) == 0) {
                    av_packet_rescale_ts(&outPkt, aEncCtx->time_base, outputCtx->streams[mergedAudioIndex]->time_base);
                    outPkt.stream_index = mergedAudioIndex;
                    av_interleaved_write_frame(outputCtx, &outPkt);
                    av_packet_unref(&outPkt);
                }
                av_frame_free(&af);
            }
        } else if (!mergeAudio && !isoTracks.empty()) {
            for (auto &it : isoTracks) {
                while ((int)it.buffer.size() >= it.encFrameSamples * 2) {
                    AVFrame* af = av_frame_alloc();
                    af->nb_samples = it.encFrameSamples;
                    av_channel_layout_copy(&af->ch_layout, &it.encCtx->ch_layout);
                    af->format = it.encCtx->sample_fmt;
                    af->sample_rate = it.encCtx->sample_rate;
                    if (av_frame_get_buffer(af, 0) < 0) {
                        DebugLog("Failed to allocate audio frame buffer", true);
                        av_frame_free(&af);
                        success = false;
                        goto cleanup;
                    }
                    std::vector<int16_t> tmp(it.encFrameSamples * 2);
                    for (int j = 0; j < it.encFrameSamples * 2; ++j) {
                        tmp[j] = it.buffer.front();
                        it.buffer.pop_front();
                    }
                    const uint8_t* inBuf[1] = { (const uint8_t*)tmp.data() };
                    if (swr_convert(it.encSwrCtx, af->data, it.encFrameSamples, inBuf, it.encFrameSamples) < 0) {
                        DebugLog("Failed to convert isolation samples", true);
                        av_frame_free(&af);
                        success = false;
                        goto cleanup;
                    }
                    af->pts = it.pts;
                    it.pts += it.encFrameSamples;
                    avcodec_send_frame(it.encCtx, af);
                    while (avcodec_receive_packet(it.encCtx, &outPkt) == 0) {
                        av_packet_rescale_ts(&outPkt, it.encCtx->time_base, outputCtx->streams[it.outIndex]->time_base);
                        outPkt.stream_index = it.outIndex;
                        av_interleaved_write_frame(outputCtx, &outPkt);
                        av_packet_unref(&outPkt);
                    }
                    av_frame_free(&af);
                }
            }
        }

        double progress = (pktPtsUs - startPts) / double(endPts - startPts);
        progress = std::max(0.0, std::min(1.0, progress));
        int progressPercent = (int)(progress * 100.0);
        
        if (progressBar && IsWindow(progressBar)) {
            // Only update if progress changed and throttle updates
            auto now = std::chrono::high_resolution_clock::now();
            auto timeSinceLastUpdate = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_lastUpdateTime).count();
            
            // Update if: progress increased significantly OR 100ms has passed
            if (progressPercent > m_lastDisplayedPercent || timeSinceLastUpdate > 100) {
                // Apply smoothing: only allow incremental increases
                if (progressPercent > m_lastDisplayedPercent) {
                    m_lastDisplayedPercent = progressPercent;
                    m_lastUpdateTime = now;
                    
                    SendMessage(progressBar, PBM_SETPOS, progressPercent, 0);
                    
                    // Update percentage text
                    if (g_hProgressPercentage && IsWindow(g_hProgressPercentage)) {
                        wchar_t percentText[32];
                        swprintf_s(percentText, L"%d%%", progressPercent);
                        SetWindowTextW(g_hProgressPercentage, percentText);
                    }
                } else if (timeSinceLastUpdate > 100) {
                    // Force update after timeout to keep UI responsive
                    m_lastUpdateTime = now;
                    SendMessage(progressBar, PBM_SETPOS, progressPercent, 0);
                }
            }
        }
    }

    // Flush encoders
    DebugLog("Flushing encoders");
    if (convertH264 && vEncCtx) {
        avcodec_send_frame(vEncCtx, nullptr);
        while (avcodec_receive_packet(vEncCtx, &outPkt) == 0) {
            av_packet_rescale_ts(&outPkt, vEncCtx->time_base, outputCtx->streams[streamMapping[m_player->videoStreamIndex]]->time_base);
            outPkt.stream_index = streamMapping[m_player->videoStreamIndex];
            av_interleaved_write_frame(outputCtx, &outPkt);
            av_packet_unref(&outPkt);
        }
    }
    if (mergeAudio && aEncCtx) {
        // flush remaining samples
        // First, for each track with RNNoise, flush any residual 48k mono samples
        for (auto &mt : mergeTracks) {
            if (mt.voiceIsolationEnabled && mt.denoiseState && !mt.voiceIsolationSampleQueue.empty()) {
                // Pad to full frame with zeros
                std::vector<int16_t> processed_mono;
                while (!mt.voiceIsolationSampleQueue.empty()) {
                    mt.voiceIsolationMonoBuffer.resize(RNNOISE_FRAME_SIZE, 0.0f);
                    int take = std::min((int)mt.voiceIsolationSampleQueue.size(), RNNOISE_FRAME_SIZE);
                    for (int j = 0; j < take; ++j) {
                        int16_t s = mt.voiceIsolationSampleQueue.front();
                        mt.voiceIsolationSampleQueue.pop_front();
                        mt.voiceIsolationMonoBuffer[j] = static_cast<float>(s);
                    }
                    // Denoise
                    rnnoise_process_frame(mt.denoiseState, mt.voiceIsolationMonoBuffer.data(), mt.voiceIsolationMonoBuffer.data());
                    for (int j = 0; j < RNNOISE_FRAME_SIZE; ++j) {
                        float v = mt.voiceIsolationMonoBuffer[j];
                        if (v > 32767.0f) v = 32767.0f;
                        if (v < -32768.0f) v = -32768.0f;
                        processed_mono.push_back(static_cast<int16_t>(v));
                    }
                }
                if (!processed_mono.empty()) {
                    int back_guess = swr_get_out_samples(mt.voiceIsolationBackSwrContext, (int)processed_mono.size());
                    std::vector<int16_t> processed_stereo(std::max(back_guess, (int)processed_mono.size()) * 2);
                    uint8_t* back_out_ptr = reinterpret_cast<uint8_t*>(processed_stereo.data());
                    const uint8_t* in_ptr = reinterpret_cast<const uint8_t*>(processed_mono.data());
                    int back_got = swr_convert(mt.voiceIsolationBackSwrContext, &back_out_ptr, (int)processed_stereo.size() / 2, &in_ptr, (int)processed_mono.size());
                    if (back_got > 0) {
                        mt.buffer.insert(mt.buffer.end(), processed_stereo.begin(), processed_stereo.begin() + back_got * 2);
                    }
                }
            }
        }

        while (true) {
            if (cancelFlag && *cancelFlag) { success = false; goto cleanup; }
            bool ready = true;
            for (auto &mt : mergeTracks)
                if ((int)mt.buffer.size() < encFrameSamples * 2) { ready = false; break; }
            if (!ready) break;
            for (int i = 0; i < encFrameSamples * 2; ++i) {
                int sum = 0;
                for (auto &mt : mergeTracks) {
                    sum += static_cast<int>(mt.buffer.front() * mt.volume);
                    mt.buffer.pop_front();
                }
                if (sum > 32767) sum = 32767;
                if (sum < -32768) sum = -32768;
                mixBuffer[i] = static_cast<int16_t>(sum);
            }
            AVFrame* af = av_frame_alloc();
            af->nb_samples = encFrameSamples;
            av_channel_layout_copy(&af->ch_layout, &aEncCtx->ch_layout);
            af->format = aEncCtx->sample_fmt;
            af->sample_rate = aEncCtx->sample_rate;
            if (av_frame_get_buffer(af, 0) < 0) {
                DebugLog("Failed to allocate audio frame buffer", true);
                av_frame_free(&af);
                success = false;
                goto cleanup;
            }
            const uint8_t* inBuf[1] = { (const uint8_t*)mixBuffer.data() };
            if (swr_convert(mixSwr, af->data, encFrameSamples, inBuf, encFrameSamples) < 0) {
                DebugLog("Failed to convert mixed samples", true);
                av_frame_free(&af);
                success = false;
                goto cleanup;
            }
            af->pts = audioPts;
            audioPts += encFrameSamples;
            avcodec_send_frame(aEncCtx, af);
            while (avcodec_receive_packet(aEncCtx, &outPkt) == 0) {
                av_packet_rescale_ts(&outPkt, aEncCtx->time_base, outputCtx->streams[mergedAudioIndex]->time_base);
                outPkt.stream_index = mergedAudioIndex;
                av_interleaved_write_frame(outputCtx, &outPkt);
                av_packet_unref(&outPkt);
            }
            av_frame_free(&af);
        }
        avcodec_send_frame(aEncCtx, nullptr);
        while (avcodec_receive_packet(aEncCtx, &outPkt) == 0) {
            av_packet_rescale_ts(&outPkt, aEncCtx->time_base, outputCtx->streams[mergedAudioIndex]->time_base);
            outPkt.stream_index = mergedAudioIndex;
            av_interleaved_write_frame(outputCtx, &outPkt);
            av_packet_unref(&outPkt);
        }
    } else if (!mergeAudio) {
        for (auto &it : isoTracks) {
            if (it.voiceIsolationEnabled && it.denoiseState && !it.voiceIsolationSampleQueue.empty()) {
                std::vector<int16_t> processed_mono;
                while (!it.voiceIsolationSampleQueue.empty()) {
                    it.voiceIsolationMonoBuffer.resize(RNNOISE_FRAME_SIZE, 0.0f);
                    int take = std::min((int)it.voiceIsolationSampleQueue.size(), RNNOISE_FRAME_SIZE);
                    for (int j = 0; j < take; ++j) {
                        int16_t s = it.voiceIsolationSampleQueue.front();
                        it.voiceIsolationSampleQueue.pop_front();
                        it.voiceIsolationMonoBuffer[j] = static_cast<float>(s);
                    }
                    rnnoise_process_frame(it.denoiseState, it.voiceIsolationMonoBuffer.data(), it.voiceIsolationMonoBuffer.data());
                    for (int j = 0; j < RNNOISE_FRAME_SIZE; ++j) {
                        float v = it.voiceIsolationMonoBuffer[j];
                        if (v > 32767.0f) v = 32767.0f;
                        if (v < -32768.0f) v = -32768.0f;
                        processed_mono.push_back(static_cast<int16_t>(v));
                    }
                }
                if (!processed_mono.empty()) {
                    int back_guess = swr_get_out_samples(it.voiceIsolationBackSwrContext, (int)processed_mono.size());
                    std::vector<int16_t> processed_stereo(std::max(back_guess, (int)processed_mono.size()) * 2);
                    uint8_t* back_out_ptr = reinterpret_cast<uint8_t*>(processed_stereo.data());
                    const uint8_t* in_ptr = reinterpret_cast<const uint8_t*>(processed_mono.data());
                    int back_got = swr_convert(it.voiceIsolationBackSwrContext, &back_out_ptr, (int)processed_stereo.size() / 2, &in_ptr, (int)processed_mono.size());
                    if (back_got > 0) {
                        for (int j = 0; j < back_got * 2; ++j) {
                            int val = static_cast<int>(processed_stereo[j] * it.volume);
                            if (val > 32767) val = 32767;
                            if (val < -32768) val = -32768;
                            it.buffer.push_back(static_cast<int16_t>(val));
                        }
                    }
                }
            }
            while ((int)it.buffer.size() >= it.encFrameSamples * 2) {
                AVFrame* af = av_frame_alloc();
                af->nb_samples = it.encFrameSamples;
                av_channel_layout_copy(&af->ch_layout, &it.encCtx->ch_layout);
                af->format = it.encCtx->sample_fmt;
                af->sample_rate = it.encCtx->sample_rate;
                if (av_frame_get_buffer(af, 0) < 0) {
                    DebugLog("Failed to allocate audio frame buffer", true);
                    av_frame_free(&af);
                    success = false;
                    goto cleanup;
                }
                std::vector<int16_t> tmp(it.encFrameSamples * 2);
                for (int j = 0; j < it.encFrameSamples * 2; ++j) {
                    tmp[j] = it.buffer.front();
                    it.buffer.pop_front();
                }
                const uint8_t* inBuf[1] = { (const uint8_t*)tmp.data() };
                if (swr_convert(it.encSwrCtx, af->data, it.encFrameSamples, inBuf, it.encFrameSamples) < 0) {
                    DebugLog("Failed to convert isolation samples", true);
                    av_frame_free(&af);
                    success = false;
                    goto cleanup;
                }
                af->pts = it.pts;
                it.pts += it.encFrameSamples;
                avcodec_send_frame(it.encCtx, af);
                while (avcodec_receive_packet(it.encCtx, &outPkt) == 0) {
                    av_packet_rescale_ts(&outPkt, it.encCtx->time_base, outputCtx->streams[it.outIndex]->time_base);
                    outPkt.stream_index = it.outIndex;
                    av_interleaved_write_frame(outputCtx, &outPkt);
                    av_packet_unref(&outPkt);
                }
                av_frame_free(&af);
            }
            avcodec_send_frame(it.encCtx, nullptr);
            while (avcodec_receive_packet(it.encCtx, &outPkt) == 0) {
                av_packet_rescale_ts(&outPkt, it.encCtx->time_base, outputCtx->streams[it.outIndex]->time_base);
                outPkt.stream_index = it.outIndex;
                av_interleaved_write_frame(outputCtx, &outPkt);
                av_packet_unref(&outPkt);
            }
        }
    }

cleanup:
    DebugLog("Entering cleanup");
    if (headerWritten)
        av_write_trailer(outputCtx);
    if (!(outputCtx->oformat->flags & AVFMT_NOFILE))
        avio_closep(&outputCtx->pb);
    if (vEncCtx) avcodec_free_context(&vEncCtx);
    if (vDecCtx) avcodec_free_context(&vDecCtx);
    if (swsCtx) sws_freeContext(swsCtx);
    if (encFrame) av_frame_free(&encFrame);
    if (decFrame) av_frame_free(&decFrame);
    if (aEncCtx) avcodec_free_context(&aEncCtx);
    if (mixSwr) swr_free(&mixSwr);
    for (auto &mt : mergeTracks) {
        if (mt.swrCtx) swr_free(&mt.swrCtx);
        if (mt.decCtx) avcodec_free_context(&mt.decCtx);
        if (mt.frame) av_frame_free(&mt.frame);
        if (mt.denoiseState) rnnoise_destroy(mt.denoiseState);
        if (mt.voiceIsolationSwrContext) swr_free(&mt.voiceIsolationSwrContext);
        if (mt.voiceIsolationBackSwrContext) swr_free(&mt.voiceIsolationBackSwrContext);
    }
    for (auto &it : isoTracks) {
        if (it.encCtx) avcodec_free_context(&it.encCtx);
        if (it.encSwrCtx) swr_free(&it.encSwrCtx);
        if (it.decCtx) avcodec_free_context(&it.decCtx);
        if (it.frame) av_frame_free(&it.frame);
        if (it.denoiseState) rnnoise_destroy(it.denoiseState);
        if (it.voiceIsolationSwrContext) swr_free(&it.voiceIsolationSwrContext);
        if (it.voiceIsolationBackSwrContext) swr_free(&it.voiceIsolationBackSwrContext);
    }
    avformat_free_context(outputCtx);
    avformat_close_input(&inputCtx);

    if (progressBar && IsWindow(progressBar))
        SendMessage(progressBar, PBM_SETPOS, 100, 0);

    DebugLog("CutVideo finished");

    return success;
}
