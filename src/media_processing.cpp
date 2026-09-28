#include "media_processing.h"
#include "media_processing_internal.h"
#include "openfx_effect.h"
#include <cmath>
#include <cstring>
#include <filesystem>

namespace
{
struct Output
{
    AVFormatContext *context = nullptr;
    std::wstring staging;
    bool published = false;
    ~Output()
    {
        if (context)
        {
            if (context->pb)
                avio_closep(&context->pb);
            avformat_free_context(context);
        }
        if (!published && !staging.empty())
            DeleteFileW(staging.c_str());
    }
};
struct VideoEncoder
{
    AVCodecContext *context = nullptr;
    AVFrame *frame = av_frame_alloc();
    SwsContext *toRgba = nullptr;
    SwsContext *fromRgba = nullptr;
    std::vector<uint8_t> rgba;
    ~VideoEncoder()
    {
        avcodec_free_context(&context);
        av_frame_free(&frame);
        sws_freeContext(toRgba);
        sws_freeContext(fromRgba);
    }
};
} // namespace

void ProcessMediaCopy(const std::wstring &filename, const std::wstring &destination,
                      const std::map<int, double> &offsets, OpenFxEffect *effect,
                      std::atomic<bool> &cancel, const MediaProgress &progress)
{
    Media::Cancelled(cancel);
    if (std::filesystem::exists(destination))
        throw std::runtime_error(
            "Choose a new output filename; existing files are never overwritten.");
    if (std::filesystem::path(destination).extension() != L".mkv")
        throw std::runtime_error("The working copy must use the .mkv extension.");
    Media::Input input(filename, cancel);
    auto *source = input.value;
    const double origin = Media::Origin(source);
    const double duration =
        source->duration > 0 ? source->duration / static_cast<double>(AV_TIME_BASE) : 0;
    Output output;
    std::wstring staging = destination + L".processing-" + std::to_wstring(GetCurrentProcessId()) +
                           L"-" + std::to_wstring(GetTickCount64());
    HANDLE file = CreateFileW(staging.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        throw std::runtime_error("Cannot create the output working file.");
    CloseHandle(file);
    output.staging = staging;
    Media::Check(avformat_alloc_output_context2(&output.context, nullptr, "matroska",
                                                Media::Utf8(staging).c_str()),
                 "Create Matroska output");
    output.context->avoid_negative_ts = AVFMT_AVOID_NEG_TS_DISABLED;
    av_dict_copy(&output.context->metadata, source->metadata, 0);
    std::vector<int> mapping(source->nb_streams, -1);
    std::map<int, std::unique_ptr<Media::Decoder>> decoders;
    int videoIndex = -1;
    double videoRate = 0;
    VideoEncoder video;
    for (const auto &offset : offsets)
        if (offset.first < 0 || static_cast<unsigned>(offset.first) >= source->nb_streams ||
            source->streams[offset.first]->codecpar->codec_type != AVMEDIA_TYPE_AUDIO ||
            !std::isfinite(offset.second) || std::abs(offset.second) > 30.0)
            throw std::runtime_error("Invalid audio alignment offset.");
    for (unsigned i = 0; i < source->nb_streams; ++i)
    {
        auto *in = source->streams[i];
        auto type = in->codecpar->codec_type;
        if (type != AVMEDIA_TYPE_AUDIO && type != AVMEDIA_TYPE_VIDEO &&
            type != AVMEDIA_TYPE_SUBTITLE)
            continue;
        auto *out = avformat_new_stream(output.context, nullptr);
        if (!out)
            throw std::bad_alloc();
        mapping[i] = out->index;
        Media::Check(avcodec_parameters_copy(out->codecpar, in->codecpar),
                     "Copy stream description");
        out->codecpar->codec_tag = 0;
        out->time_base = in->time_base;
        out->disposition = in->disposition;
        out->sample_aspect_ratio = in->sample_aspect_ratio;
        out->avg_frame_rate = in->avg_frame_rate;
        av_dict_copy(&out->metadata, in->metadata, 0);
        bool transformVideo = effect && videoIndex < 0 && type == AVMEDIA_TYPE_VIDEO;
        // Materialize timestamp gaps as samples on every audio stream during
        // alignment. This keeps the existing export mixer in sync as well.
        bool transformAudio = type == AVMEDIA_TYPE_AUDIO && !offsets.empty();
        if (!transformAudio && !transformVideo)
            continue;
        auto decoder = std::make_unique<Media::Decoder>();
        decoder->Open(in);
        if (transformAudio)
        {
            decoder->Resample(decoder->context->sample_rate, decoder->context->ch_layout);
            // Pack samples without lossy encoding. Each timestamp is adjusted at
            // sample precision and samples before the start are trimmed exactly.
            av_freep(&out->codecpar->extradata);
            out->codecpar->extradata_size = 0;
            out->codecpar->codec_id = AV_CODEC_ID_PCM_F32LE;
            out->codecpar->format = AV_SAMPLE_FMT_FLT;
            out->codecpar->bits_per_coded_sample = 32;
            out->codecpar->bits_per_raw_sample = 32;
            out->codecpar->block_align = 4 * decoder->context->ch_layout.nb_channels;
            out->codecpar->bit_rate = static_cast<int64_t>(out->codecpar->block_align) * 8 *
                                      decoder->context->sample_rate;
            out->codecpar->initial_padding = out->codecpar->trailing_padding = 0;
            out->codecpar->frame_size = 0;
            out->time_base = {1, decoder->context->sample_rate};
        }
        else
        {
            videoIndex = i;
            video.context = avcodec_alloc_context3(avcodec_find_encoder(AV_CODEC_ID_FFV1));
            if (!video.context || !video.frame)
                throw std::bad_alloc();
            auto *c = video.context;
            c->width = decoder->context->width;
            c->height = decoder->context->height;
            c->pix_fmt = AV_PIX_FMT_BGRA;
            c->time_base = in->time_base;
            c->framerate = av_guess_frame_rate(source, in, nullptr);
            videoRate = av_q2d(c->framerate);
            if (videoRate <= 0)
                throw std::runtime_error("Cannot determine the video frame rate for OpenFX.");
            c->sample_aspect_ratio = in->sample_aspect_ratio;
            c->color_range = AVCOL_RANGE_JPEG;
            c->color_primaries = decoder->context->color_primaries;
            c->color_trc = decoder->context->color_trc;
            if (output.context->oformat->flags & AVFMT_GLOBALHEADER)
                c->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
            Media::Check(avcodec_open2(c, nullptr, nullptr), "Open lossless video encoder");
            Media::Check(avcodec_parameters_from_context(out->codecpar, c),
                         "Configure effect output");
            video.frame->format = c->pix_fmt;
            video.frame->width = c->width;
            video.frame->height = c->height;
            Media::Check(av_frame_get_buffer(video.frame, 32), "Allocate effect frame");
            video.rgba.resize(static_cast<size_t>(c->width) * c->height * 4);
        }
        decoders.emplace(i, std::move(decoder));
    }
    if (effect && videoIndex < 0)
        throw std::runtime_error("No video stream found.");
    Media::Check(avio_open(&output.context->pb, Media::Utf8(staging).c_str(), AVIO_FLAG_WRITE),
                 "Open working copy");
    Media::Check(avformat_write_header(output.context, nullptr), "Write output header");
    Media::Packet encoded;
    auto writeVideo = [&]() {
        int result;
        while ((result = avcodec_receive_packet(video.context, encoded.value)) >= 0)
        {
            auto *out = output.context->streams[mapping[videoIndex]];
            av_packet_rescale_ts(encoded.value, video.context->time_base, out->time_base);
            encoded.value->stream_index = out->index;
            Media::Check(av_interleaved_write_frame(output.context, encoded.value),
                         "Write effect video");
            av_packet_unref(encoded.value);
        }
        if (result != AVERROR(EAGAIN) && result != AVERROR_EOF)
            Media::Check(result, "Encode effect video");
    };
    std::map<int, int64_t> nextAudioSample, writtenAudioSamples;
    auto decode = [&](int index, AVPacket *packet) {
        auto &d = *decoders.at(index);
        auto *in = source->streams[index];
        auto *out = output.context->streams[mapping[index]];
        Media::Check(avcodec_send_packet(d.context, packet), "Send media packet");
        int result;
        while ((result = avcodec_receive_frame(d.context, d.frame)) >= 0)
        {
            Media::Cancelled(cancel);
            auto *frame = d.frame;
            int64_t ts = frame->best_effort_timestamp;
            if (index == videoIndex)
            {
                if (ts == AV_NOPTS_VALUE)
                    throw std::runtime_error("OpenFX requires timestamped video frames.");
                if (frame->width != video.context->width || frame->height != video.context->height)
                    throw std::runtime_error(
                        "OpenFX does not support changing frame dimensions within a file.");
                if (frame->flags & AV_FRAME_FLAG_INTERLACED)
                    throw std::runtime_error(
                        "Deinterlace the video before applying this OpenFX host.");
                video.toRgba =
                    sws_getCachedContext(video.toRgba, frame->width, frame->height,
                                         (AVPixelFormat)frame->format, frame->width, frame->height,
                                         AV_PIX_FMT_RGBA, SWS_BICUBIC, nullptr, nullptr, nullptr);
                video.fromRgba = sws_getCachedContext(
                    video.fromRgba, frame->width, frame->height, AV_PIX_FMT_RGBA, frame->width,
                    frame->height, AV_PIX_FMT_BGRA, SWS_BICUBIC, nullptr, nullptr, nullptr);
                if (!video.toRgba || !video.fromRgba)
                    throw std::runtime_error("Cannot convert OpenFX image format.");
                uint8_t *data[] = {video.rgba.data(), nullptr, nullptr, nullptr};
                int strides[] = {frame->width * 4, 0, 0, 0};
                if (sws_scale(video.toRgba, frame->data, frame->linesize, 0, frame->height, data,
                              strides) != frame->height)
                    throw std::runtime_error("OpenFX input conversion failed.");
                double time = ts * av_q2d(in->time_base) - origin;
                effect->Render(data[0], strides[0], time * videoRate);
                Media::Check(av_frame_make_writable(video.frame), "Prepare effect output frame");
                sws_scale(video.fromRgba, data, strides, 0, frame->height, video.frame->data,
                          video.frame->linesize);
                video.frame->pts = ts - av_rescale_q(std::llround(origin * AV_TIME_BASE),
                                                     AV_TIME_BASE_Q, in->time_base);
                Media::Check(avcodec_send_frame(video.context, video.frame), "Encode effect frame");
                writeVideo();
            }
            else
            {
                int rate = d.context->sample_rate, channels = d.context->ch_layout.nb_channels;
                int capacity = swr_get_out_samples(d.resampler, frame->nb_samples);
                std::vector<float> samples(static_cast<size_t>(capacity) * channels);
                uint8_t *data = reinterpret_cast<uint8_t *>(samples.data());
                int count = swr_convert(d.resampler, &data, capacity,
                                        const_cast<const uint8_t **>(frame->extended_data),
                                        frame->nb_samples);
                Media::Check(count, "Convert aligned audio");
                const double offset = offsets.count(index) ? offsets.at(index) : 0.0;
                if (ts == AV_NOPTS_VALUE && !nextAudioSample.count(index))
                    throw std::runtime_error(
                        "Alignment requires an initial timestamp on each audio stream.");
                int64_t start =
                    ts == AV_NOPTS_VALUE
                        ? nextAudioSample[index]
                        : std::llround((ts * av_q2d(in->time_base) - origin + offset) * rate);
                // Containers such as Matroska quantize timestamps to milliseconds.
                // Preserve sample continuity within one timestamp tick.
                if (nextAudioSample.count(index) &&
                    std::abs(start - nextAudioSample[index]) <=
                        std::max<int64_t>(1, std::llround(av_q2d(in->time_base) * rate)))
                    start = nextAudioSample[index];
                nextAudioSample[index] = start + count;
                auto &written = writtenAudioSamples[index];
                int trim = static_cast<int>(
                    std::min<int64_t>(count, std::max<int64_t>(0, written - start)));
                count -= trim;
                start += trim;
                while (count > 0 && written < start)
                {
                    Media::Cancelled(cancel);
                    int gap = static_cast<int>(std::min<int64_t>(8192, start - written));
                    Media::Check(av_new_packet(encoded.value, gap * channels * 4),
                                 "Allocate alignment silence");
                    std::memset(encoded.value->data, 0, gap * channels * 4);
                    encoded.value->pts = encoded.value->dts =
                        av_rescale_q(written, {1, rate}, out->time_base);
                    encoded.value->duration = av_rescale_q(gap, {1, rate}, out->time_base);
                    encoded.value->stream_index = out->index;
                    encoded.value->flags |= AV_PKT_FLAG_KEY;
                    Media::Check(av_interleaved_write_frame(output.context, encoded.value),
                                 "Write alignment silence");
                    av_packet_unref(encoded.value);
                    written += gap;
                }
                if (count > 0)
                {
                    Media::Check(av_new_packet(encoded.value, count * channels * 4),
                                 "Allocate aligned audio packet");
                    std::memcpy(encoded.value->data,
                                samples.data() + static_cast<size_t>(trim) * channels,
                                count * channels * 4);
                    encoded.value->pts = encoded.value->dts =
                        av_rescale_q(start, {1, rate}, out->time_base);
                    encoded.value->duration = av_rescale_q(count, {1, rate}, out->time_base);
                    encoded.value->stream_index = out->index;
                    encoded.value->flags |= AV_PKT_FLAG_KEY;
                    Media::Check(av_interleaved_write_frame(output.context, encoded.value),
                                 "Write aligned audio");
                    av_packet_unref(encoded.value);
                    written = start + count;
                }
            }
            av_frame_unref(frame);
        }
        if (result != AVERROR(EAGAIN) && result != AVERROR_EOF)
            Media::Check(result, "Receive media frame");
    };
    Media::Packet packet;
    int result;
    while ((result = av_read_frame(source, packet.value)) >= 0)
    {
        Media::Cancelled(cancel);
        auto *p = packet.value;
        int index = p->stream_index;
        auto *in = source->streams[index];
        if (progress && duration > 0 && p->pts != AV_NOPTS_VALUE)
            progress(static_cast<int>(
                std::clamp((p->pts * av_q2d(in->time_base) - origin) / duration * 100, 0.0, 99.0)));
        if (mapping[index] >= 0)
        {
            if (decoders.count(index))
                decode(index, p);
            else
            {
                int64_t base = av_rescale_q(std::llround(origin * AV_TIME_BASE), AV_TIME_BASE_Q,
                                            in->time_base);
                if (p->pts != AV_NOPTS_VALUE)
                    p->pts -= base;
                if (p->dts != AV_NOPTS_VALUE)
                    p->dts -= base;
                auto *out = output.context->streams[mapping[index]];
                av_packet_rescale_ts(p, in->time_base, out->time_base);
                p->stream_index = out->index;
                p->pos = -1;
                Media::Check(av_interleaved_write_frame(output.context, p), "Copy media stream");
            }
        }
        av_packet_unref(p);
    }
    if (result != AVERROR_EOF)
        Media::Check(result, "Read input media");
    for (auto &d : decoders)
        decode(d.first, nullptr);
    if (video.context)
    {
        Media::Check(avcodec_send_frame(video.context, nullptr), "Finish effect video");
        writeVideo();
    }
    Media::Cancelled(cancel);
    Media::Check(av_write_trailer(output.context), "Finalize working copy");
    Media::Check(avio_closep(&output.context->pb), "Close working copy");
    Media::Cancelled(cancel);
    if (!MoveFileExW(staging.c_str(), destination.c_str(), MOVEFILE_WRITE_THROUGH))
        throw std::runtime_error(
            "Cannot publish the working copy; the destination may already exist.");
    output.published = true;
    if (progress)
        progress(100);
}
