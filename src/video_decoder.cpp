#include "video_decoder.h"
#include "video_player.h"
#include "audio_player.h"
#include "video_renderer.h"
#include "ui_updates.h"
#include <algorithm> // For std::lower_bound
#include <cstdlib>
#include <libavutil/pixdesc.h>

VideoDecoder::VideoDecoder(VideoPlayer* player) : m_player(player) {}

VideoDecoder::~VideoDecoder() {
    Cleanup();
}

bool VideoDecoder::Initialize() {
    AVStream *vs = m_player->formatContext->streams[m_player->videoStreamIndex];
    AVCodecParameters *cp = vs->codecpar;
    const AVCodec *codec = avcodec_find_decoder(cp->codec_id);
    m_player->useHwAccel = false;

    if (!codec)
        return false;

    m_player->codecContext = avcodec_alloc_context3(codec);
    if (!m_player->codecContext)
        return false;
    m_player->codecContext->opaque = m_player;
    m_player->codecContext->get_format = [](AVCodecContext *ctx, const enum AVPixelFormat *pix_fmts) {
        VideoPlayer* vp = reinterpret_cast<VideoPlayer*>(ctx->opaque);
        if (ctx->hw_device_ctx) {
            AVHWDeviceContext *hwctx = (AVHWDeviceContext*)ctx->hw_device_ctx->data;
            for (const enum AVPixelFormat *p = pix_fmts; *p != -1; p++) {
                if (hwctx->type == AV_HWDEVICE_TYPE_D3D11VA && *p == AV_PIX_FMT_D3D11) {
                    vp->hwPixelFormat = *p;
                    return *p;
                }
                if (hwctx->type == AV_HWDEVICE_TYPE_DXVA2 && *p == AV_PIX_FMT_DXVA2_VLD) {
                    vp->hwPixelFormat = *p;
                    return *p;
                }
            }
        }
        for (const enum AVPixelFormat *p = pix_fmts; *p != -1; p++) {
            if (*p != AV_PIX_FMT_D3D11 && *p != AV_PIX_FMT_DXVA2_VLD) {
                vp->hwPixelFormat = AV_PIX_FMT_NONE;
                return *p;
            }
        }
        vp->hwPixelFormat = pix_fmts[0];
        return pix_fmts[0];
    };

    if (avcodec_parameters_to_context(m_player->codecContext, cp) < 0)
    {
        avcodec_free_context(&m_player->codecContext);
        return false;
    }

    // D3D11VA/DXVA2 consumer decode paths are 4:2:0. Merely creating a D3D
    // device does not mean the stream profile can use it; treating a 4:4:4
    // software fallback as hardware forced FFmpeg onto the single-thread path
    // and made 4x playback slower than real time on demanding sources.
    const AVPixFmtDescriptor* sourceFormat = cp->format >= 0
        ? av_pix_fmt_desc_get(static_cast<AVPixelFormat>(cp->format))
        : nullptr;
    const bool hardwareCompatibleFormat = sourceFormat &&
        sourceFormat->log2_chroma_w == 1 && sourceFormat->log2_chroma_h == 1 &&
        sourceFormat->comp[0].depth <= 10;

    // Try hardware acceleration only when both the codec and source pixel
    // format have a compatible device path: D3D11VA first, then DXVA2.
    AVHWDeviceType hwTypes[] = { AV_HWDEVICE_TYPE_D3D11VA, AV_HWDEVICE_TYPE_DXVA2 };
    for (auto hwType : hwTypes)
    {
        if (!hardwareCompatibleFormat)
            break;
        bool codecSupportsDevice = false;
        for (int configIndex = 0;; ++configIndex)
        {
            const AVCodecHWConfig* config = avcodec_get_hw_config(codec, configIndex);
            if (!config)
                break;
            if ((config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX) != 0 &&
                config->device_type == hwType)
            {
                codecSupportsDevice = true;
                break;
            }
        }
        if (!codecSupportsDevice)
            continue;
        if (av_hwdevice_ctx_create(&m_player->hwDeviceCtx, hwType, nullptr, nullptr, 0) >= 0)
        {
            m_player->codecContext->hw_device_ctx = av_buffer_ref(m_player->hwDeviceCtx);
            m_player->useHwAccel = true;
            break;
        }
    }

    // Configure threading.
    if (m_player->useHwAccel)
    {
        // Hardware decoders can freeze when frame-threaded; use a single slice thread.
        m_player->codecContext->thread_count = 1;
        m_player->codecContext->thread_type  = FF_THREAD_SLICE;
    }
    else
    {
        // Use both frame and slice threading for maximum software decode throughput.
        m_player->codecContext->thread_count = 0; // auto
        m_player->codecContext->thread_type  = FF_THREAD_FRAME | FF_THREAD_SLICE;
    }

    if (avcodec_open2(m_player->codecContext, codec, nullptr) < 0)
    {
        avcodec_free_context(&m_player->codecContext);
        return false;
    }

    m_player->frameWidth = m_player->codecContext->width;
    m_player->frameHeight = m_player->codecContext->height;
    {
        std::lock_guard<std::mutex> lock(m_player->cropMutex);
        m_player->cropOutputWidth = m_player->frameWidth;
        m_player->cropOutputHeight = m_player->frameHeight;
    }
    m_player->frame = av_frame_alloc();
    m_player->frameRGB = av_frame_alloc();
    m_player->hwFrame = av_frame_alloc();
    m_player->packet = av_packet_alloc();
    if (!m_player->frame || !m_player->frameRGB || !m_player->hwFrame || !m_player->packet)
    {
        Cleanup();
        return false;
    }

    int numBytes = av_image_get_buffer_size(AV_PIX_FMT_BGRA, m_player->frameWidth, m_player->frameHeight, 32);
    m_player->rgbBufferSize = numBytes;
    m_player->buffer = (uint8_t *)av_malloc(numBytes);
    av_image_fill_arrays(m_player->frameRGB->data, m_player->frameRGB->linesize, m_player->buffer,
                         AV_PIX_FMT_BGRA, m_player->frameWidth, m_player->frameHeight, 32);

    // Determine the software pixel format for sws_scale.
    // With hw accel, sw_pix_fmt is set after avcodec_open2; without it, use pix_fmt.
    enum AVPixelFormat swFmt = m_player->codecContext->sw_pix_fmt != AV_PIX_FMT_NONE ?
                              m_player->codecContext->sw_pix_fmt : m_player->codecContext->pix_fmt;
    m_player->swsSourceFormat = swFmt;
    m_player->swsContext = sws_getContext(
        m_player->frameWidth, m_player->frameHeight, swFmt,
        m_player->frameWidth, m_player->frameHeight, AV_PIX_FMT_BGRA,
        SWS_FAST_BILINEAR, nullptr, nullptr, nullptr);
    if (!m_player->swsContext)
    {
        Cleanup();
        return false;
    }

    return true;
}

