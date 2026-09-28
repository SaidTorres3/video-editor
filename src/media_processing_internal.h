#pragma once
#include <algorithm>
#include <atomic>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <windows.h>
extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

namespace Media
{
inline std::string Utf8(const std::wstring &text)
{
    int n = WideCharToMultiByte(CP_UTF8, 0, text.data(), (int)text.size(), nullptr, 0, nullptr,
                                nullptr);
    std::string result(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), (int)text.size(), result.data(), n, nullptr,
                        nullptr);
    return result;
}
inline void Check(int result, const char *operation)
{
    if (result >= 0)
        return;
    char reason[AV_ERROR_MAX_STRING_SIZE] = {};
    av_strerror(result, reason, sizeof(reason));
    throw std::runtime_error(std::string(operation) + ": " + reason);
}
inline void Cancelled(const std::atomic<bool> &cancel)
{
    if (cancel.load())
        throw std::runtime_error("Processing cancelled.");
}
struct Input
{
    AVFormatContext *value = nullptr;
    Input(const std::wstring &path, std::atomic<bool> &cancel)
    {
        value = avformat_alloc_context();
        if (!value)
            throw std::bad_alloc();
        value->interrupt_callback = {
            [](void *p) { return static_cast<std::atomic<bool> *>(p)->load() ? 1 : 0; }, &cancel};
        try
        {
            Check(avformat_open_input(&value, Utf8(path).c_str(), nullptr, nullptr), "Open input");
            Check(avformat_find_stream_info(value, nullptr), "Read streams");
        }
        catch (...)
        {
            avformat_close_input(&value);
            throw;
        }
    }
    ~Input()
    {
        avformat_close_input(&value);
    }
    Input(const Input &) = delete;
};
struct Decoder
{
    AVCodecContext *context = nullptr;
    AVFrame *frame = av_frame_alloc();
    SwrContext *resampler = nullptr;
    ~Decoder()
    {
        swr_free(&resampler);
        av_frame_free(&frame);
        avcodec_free_context(&context);
    }
    void Open(AVStream *stream)
    {
        if (!frame)
            throw std::bad_alloc();
        context = avcodec_alloc_context3(avcodec_find_decoder(stream->codecpar->codec_id));
        if (!context)
            throw std::bad_alloc();
        Check(avcodec_parameters_to_context(context, stream->codecpar), "Configure decoder");
        context->pkt_timebase = stream->time_base;
        Check(avcodec_open2(context, nullptr, nullptr), "Open decoder");
    }
    void Resample(int rate, const AVChannelLayout &layout)
    {
        Check(swr_alloc_set_opts2(&resampler, &layout, AV_SAMPLE_FMT_FLT, rate, &context->ch_layout,
                                  context->sample_fmt, context->sample_rate, 0, nullptr),
              "Configure audio conversion");
        Check(swr_init(resampler), "Initialize audio conversion");
    }
};
struct Packet
{
    AVPacket *value = av_packet_alloc();
    Packet()
    {
        if (!value)
            throw std::bad_alloc();
    }
    ~Packet()
    {
        av_packet_free(&value);
    }
};
inline double Origin(AVFormatContext *format)
{
    double origin = std::numeric_limits<double>::max();
    for (unsigned i = 0; i < format->nb_streams; ++i)
    {
        auto *stream = format->streams[i];
        if (stream->start_time != AV_NOPTS_VALUE)
            origin = std::min(origin, stream->start_time * av_q2d(stream->time_base));
    }
    return origin == std::numeric_limits<double>::max() ? 0.0 : origin;
}
} // namespace Media
