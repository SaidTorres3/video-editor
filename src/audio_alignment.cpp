#include "media_processing.h"
#include "media_processing_internal.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
extern "C"
{
#include <libavutil/tx.h>
}

namespace
{
struct Transform
{
    AVTXContext *context = nullptr;
    av_tx_fn execute = nullptr;
    Transform(int length, bool inverse)
    {
        Media::Check(av_tx_init(&context, &execute, AV_TX_FLOAT_FFT, inverse, length, nullptr,
                                AV_TX_UNALIGNED),
                     "Initialize correlation transform");
    }
    ~Transform()
    {
        av_tx_uninit(&context);
    }
};
} // namespace

AudioAlignment MatchAudio(const std::vector<float> &reference, const std::vector<float> &candidate,
                          int rate, double maxOffset)
{
    AudioAlignment result;
    if (rate <= 0 || !std::isfinite(maxOffset) || maxOffset < 0 ||
        reference.size() < static_cast<size_t>(rate * 3) ||
        candidate.size() < static_cast<size_t>(rate * 3))
        return result;
    // Bound work and allocation even when called outside the analysis UI.
    if (reference.size() > 2000000 || candidate.size() > 2000000)
        throw std::runtime_error("Audio analysis window is too large.");
    int size = 1;
    while (size < reference.size() + candidate.size())
        size *= 2;
    std::vector<AVComplexFloat> a(size), b(size), fa(size), fb(size);
    std::vector<double> sumsA(reference.size() + 1), sumsB(candidate.size() + 1);
    std::vector<double> energyA(reference.size() + 1), energyB(candidate.size() + 1);
    auto prepare = [](const auto &samples, auto &fft, auto &sums, auto &energy) {
        for (size_t i = 0; i < samples.size(); ++i)
        {
            const double value = std::isfinite(samples[i]) ? samples[i] : 0.0;
            fft[i].re = static_cast<float>(value);
            sums[i + 1] = sums[i] + value;
            energy[i + 1] = energy[i] + value * value;
        }
    };
    prepare(reference, a, sumsA, energyA);
    prepare(candidate, b, sumsB, energyB);
    Transform forward(size, false), inverse(size, true);
    forward.execute(forward.context, fa.data(), a.data(), sizeof(AVComplexFloat));
    forward.execute(forward.context, fb.data(), b.data(), sizeof(AVComplexFloat));
    for (int i = 0; i < size; ++i)
    {
        // conj(reference) * candidate: positive lag means candidate is late.
        a[i] = {fa[i].re * fb[i].re + fa[i].im * fb[i].im,
                fa[i].re * fb[i].im - fa[i].im * fb[i].re};
    }
    inverse.execute(inverse.context, b.data(), a.data(), sizeof(AVComplexFloat));
    int limit = static_cast<int>(
        std::min(maxOffset * rate,
                 static_cast<double>(std::min(reference.size(), candidate.size()) - rate * 3)));
    std::vector<double> scores(2 * limit + 1);
    int bestLag = 0;
    double best = 0;
    for (int lag = -limit; lag <= limit; ++lag)
    {
        int beginA = std::max(0, -lag), beginB = std::max(0, lag);
        int count =
            static_cast<int>(std::min(reference.size() - beginA, candidate.size() - beginB));
        double sa = sumsA[beginA + count] - sumsA[beginA];
        double sb = sumsB[beginB + count] - sumsB[beginB];
        double ea = energyA[beginA + count] - energyA[beginA] - sa * sa / count;
        double eb = energyB[beginB + count] - energyB[beginB] - sb * sb / count;
        if (ea < count * 1e-9 || eb < count * 1e-9)
            continue;
        double cross = b[lag < 0 ? size + lag : lag].re / size - sa * sb / count;
        double score = std::min(1.0, std::abs(cross) / std::sqrt(ea * eb));
        scores[lag + limit] = score;
        if (score > best)
        {
            best = score;
            bestLag = lag;
        }
    }
    double runnerUp = 0;
    for (int lag = -limit; lag <= limit; ++lag)
        if (std::abs(lag - bestLag) > rate / 20)
            runnerUp = std::max(runnerUp, scores[lag + limit]);
    result.confidence = best;
    // Silence, unrelated audio and repetitive tones must not silently move tracks.
    result.matched =
        best >= 0.35 && best - runnerUp >= 0.08 && (limit == 0 || std::abs(bestLag) < limit);
    if (result.matched)
        result.offsetSeconds = -static_cast<double>(bestLag) / rate;
    return result;
}