void VideoDecoder::Cleanup() {
    ResetBufferedDecodeState();
    if (m_player->playbackSwsContext)
        sws_freeContext(m_player->playbackSwsContext), m_player->playbackSwsContext = nullptr;
    m_player->playbackSwsSourceFormat = AV_PIX_FMT_NONE;
    m_player->playbackRgbBuffer.clear();
    m_player->playbackRgbWidth = 0;
    m_player->playbackRgbHeight = 0;
    m_player->playbackRgbStride = 0;
    m_player->displayUsesPlaybackBuffer = false;
    if (m_player->swsContext)
        sws_freeContext(m_player->swsContext), m_player->swsContext = nullptr;
    if (m_player->buffer)
        av_free(m_player->buffer), m_player->buffer = nullptr;
    m_player->rgbBufferSize = 0;
    if (m_player->packet)
        av_packet_free(&m_player->packet), m_player->packet = nullptr;
    if (m_player->frameRGB)
        av_frame_free(&m_player->frameRGB), m_player->frameRGB = nullptr;
    if (m_player->hwFrame)
        av_frame_free(&m_player->hwFrame), m_player->hwFrame = nullptr;
    if (m_player->frame)
        av_frame_free(&m_player->frame), m_player->frame = nullptr;
    if (m_player->codecContext)
        avcodec_free_context(&m_player->codecContext), m_player->codecContext = nullptr;
    if (m_player->hwDeviceCtx)
        av_buffer_unref(&m_player->hwDeviceCtx), m_player->hwDeviceCtx = nullptr;
    m_player->useHwAccel = false;
}

