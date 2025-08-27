#include "video_decoder.h"
#include "video_player.h"
#include "audio_player.h"
#include "video_renderer.h"
#include "ui_updates.h"
#include <algorithm> // For std::lower_bound

VideoDecoder::VideoDecoder(VideoPlayer* player) : m_player(player) {}

VideoDecoder::~VideoDecoder() {
    Cleanup();
}

bool VideoDecoder::Initialize() {
    AVStream *vs = m_player->formatContext->streams[m_player->videoStreamIndex];
    AVCodecParameters *cp = vs->codecpar;
    const AVCodec *codec = nullptr;
    m_player->useHwAccel = false;

    if (cp->codec_id == AV_CODEC_ID_H264)
    {
        codec = avcodec_find_decoder_by_name("h264_dxva2");
        if (codec)
            m_player->useHwAccel = true;
    }
    if (!codec)
        codec = avcodec_find_decoder(cp->codec_id);
    if (!codec)
        return false;

    m_player->codecContext = avcodec_alloc_context3(codec);
    if (!m_player->codecContext)
        return false;
    m_player->codecContext->opaque = m_player;
    m_player->codecContext->get_format = [](AVCodecContext *ctx, const enum AVPixelFormat *pix_fmts) {
        VideoPlayer* vp = reinterpret_cast<VideoPlayer*>(ctx->opaque);
        for (const enum AVPixelFormat *p = pix_fmts; *p != -1; p++) {
            if (*p == AV_PIX_FMT_DXVA2_VLD) {
                vp->hwPixelFormat = *p;
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

    // Skip non-reference frames to decode faster at the cost of quality
    m_player->codecContext->skip_frame = AVDISCARD_NONREF;

    if (m_player->useHwAccel)
    {
        if (av_hwdevice_ctx_create(&m_player->hwDeviceCtx, AV_HWDEVICE_TYPE_DXVA2, nullptr, nullptr, 0) < 0)
        {
            m_player->useHwAccel = false;
        }
        else
        {
            m_player->codecContext->hw_device_ctx = av_buffer_ref(m_player->hwDeviceCtx);
        }
    }

    if (avcodec_open2(m_player->codecContext, codec, nullptr) < 0)
    {
        avcodec_free_context(&m_player->codecContext);
        return false;
    }

    m_player->frameWidth = m_player->codecContext->width;
    m_player->frameHeight = m_player->codecContext->height;
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

    enum AVPixelFormat swFmt = m_player->codecContext->sw_pix_fmt != AV_PIX_FMT_NONE ?
                              m_player->codecContext->sw_pix_fmt : m_player->codecContext->pix_fmt;
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

bool VideoDecoder::DecodeNextFrame(bool presentFrame, bool scheduleDisplay) {
    if (!m_player->isLoaded)
        return false;

    std::unique_lock<std::mutex> lock(m_player->decodeMutex);

    while (true)
    {
        int ret = av_read_frame(m_player->formatContext, m_player->packet);
        if (ret < 0)
        {
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
                if (m_player->useHwAccel && m_player->hwFrame->format == m_player->hwPixelFormat)
                {
                    if (av_hwframe_transfer_data(m_player->frame, m_player->hwFrame, 0) < 0)
                        return false;
                    swFrame = m_player->frame;
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
                m_player->currentFrame++;

                bool needImage = presentFrame || scheduleDisplay;
                if (needImage)
                {
                    sws_scale(
                        m_player->swsContext,
                        (uint8_t const *const *)swFrame->data, swFrame->linesize,
                        0, m_player->frameHeight,
                        m_player->frameRGB->data, m_player->frameRGB->linesize);

                    // Cache frame for responsive backward stepping
                    if (m_player->frameCache.size() >= m_player->frameCacheLimit)
                    {
                        // Smart cache management: remove frames with larger gaps between them
                        // This helps keep more keyframes and evenly spaced frames in the cache
                        if (m_player->frameCache.size() >= 2)
                        {
                            size_t largestGapIndex = 0;
                            int64_t largestGap = 0;

                            for (size_t i = 0; i < m_player->frameCache.size() - 1; i++)
                            {
                                int64_t gap = m_player->frameCache[i+1].number - m_player->frameCache[i].number;
                                if (gap > largestGap)
                                {
                                    largestGap = gap;
                                    largestGapIndex = i;
                                }
                            }

                            // If we found a large gap and it's not the first frame, remove the second frame of the pair
                            if (largestGap > 1 && largestGapIndex < m_player->frameCache.size() - 1)
                                m_player->frameCache.erase(m_player->frameCache.begin() + largestGapIndex + 1);
                            else
                                m_player->frameCache.pop_front();  // Fall back to removing oldest
                        }
                        else
                        {
                            m_player->frameCache.pop_front();
                        }
                    }

                    // Create a more efficient cache by avoiding duplicate memory copy
                    m_player->frameCache.push_back({
                        m_player->currentFrame,
                        m_player->currentPts,
                        std::vector<uint8_t>(m_player->buffer, m_player->buffer + m_player->rgbBufferSize)
                    });
                }

                av_frame_unref(m_player->hwFrame);
                if (swFrame != m_player->hwFrame)
                    av_frame_unref(swFrame);

                lock.unlock();

                if (presentFrame)
                    m_player->m_renderer->UpdateDisplay();
                else if (scheduleDisplay)
                {
                    InvalidateRect(m_player->videoWindow, nullptr, FALSE);
                    UpdateWindow(m_player->videoWindow);
                }

                if (needImage)
                    UpdateTimeline();

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