std::vector<AudioAlignment> AnalyzeAudioAlignment(const std::wstring &filename, int referenceStream,
                                                  std::atomic<bool> &cancel,
                                                  const MediaProgress &progress)
{
    constexpr int rate = 8000;
    constexpr double seconds = 120.0;
    Media::Input input(filename, cancel);
    auto *format = input.value;
    const double origin = Media::Origin(format);
    struct Track
    {
        Media::Decoder decoder;
        std::vector<float> samples;
        int64_t nextSample = 0;
        bool started = false;
        bool complete = false;
    };
    std::map<int, std::unique_ptr<Track>> tracks;
    AVChannelLayout mono = AV_CHANNEL_LAYOUT_MONO;
    for (unsigned i = 0; i < format->nb_streams; ++i)
    {
        if (format->streams[i]->codecpar->codec_type != AVMEDIA_TYPE_AUDIO)
            continue;
        if (tracks.size() >= 32)
            throw std::runtime_error("Alignment supports up to 32 audio tracks.");
        auto track = std::make_unique<Track>();
        track->samples.resize(static_cast<size_t>(seconds * rate));
        track->decoder.Open(format->streams[i]);
        track->decoder.Resample(rate, mono);
        tracks.emplace(i, std::move(track));
    }
    if (!tracks.count(referenceStream) || tracks.size() < 2)
        throw std::runtime_error(
            "Select a reference track in a video with at least two audio tracks.");
    auto consume = [&](int index, AVPacket *packet) {
        auto &track = *tracks.at(index);
        if (track.complete)
            return;
        auto &decoder = track.decoder;
        Media::Check(avcodec_send_packet(decoder.context, packet), "Decode analysis audio");
        int status;
        while ((status = avcodec_receive_frame(decoder.context, decoder.frame)) >= 0)
        {
            Media::Cancelled(cancel);
            auto *frame = decoder.frame;
            int64_t timestamp = frame->best_effort_timestamp;
            int64_t start =
                timestamp == AV_NOPTS_VALUE
                    ? track.nextSample
                    : std::llround(
                          (timestamp * av_q2d(format->streams[index]->time_base) - origin) * rate);
            // Account for the resampler's retained input at the start of this frame.
            start -= av_rescale_rnd(swr_get_delay(decoder.resampler, decoder.context->sample_rate),
                                    rate, decoder.context->sample_rate, AV_ROUND_NEAR_INF);
            int count = swr_get_out_samples(decoder.resampler, frame->nb_samples);
            std::vector<float> converted(count);
            uint8_t *destination = reinterpret_cast<uint8_t *>(converted.data());
            count =
                swr_convert(decoder.resampler, &destination, count,
                            const_cast<const uint8_t **>(frame->extended_data), frame->nb_samples);
            Media::Check(count, "Resample analysis audio");
            for (int i = 0; i < count; ++i)
                if (start + i >= 0 && start + i < static_cast<int64_t>(track.samples.size()))
                    track.samples[start + i] = converted[i];
            track.nextSample = start + count;
            track.started = true;
            track.complete = track.nextSample >= static_cast<int64_t>(track.samples.size());
            if (progress)
                progress(static_cast<int>(
                    std::clamp(track.nextSample / (seconds * rate) * 70, 0.0, 70.0)));
            av_frame_unref(frame);
        }
        if (status != AVERROR(EAGAIN) && status != AVERROR_EOF)
            Media::Check(status, "Read analysis audio");
    };
    Media::Packet packet;
    int readStatus;
    while ((readStatus = av_read_frame(format, packet.value)) >= 0)
    {
        Media::Cancelled(cancel);
        int index = packet.value->stream_index;
        if (tracks.count(index))
            consume(index, packet.value);
        const int64_t packetTime =
            packet.value->dts != AV_NOPTS_VALUE ? packet.value->dts : packet.value->pts;
        const bool beyondWindow =
            packetTime != AV_NOPTS_VALUE &&
            packetTime * av_q2d(format->streams[index]->time_base) - origin > seconds + 2.0;
        av_packet_unref(packet.value);
        // A shorter or empty track must not make us demux a multi-hour video
        // after every available sample in the analysis window has been read.
        if (beyondWindow || std::all_of(tracks.begin(), tracks.end(),
                                        [](const auto &entry) { return entry.second->complete; }))
            break;
    }
    if (readStatus < 0 && readStatus != AVERROR_EOF)
        Media::Check(readStatus, "Read audio input");
    for (auto &entry : tracks)
    {
        consume(entry.first, nullptr);
        entry.second->samples.resize(static_cast<size_t>(
            std::clamp<int64_t>(entry.second->nextSample, 0, entry.second->samples.size())));
    }
    std::vector<AudioAlignment> result;
    for (auto &entry : tracks)
    {
        Media::Cancelled(cancel);
        if (entry.first == referenceStream)
            continue;
        auto match = MatchAudio(tracks.at(referenceStream)->samples, entry.second->samples, rate);
        match.streamIndex = entry.first;
        result.push_back(match);
        if (progress)
            progress(70 + static_cast<int>(30 * result.size() / (tracks.size() - 1)));
    }
    return result;
}
