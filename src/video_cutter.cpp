#include "video_cutter.h"
#include "video_player.h"
#include "options_window.h"
#include <iostream>
#include <sstream>
#include <fstream>
#include <commctrl.h>

// Simple debug logging helper that also writes to a file and can show popups

static std::ofstream g_debugFile;
static void DebugLog(const std::string& msg, bool popup = false)
{
    if (g_logToFile) {
        if (!g_debugFile.is_open())
            g_debugFile.open("debug.log", std::ios::app);
        if (g_debugFile.is_open())
            g_debugFile << msg << std::endl;
    }

    OutputDebugStringA((msg + "\n").c_str());
    if (popup)
        MessageBoxA(nullptr, msg.c_str(), "Video Editor Debug", MB_OK | MB_ICONINFORMATION);
}

VideoCutter::VideoCutter(VideoPlayer* player) : m_player(player) {}

VideoCutter::~VideoCutter() {}

bool VideoCutter::CutVideo(const std::wstring& outputFilename, double startTime,
                           double endTime, bool mergeAudio, bool convertH264,
                           bool useNvenc, int maxBitrate, HWND progressBar,
                           std::atomic<bool>* cancelFlag)
{
    if (!m_player->isLoaded) {
        DebugLog("CutVideo called but no video loaded", true);
        return false;
    }

    {
        std::ostringstream oss;
        oss << "CutVideo start start=" << startTime << " end=" << endTime
            << " mergeAudio=" << mergeAudio
            << " convertH264=" << convertH264
            << " useNvenc=" << useNvenc
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

    // When re-encoding or merging audio we need to set up decoder/encoder
    // contexts. The previous implementation only supported stream copying.
    // Build encoder state on demand.
    bool success = true;
    AVCodecContext* vEncCtx = nullptr;
    AVCodecContext* vDecCtx = nullptr;
    SwsContext*     swsCtx  = nullptr;
    AVFrame*        encFrame = nullptr;
    AVFrame*        decFrame = nullptr;

    struct MergeTrack {
        int index;
        AVCodecContext* decCtx;
        SwrContext* swrCtx;
        AVFrame* frame;
        std::deque<int16_t> buffer;
    };
    std::vector<MergeTrack> mergeTracks;
    AVCodecContext* aEncCtx = nullptr;
    int encFrameSamples = 0;
    std::vector<int16_t> mixBuffer;
    SwrContext* mixSwr = nullptr;
    bool headerWritten = false;

    bool needReencode = convertH264 || mergeAudio;

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
            const AVCodec* vEnc = useNvenc ?
                avcodec_find_encoder_by_name("h264_nvenc") :
                avcodec_find_encoder(AV_CODEC_ID_H264);
            if (!vEnc) {
                DebugLog("H.264 encoder not found", true);
                avformat_free_context(outputCtx);
                avformat_close_input(&inputCtx);
                return false;
            }
            outStream = avformat_new_stream(outputCtx, vEnc);
            vEncCtx = avcodec_alloc_context3(vEnc);
            vEncCtx->codec_id = AV_CODEC_ID_H264;
            vEncCtx->width = inStream->codecpar->width;
            vEncCtx->height = inStream->codecpar->height;
            vEncCtx->time_base = inStream->time_base;
            vEncCtx->pix_fmt = AV_PIX_FMT_YUV420P;
            vEncCtx->max_b_frames = 2;
            vEncCtx->gop_size = 12;
            if (maxBitrate > 0) {
                int br = maxBitrate * 1000;
                vEncCtx->bit_rate       = br;
                vEncCtx->rc_max_rate    = br;
                vEncCtx->rc_min_rate    = br;
                vEncCtx->rc_buffer_size = br * 2;
                if (vEncCtx->priv_data) {
                    av_opt_set_int(vEncCtx->priv_data, "bitrate", br, 0);
                    av_opt_set_int(vEncCtx->priv_data, "b", br, 0);
                    av_opt_set_int(vEncCtx->priv_data, "maxrate", br, 0);
                    av_opt_set_int(vEncCtx->priv_data, "minrate", br, 0);
                    av_opt_set_int(vEncCtx->priv_data, "bufsize", br * 2, 0);
                    av_opt_set       (vEncCtx->priv_data, "nal-hrd", "cbr", 0);
                    if (useNvenc) {
                        av_opt_set(vEncCtx->priv_data, "rc", "cbr", 0);
                        av_opt_set(vEncCtx->priv_data, "cbr", "1", 0);
                        av_opt_set(vEncCtx->priv_data, "cbr_padding", "1", 0);
                        av_opt_set(vEncCtx->priv_data, "strict_gop", "1", 0);
                    }
                }
            }
            if (outputCtx->oformat->flags & AVFMT_GLOBALHEADER)
                vEncCtx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
            AVDictionary* encOpts = nullptr;
            av_dict_set(&encOpts, "preset", "fast", 0);
            if (maxBitrate > 0) {
                char brStr[32];
                snprintf(brStr, sizeof(brStr), "%dk", maxBitrate);
                av_dict_set(&encOpts, "b", brStr, 0);
                av_dict_set(&encOpts, "minrate", brStr, 0);
                av_dict_set(&encOpts, "maxrate", brStr, 0);
                char bufStr[32];
                snprintf(bufStr, sizeof(bufStr), "%dk", maxBitrate * 2);
                av_dict_set(&encOpts, "bufsize", bufStr, 0);
                av_dict_set(&encOpts, "nal-hrd", "cbr", 0);
                if (useNvenc) {
                    av_dict_set(&encOpts, "rc", "cbr", 0);
                    av_dict_set(&encOpts, "cbr", "1", 0);
                    av_dict_set(&encOpts, "cbr_padding", "1", 0);
                    av_dict_set(&encOpts, "strict_gop", "1", 0);
                }
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
        } else if (needReencode && inStream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO && mergeAudio) {
            // We'll create a single output audio stream later
            MergeTrack mt{};
            mt.index = i;
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
        aEncCtx->sample_fmt = aEnc->sample_fmts ? aEnc->sample_fmts[0] : AV_SAMPLE_FMT_FLTP;
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
    av_init_packet(&pkt);
    av_init_packet(&outPkt); // ensure fields are zeroed before use
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
                if (!swsCtx) {
                    swsCtx = sws_getContext(vDecCtx->width, vDecCtx->height,
                                            (AVPixelFormat)decFrame->format,
                                            vEncCtx->width, vEncCtx->height,
                                            vEncCtx->pix_fmt, SWS_BILINEAR,
                                            nullptr, nullptr, nullptr);
                    if (!swsCtx) {
                        DebugLog("Failed to create scaling context", true);
                        av_packet_unref(&pkt);
                        success = false;
                        goto cleanup;
                    }
                }
                sws_scale(swsCtx, decFrame->data, decFrame->linesize, 0, vDecCtx->height, encFrame->data, encFrame->linesize);
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
                        int outSamples = swr_get_out_samples(mt.swrCtx, mt.frame->nb_samples);
                        std::vector<int16_t> tmp(outSamples * 2);
                        uint8_t* outArr[1] = { reinterpret_cast<uint8_t*>(tmp.data()) };
                        int conv = swr_convert(mt.swrCtx, outArr, outSamples,
                                              (const uint8_t**)mt.frame->data,
                                              mt.frame->nb_samples);
                        mt.buffer.insert(mt.buffer.end(), tmp.begin(),
                                          tmp.begin() + conv * 2);
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
                        sum += mt.buffer.front();
                        mt.buffer.pop_front();
                    }
                    int v = sum / (int)mergeTracks.size();
                    if (v > 32767) v = 32767;
                    if (v < -32768) v = -32768;
                    mixBuffer[i] = (int16_t)v;
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
        }

        double progress = (pktPtsUs - startPts) / double(endPts - startPts);
        if (progressBar && IsWindow(progressBar))
            SendMessage(progressBar, PBM_SETPOS, (int)(progress * 100.0), 0);
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
        while (true) {
            if (cancelFlag && *cancelFlag) { success = false; goto cleanup; }
            bool ready = true;
            for (auto &mt : mergeTracks)
                if ((int)mt.buffer.size() < encFrameSamples * 2) { ready = false; break; }
            if (!ready) break;
            for (int i = 0; i < encFrameSamples * 2; ++i) {
                int sum = 0;
                for (auto &mt : mergeTracks) { sum += mt.buffer.front(); mt.buffer.pop_front(); }
                int v = sum / (int)mergeTracks.size();
                if (v > 32767) v = 32767;
                if (v < -32768) v = -32768;
                mixBuffer[i] = (int16_t)v;
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
    }
    avformat_free_context(outputCtx);
    avformat_close_input(&inputCtx);

    if (progressBar && IsWindow(progressBar))
        SendMessage(progressBar, PBM_SETPOS, 100, 0);

    DebugLog("CutVideo finished");

    return success;
}