void VideoDecoder::ResetBufferedDecodeState() {
    if (m_player->packet)
        av_packet_unref(m_player->packet);
    m_bufferedPacketPending = false;
    m_bufferedDemuxEof = false;
    m_bufferedFlushSent = false;
}

int VideoDecoder::ReceiveBufferedCodecFrame(AVFrame* frame) {
#ifdef VIDEO_EDITOR_TESTING
    // A receive attempt is the operation FFmpeg requires after send EAGAIN.
    // Clearing this before the real call also models asynchronous hardware,
    // where the first poll may itself still report EAGAIN.
    m_testInjectedEagainNeedsReceive = false;
#endif
    return avcodec_receive_frame(m_player->codecContext, frame);
}

int VideoDecoder::SendBufferedCodecPacket(const AVPacket* packet) {
#ifdef VIDEO_EDITOR_TESTING
    if (packet && m_testForceSendEagain &&
        m_testAcceptedPacketCount >= m_testInjectEagainAfterPackets &&
        (m_testInjectedEagainCount == 0 || m_testInjectedEagainNeedsReceive))
    {
        m_testInjectedEagainNeedsReceive = true;
        ++m_testInjectedEagainCount;
        return AVERROR(EAGAIN);
    }
#endif

    const int result = avcodec_send_packet(m_player->codecContext, packet);
#ifdef VIDEO_EDITOR_TESTING
    if (packet && result == 0)
        ++m_testAcceptedPacketCount;
#endif
    return result;
}

#ifdef VIDEO_EDITOR_TESTING
void VideoDecoder::ForceBufferedSendEagainAfterPacketsForTesting(int acceptedPackets) {
    m_testForceSendEagain = true;
    m_testInjectedEagainNeedsReceive = false;
    m_testAcceptedPacketCount = 0;
    m_testInjectEagainAfterPackets = std::max(0, acceptedPackets);
    m_testInjectedEagainCount = 0;
}

uint64_t VideoDecoder::GetInjectedBufferedSendEagainCountForTesting() const {
    return m_testInjectedEagainCount;
}
#endif

bool VideoDecoder::DecodeNextFrame(bool presentFrame, bool scheduleDisplay, bool generateImage) {
    if (!m_player->isLoaded)
        return false;

    std::unique_lock<std::mutex> lock(m_player->decodeMutex);

    while (true)
    {
        int ret = av_read_frame(m_player->formatContext, m_player->packet);
        if (ret < 0)
        {
            // Only reset to beginning (Stop) if we were actually playing
            // If paused and manually seeking, stay at current position
            if (m_player->isPlaying && presentFrame)
                m_player->Stop();
            return false;
        }

        if (m_player->packet->stream_index == m_player->videoStreamIndex)
        {
            ret = avcodec_send_packet(m_player->codecContext, m_player->packet);
            av_packet_unref(m_player->packet);
            if (ret < 0)
                continue;

            while (true)
            {
                ret = avcodec_receive_frame(m_player->codecContext, m_player->hwFrame);
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
                    break;
                if (ret < 0)
                    return false;

                AVFrame* swFrame = m_player->hwFrame;
                if (generateImage)
                {
                    if (m_player->useHwAccel && m_player->hwFrame->format == m_player->hwPixelFormat)
                    {
                        if (av_hwframe_transfer_data(m_player->frame, m_player->hwFrame, 0) < 0)
                            return false;
                        swFrame = m_player->frame;
                    }
                }

                AVStream *vs = m_player->formatContext->streams[m_player->videoStreamIndex];
                double pts = 0.0;
                if (swFrame->best_effort_timestamp != AV_NOPTS_VALUE)
                    pts = swFrame->best_effort_timestamp * av_q2d(vs->time_base);
                else if (swFrame->pts != AV_NOPTS_VALUE)
                    pts = swFrame->pts * av_q2d(vs->time_base);
                else
                    pts = m_player->currentPts + (m_player->frameRate > 0 ? 1.0 / m_player->frameRate : 0.0);
                m_player->currentPts = pts - m_player->startTimeOffset;
                if (m_player->currentPts < 0.0)
                    m_player->currentPts = 0.0;
                
                // Calculate currentFrame from PTS to keep them in sync and prevent drift
                m_player->currentFrame = static_cast<int64_t>(m_player->currentPts * m_player->frameRate + 0.5);
                
                bool cropChanged = m_player->UpdateCropForTime(m_player->currentPts);
                
                if (generateImage)
                {
                    // Recreate sws context if the source pixel format changed
                    // (e.g. hw accel negotiated a different sw transfer format).
                    AVPixelFormat actualFmt = (AVPixelFormat)swFrame->format;
                    if (actualFmt != m_player->swsSourceFormat && actualFmt != AV_PIX_FMT_NONE)
                    {
                        sws_freeContext(m_player->swsContext);
                        m_player->swsSourceFormat = actualFmt;
                        m_player->swsContext = sws_getContext(
                            m_player->frameWidth, m_player->frameHeight, actualFmt,
                            m_player->frameWidth, m_player->frameHeight, AV_PIX_FMT_BGRA,
                            SWS_FAST_BILINEAR, nullptr, nullptr, nullptr);
                    }

                    {
                        std::lock_guard<std::mutex> renderLock(m_player->renderMutex);
                        sws_scale(
                            m_player->swsContext,
                            (uint8_t const *const *)swFrame->data, swFrame->linesize,
                            0, m_player->frameHeight,
                            m_player->frameRGB->data, m_player->frameRGB->linesize);
                        m_player->displayUsesPlaybackBuffer = false;
                    }
                }

                av_frame_unref(m_player->hwFrame);
                if (swFrame != m_player->hwFrame)
                    av_frame_unref(swFrame);

                lock.unlock();

                if (presentFrame)
                    m_player->m_renderer->UpdateDisplay();
                else if (scheduleDisplay)
                    InvalidateRect(m_player->videoWindow, nullptr, FALSE);

                if (presentFrame || scheduleDisplay)
                    UpdateTimeline();

                if (cropChanged && !presentFrame && !scheduleDisplay)
                    InvalidateRect(m_player->videoWindow, nullptr, FALSE);

                return true;
            }
        }
        else
        {
            // Check if this is an audio packet
            for (auto& track : m_player->audioTracks)
            {
                if (m_player->packet->stream_index == track->streamIndex)
                {
                    // Drop audio while single-stepping to keep UI responsive and avoid A/V drift
                    if (!m_player->dropAudioDuringStepping)
                        m_player->m_audioPlayer->ProcessFrame(m_player->packet);
                    break;
                }
            }
            av_packet_unref(m_player->packet);
        }
    }
    return false; // Should never reach here
}

VideoDecoder::BufferedReceiveResult VideoDecoder::ReceiveBufferedFrame(
    AVFrame* outputFrame, double& pts, int64_t& frameNumber) {
    const int ret = ReceiveBufferedCodecFrame(m_player->hwFrame);
    if (ret == AVERROR(EAGAIN))
        return BufferedReceiveResult::NeedPacket;
    if (ret == AVERROR_EOF)
        return BufferedReceiveResult::EndOfStream;
    if (ret < 0)
        return BufferedReceiveResult::RecoverableError;

    AVStream* stream = m_player->formatContext->streams[m_player->videoStreamIndex];
    const int64_t decodedTimestamp =
        m_player->hwFrame->best_effort_timestamp != AV_NOPTS_VALUE
            ? m_player->hwFrame->best_effort_timestamp
            : m_player->hwFrame->pts;
    const double decodedPts = decodedTimestamp != AV_NOPTS_VALUE
        ? std::max(0.0, decodedTimestamp * av_q2d(stream->time_base) -
                          m_player->startTimeOffset)
        : -1.0;
    const double speed = m_player->GetPlaybackSpeed();
    if (speed >= 4.0 && decodedPts >= 0.0)
    {
        const double clockTarget = m_player->GetPlaybackClockTarget();
        const int64_t nowNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        const bool highSpeedDeliveryOverdue = speed >= 8.0 &&
            (m_player->m_lastHighSpeedFrameDeliveryNs == 0 ||
             nowNs - m_player->m_lastHighSpeedFrameDeliveryNs >= 16666667);

        const double displayMediaStep = speed / 60.0;
        const bool cadenceDue = m_lastHighSpeedFrameDeliveryPts < 0.0 ||
            decodedPts >= m_lastHighSpeedFrameDeliveryPts + displayMediaStep * 0.6;
        if (!cadenceDue && !highSpeedDeliveryOverdue)
        {
            av_frame_unref(m_player->hwFrame);
            return BufferedReceiveResult::FrameDiscarded;
        }

        const double allowedLag = std::max(0.10, speed / 60.0);
        if (decodedPts < clockTarget - allowedLag && !highSpeedDeliveryOverdue)
        {
            av_frame_unref(m_player->hwFrame);
            return BufferedReceiveResult::FrameDiscarded;
        }
    }

    if (m_player->useHwAccel &&
        m_player->hwFrame->format == m_player->hwPixelFormat)
    {
        if (av_hwframe_transfer_data(outputFrame, m_player->hwFrame, 0) < 0)
        {
            av_frame_unref(m_player->hwFrame);
            return BufferedReceiveResult::RecoverableError;
        }
        av_frame_copy_props(outputFrame, m_player->hwFrame);
    }
    else if (av_frame_ref(outputFrame, m_player->hwFrame) < 0)
    {
        av_frame_unref(m_player->hwFrame);
        return BufferedReceiveResult::RecoverableError;
    }

    if (decodedPts >= 0.0)
        pts = decodedPts;
    else
        pts = m_player->currentPts +
              (m_player->frameRate > 0.0 ? 1.0 / m_player->frameRate : 1.0 / 30.0);

    pts = std::max(0.0, pts);
    frameNumber = static_cast<int64_t>(pts * m_player->frameRate + 0.5);
    av_frame_unref(m_player->hwFrame);
    if (speed >= 4.0)
    {
        m_lastHighSpeedFrameDeliveryPts = pts;
        m_player->m_lastHighSpeedFrameDeliveryNs =
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
    }
    else
    {
        m_lastHighSpeedFrameDeliveryPts = -1.0;
        m_player->m_lastHighSpeedFrameDeliveryNs = 0;
    }
    return BufferedReceiveResult::FrameReady;
}

VideoDecoder::BufferedDecodeResult VideoDecoder::DecodeNextBufferedFrame(
    AVFrame* outputFrame, double& pts, int64_t& frameNumber) {
    if (!m_player->isLoaded || !outputFrame)
        return BufferedDecodeResult::RecoverableError;

    std::unique_lock<std::mutex> lock(m_player->decodeMutex);
    av_frame_unref(outputFrame);
    bool scanningToForwardKeyframe = false;
    bool jumpedToClockTarget = false;

    const double initialSpeed = m_player->GetPlaybackSpeed();
    if (m_player->m_lastHighSpeedFrameDeliveryNs == 0)
    {
        m_lastHighSpeedFrameDeliveryPts = -1.0;
        m_moderateLagStartNs = 0;
    }
    if (initialSpeed >= 4.0 && initialSpeed < 64.0)
    {
        // At these rates the display cannot show every source frame. Decode
        // reference frames from the outset so demanding sources do not become
        // throughput-limited before the clock has even started. NONREF keeps
        // reference B-frames (unlike BIDIR), preserving a denser cadence.
        m_player->codecContext->skip_frame = AVDISCARD_NONREF;
        m_player->codecContext->skip_idct = AVDISCARD_NONREF;
        m_player->codecContext->skip_loop_filter = AVDISCARD_ALL;
    }
    else
    {
        m_moderateLagStartNs = 0;
        m_lastHighSpeedFrameDeliveryPts = -1.0;
        m_player->codecContext->skip_frame = AVDISCARD_DEFAULT;
        m_player->codecContext->skip_idct = AVDISCARD_DEFAULT;
        m_player->codecContext->skip_loop_filter = AVDISCARD_DEFAULT;
    }

    while (m_player->playbackThreadRunning)
    {
        // Always drain decoder output before reading another demux packet.
        // A single compressed packet can produce multiple frames. Reading and
        // sending a new packet first can therefore return EAGAIN; the packet
        // must remain pending while those frames are delivered.
        const BufferedReceiveResult receiveResult =
            ReceiveBufferedFrame(outputFrame, pts, frameNumber);
        if (receiveResult == BufferedReceiveResult::FrameReady)
            return BufferedDecodeResult::FrameReady;
        if (receiveResult == BufferedReceiveResult::FrameDiscarded)
            continue;
        if (receiveResult == BufferedReceiveResult::EndOfStream)
            return BufferedDecodeResult::EndOfStream;
        if (receiveResult == BufferedReceiveResult::RecoverableError)
            return BufferedDecodeResult::RecoverableError;

        if (m_player->m_playbackSeekPending.load(std::memory_order_acquire))
            return BufferedDecodeResult::RecoverableError;

        if (m_bufferedPacketPending)
        {
            const int sendResult =
                SendBufferedCodecPacket(m_player->packet);
            if (sendResult == 0)
            {
                av_packet_unref(m_player->packet);
                m_bufferedPacketPending = false;
                continue;
            }
            if (sendResult == AVERROR(EAGAIN))
            {
                // Keep ownership of the packet. The next iteration drains
                // output first and retries this exact packet afterward.
                std::this_thread::yield();
                continue;
            }

            av_packet_unref(m_player->packet);
            m_bufferedPacketPending = false;
            if (sendResult == AVERROR_EOF)
                return BufferedDecodeResult::EndOfStream;
            return BufferedDecodeResult::RecoverableError;
        }

        if (m_bufferedDemuxEof)
        {
            if (m_bufferedFlushSent)
                return BufferedDecodeResult::EndOfStream;

            const int flushResult =
                SendBufferedCodecPacket(nullptr);
            if (flushResult == 0 || flushResult == AVERROR_EOF)
            {
                m_bufferedFlushSent = true;
                continue;
            }
            if (flushResult == AVERROR(EAGAIN))
                continue;
            return BufferedDecodeResult::RecoverableError;
        }

        int ret = av_read_frame(m_player->formatContext, m_player->packet);
        if (ret < 0)
        {
            if (ret == AVERROR(EAGAIN))
                continue;
            if (ret == AVERROR_EOF)
            {
                m_bufferedDemuxEof = true;
                continue;
            }
            return BufferedDecodeResult::RecoverableError;
        }

        if (m_player->packet->stream_index == m_player->videoStreamIndex)
        {
            AVStream* stream = m_player->formatContext->streams[m_player->videoStreamIndex];
            const int64_t packetTimestamp = m_player->packet->pts != AV_NOPTS_VALUE
                ? m_player->packet->pts
                : m_player->packet->dts;
            const double packetPts = packetTimestamp != AV_NOPTS_VALUE
                ? std::max(0.0, packetTimestamp * av_q2d(stream->time_base) -
                                  m_player->startTimeOffset)
                : -1.0;
            const double packetSpeed = m_player->GetPlaybackSpeed();
            const double clockTarget = m_player->GetPlaybackClockTarget();
            // Frame-threaded decoders can accept packets far ahead of the
            // frames they have actually produced. Use delivered media
            // progress when available, otherwise catch-up falsely concludes
            // that playback is on time while presentation is several seconds
            // behind the requested clock.
            const double deliveredProgress = m_lastHighSpeedFrameDeliveryPts >= 0.0
                ? m_lastHighSpeedFrameDeliveryPts
                : packetPts;
            const double lag = deliveredProgress >= 0.0
                ? clockTarget - deliveredProgress
                : 0.0;

            if (packetSpeed >= 4.0 && packetSpeed < 64.0 &&
                packetPts >= 0.0 && !jumpedToClockTarget)
            {
                // A packet can briefly be late because of ordinary decoder or
                // scheduler jitter. The decoded-frame path below already drops
                // stale output without doing color conversion, which normally
                // lets it recover while preserving continuous I/P motion. Only
                // abandon that GOP when the lag persists or becomes too large
                // to recover sequentially. This avoids turning a ~30 ms hiccup
                // into the multi-second visual jump of a keyframe seek.
                const bool smoothModerateSpeed = packetSpeed < 8.0;
                const double recoverableLag = smoothModerateSpeed
                    ? std::max(0.20, packetSpeed / 30.0)
                    : std::max(0.50, packetSpeed / 15.0);
                const double severeLag = smoothModerateSpeed
                    ? std::max(0.75, packetSpeed / 4.0)
                    : std::max(1.50, packetSpeed / 4.0);
                const int64_t nowNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now().time_since_epoch()).count();

                if (lag <= recoverableLag)
                {
                    m_moderateLagStartNs = 0;
                    m_player->codecContext->skip_frame = AVDISCARD_NONREF;
                    m_player->codecContext->skip_idct = AVDISCARD_NONREF;
                }
                else if (m_moderateLagStartNs == 0)
                {
                    m_moderateLagStartNs = nowNs;
                }

                if (lag > recoverableLag)
                {
                    // B-frames are not references for future P-frames, so they
                    // are safe to omit temporarily while the decoder closes a
                    // real clock gap. Full cadence resumes once lag recovers.
                    m_player->codecContext->skip_frame = AVDISCARD_BIDIR;
                    m_player->codecContext->skip_idct = AVDISCARD_BIDIR;
                }

                // Preview-sized conversion removes the main transient cost,
                // so give sequential reference decoding enough time to close
                // short gaps on its own. The old 120 ms window repeatedly
                // forced a seek during harmless jitter, producing the visible
                // play-pause-jump cycle at 5x. Severe lag still jumps at once.
                const bool lagWasSustained = !smoothModerateSpeed ||
                    (m_moderateLagStartNs != 0 &&
                     nowNs - m_moderateLagStartNs >= 75000000);
                if (lag > severeLag ||
                    (lag > recoverableLag && lagWasSustained))
                {
                    const double frameDuration = m_player->frameRate > 0.0
                        ? 1.0 / m_player->frameRate
                        : 1.0 / 30.0;
                    // A catch-up seek itself costs part of a display interval.
                    // Aim one display tick ahead at 8x+ so the frame that
                    // arrives after demux/decode is aligned with the clock
                    // then, instead of permanently trailing it by that work.
                    const double schedulingLead = smoothModerateSpeed
                        ? 0.0
                        : std::min(0.25, packetSpeed / 60.0);
                    const double seekClockTarget = clockTarget + schedulingLead;
                    const double target = m_player->duration > frameDuration
                        ? std::min(seekClockTarget, m_player->duration - frameDuration)
                        : std::max(0.0, seekClockTarget);
                    const int64_t targetTs = static_cast<int64_t>(
                        (target + m_player->startTimeOffset) / av_q2d(stream->time_base));

                    const AVIndexEntry* before = avformat_index_get_entry_from_timestamp(
                        stream, targetTs, AVSEEK_FLAG_BACKWARD);
                    const AVIndexEntry* after = avformat_index_get_entry_from_timestamp(
                        stream, targetTs, 0);
                    const AVIndexEntry* chosen = nullptr;
                    const int64_t deliveredTs = deliveredProgress >= 0.0
                        ? static_cast<int64_t>((deliveredProgress + m_player->startTimeOffset) /
                                               av_q2d(stream->time_base))
                        : packetTimestamp;
                    if (before && before->timestamp > deliveredTs)
                        chosen = before;

                    // Do not leap to the next GOP merely because the keyframe
                    // before the clock is the one currently being decoded. A
                    // modest forward overshoot is acceptable; a distant one is
                    // smoother to reach by decoding reference frames. Severe
                    // lag remains the fallback that guarantees the real rate.
                    if (after && after->timestamp > deliveredTs)
                    {
                        const double afterPts = after->timestamp *
                            av_q2d(stream->time_base) - m_player->startTimeOffset;
                        const double maxForwardOvershoot =
                            std::max(0.35, packetSpeed / 30.0);
                        const bool afterIsUsable = afterPts <= target + maxForwardOvershoot ||
                                                   lag > severeLag;
                        if (afterIsUsable &&
                            (!chosen || std::llabs(after->timestamp - targetTs) <
                                        std::llabs(chosen->timestamp - targetTs)))
                            chosen = after;
                    }

                    int seekResult = -1;
                    if (chosen)
                    {
                        av_packet_unref(m_player->packet);
                        seekResult = av_seek_frame(m_player->formatContext,
                                                   m_player->videoStreamIndex,
                                                   chosen->timestamp,
                                                   AVSEEK_FLAG_BACKWARD);
                    }
                    else if (!before && !after)
                    {
                        av_packet_unref(m_player->packet);
                        seekResult = avformat_seek_file(
                            m_player->formatContext, m_player->videoStreamIndex,
                            targetTs, targetTs, INT64_MAX, AVSEEK_FLAG_ANY);
                        scanningToForwardKeyframe = seekResult >= 0;
                    }
                    if (seekResult >= 0)
                    {
                        avcodec_flush_buffers(m_player->codecContext);
                        ResetBufferedDecodeState();
                        for (auto& track : m_player->audioTracks)
                        {
                            if (track->codecContext)
                                avcodec_flush_buffers(track->codecContext);
                        }
                        {
                            std::lock_guard<std::mutex> audioLock(m_player->audioMutex);
                            for (auto& track : m_player->audioTracks)
                                track->buffer.clear();
                        }
                        m_moderateLagStartNs = 0;
                        m_lastHighSpeedFrameDeliveryPts = -1.0;
                        jumpedToClockTarget = true;
                        continue;
                    }
                }
            }

            if (packetSpeed >= 64.0 && packetPts >= 0.0 && !jumpedToClockTarget)
            {
                // Walking every intervening GOP still imposes a throughput
                // ceiling at extreme rates. Jump the demuxer near the media
                // clock instead, then scan forward to a valid keyframe. The
                // lag allowance represents roughly one 30 Hz display tick in
                // media time and therefore scales without capping the rate.
                const double jumpThreshold = std::max(1.0, packetSpeed / 30.0);
                if (lag > jumpThreshold)
                {
                    const double frameDuration = m_player->frameRate > 0.0
                        ? 1.0 / m_player->frameRate
                        : 1.0 / 30.0;
                    const double target = m_player->duration > frameDuration
                        ? std::min(clockTarget, m_player->duration - frameDuration)
                        : std::max(0.0, clockTarget);
                    const int64_t targetTs = static_cast<int64_t>(
                        (target + m_player->startTimeOffset) / av_q2d(stream->time_base));

                    av_packet_unref(m_player->packet);
                    std::vector<AVDiscard> previousDiscard(m_player->formatContext->nb_streams);
                    for (unsigned i = 0; i < m_player->formatContext->nb_streams; ++i)
                    {
                        previousDiscard[i] = m_player->formatContext->streams[i]->discard;
                        if (static_cast<int>(i) != m_player->videoStreamIndex)
                            m_player->formatContext->streams[i]->discard = AVDISCARD_ALL;
                    }
                    int seekResult = avformat_seek_file(
                        m_player->formatContext, m_player->videoStreamIndex,
                        targetTs, targetTs, INT64_MAX, AVSEEK_FLAG_ANY);
                    if (seekResult < 0)
                    {
                        seekResult = av_seek_frame(
                            m_player->formatContext, m_player->videoStreamIndex,
                            targetTs, AVSEEK_FLAG_ANY);
                    }
                    for (unsigned i = 0; i < m_player->formatContext->nb_streams; ++i)
                        m_player->formatContext->streams[i]->discard = previousDiscard[i];

                    if (seekResult >= 0)
                    {
                        avcodec_flush_buffers(m_player->codecContext);
                        ResetBufferedDecodeState();
                        for (auto& track : m_player->audioTracks)
                        {
                            if (track->codecContext)
                                avcodec_flush_buffers(track->codecContext);
                        }
                        {
                            std::lock_guard<std::mutex> audioLock(m_player->audioMutex);
                            for (auto& track : m_player->audioTracks)
                                track->buffer.clear();
                        }
                        scanningToForwardKeyframe = true;
                        jumpedToClockTarget = true;
                        continue;
                    }
                }
            }

            if (packetSpeed >= 64.0 && packetPts >= 0.0)
            {
                const double scanThreshold = std::max(0.5, packetSpeed / 15.0);
                if (lag > scanThreshold)
                    scanningToForwardKeyframe = true;
            }

            if (scanningToForwardKeyframe)
            {
                if ((m_player->packet->flags & AV_PKT_FLAG_KEY) == 0)
                {
                    av_packet_unref(m_player->packet);
                    continue;
                }
                // Resume from a keyframe encountered by forward demuxing. No
                // seek is involved, so the decoder can never revisit a GOP.
                avcodec_flush_buffers(m_player->codecContext);
                scanningToForwardKeyframe = false;
            }

            m_bufferedPacketPending = true;
            continue;
        }
        else
        {
            // Audio at these rates is not intelligible, and decoding/resampling
            // it can consume enough time to throttle the video clock.
            if (scanningToForwardKeyframe || m_player->GetPlaybackSpeed() >= 4.0)
            {
                av_packet_unref(m_player->packet);
                continue;
            }
            for (auto& track : m_player->audioTracks)
            {
                if (m_player->packet->stream_index == track->streamIndex)
                {
                    m_player->m_audioPlayer->ProcessFrame(m_player->packet);
                    break;
                }
            }
            av_packet_unref(m_player->packet);
        }
    }
    return BufferedDecodeResult::EndOfStream;
}
