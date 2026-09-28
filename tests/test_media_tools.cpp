#include "../src/media_processing.h"
#include "../src/media_processing_internal.h"
#include "../src/openfx_effect.h"
#include "../src/options_window.h"
#include "../src/video_player.h"
#include "test_framework.h"
#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>

extern std::wstring g_testTempDir, g_testVideoPath;
extern HWND g_testHwnd;
namespace
{
std::vector<float> Signal(int size)
{
    std::mt19937 random(42);
    std::vector<float> result(size);
    for (int i = 0; i < size; ++i)
        result[i] = static_cast<float>((static_cast<double>(random()) / random.max() - 0.5) * 0.4);
    return result;
}
std::wstring Path(const wchar_t *name)
{
    return g_testTempDir + L"\\" + name;
}
void CreateAudioFixture(const std::wstring &path, bool includeVideo = false)
{
    AVFormatContext *output = nullptr;
    Media::Check(
        avformat_alloc_output_context2(&output, nullptr, "matroska", Media::Utf8(path).c_str()),
        "Create test fixture");
    for (int i = 0; i < 2; ++i)
    {
        auto *stream = avformat_new_stream(output, nullptr);
        stream->time_base = {1, 8000};
        stream->codecpar->codec_type = AVMEDIA_TYPE_AUDIO;
        stream->codecpar->codec_id = AV_CODEC_ID_PCM_F32LE;
        stream->codecpar->sample_rate = 8000;
        stream->codecpar->format = AV_SAMPLE_FMT_FLT;
        stream->codecpar->bits_per_coded_sample = 32;
        stream->codecpar->block_align = 4;
        av_channel_layout_default(&stream->codecpar->ch_layout, 1);
        av_dict_set(&stream->metadata, "title", i == 0 ? "Reference" : "Delayed recording", 0);
    }
    std::atomic<bool> cancel{false};
    std::unique_ptr<Media::Input> videoSource;
    int videoIndex = -1;
    if (includeVideo)
    {
        videoSource = std::make_unique<Media::Input>(g_testVideoPath, cancel);
        for (unsigned i = 0; i < videoSource->value->nb_streams; ++i)
        {
            auto *stream = videoSource->value->streams[i];
            if (stream->codecpar->codec_type != AVMEDIA_TYPE_VIDEO)
                continue;
            videoIndex = i;
            auto *out = avformat_new_stream(output, nullptr);
            Media::Check(avcodec_parameters_copy(out->codecpar, stream->codecpar),
                         "Copy test video description");
            out->codecpar->codec_tag = 0;
            out->time_base = stream->time_base;
            out->avg_frame_rate = stream->avg_frame_rate;
            break;
        }
    }
    Media::Check(avio_open(&output->pb, Media::Utf8(path).c_str(), AVIO_FLAG_WRITE),
                 "Open fixture");
    Media::Check(avformat_write_header(output, nullptr), "Write fixture header");
    auto a = Signal(8 * 8000);
    std::vector<float> b(a.size());
    for (size_t i = 3400; i < b.size(); ++i)
        b[i] = a[i - 3400] * 0.7f;
    Media::Packet packet;
    for (int position = 0; position < static_cast<int>(a.size()); position += 320)
    {
        for (int track = 0; track < 2; ++track)
        {
            Media::Check(av_new_packet(packet.value, 320 * 4), "Allocate fixture packet");
            std::memcpy(packet.value->data, (track ? b : a).data() + position, 320 * 4);
            packet.value->stream_index = track;
            packet.value->pts = packet.value->dts = av_rescale_q(
                position + (track ? 2000 : 0), {1, 8000}, output->streams[track]->time_base);
            packet.value->duration =
                av_rescale_q(320, {1, 8000}, output->streams[track]->time_base);
            packet.value->flags |= AV_PKT_FLAG_KEY;
            Media::Check(av_interleaved_write_frame(output, packet.value), "Write fixture samples");
        }
    }
    if (videoSource)
    {
        while (av_read_frame(videoSource->value, packet.value) >= 0)
        {
            if (packet.value->stream_index == videoIndex)
            {
                av_packet_rescale_ts(packet.value,
                                     videoSource->value->streams[videoIndex]->time_base,
                                     output->streams[2]->time_base);
                packet.value->stream_index = 2;
                Media::Check(av_interleaved_write_frame(output, packet.value), "Copy test video");
            }
            av_packet_unref(packet.value);
        }
    }
    Media::Check(av_write_trailer(output), "Finalize fixture");
    avio_closep(&output->pb);
    avformat_free_context(output);
}
uint64_t VideoHash(const std::wstring &path)
{
    std::atomic<bool> cancel{false};
    Media::Input input(path, cancel);
    Media::Packet packet;
    uint64_t hash = 1469598103934665603ULL;
    while (av_read_frame(input.value, packet.value) >= 0)
    {
        if (input.value->streams[packet.value->stream_index]->codecpar->codec_type ==
            AVMEDIA_TYPE_VIDEO)
            for (int i = 0; i < packet.value->size; ++i)
                hash = (hash ^ packet.value->data[i]) * 1099511628211ULL;
        av_packet_unref(packet.value);
    }
    return hash;
}
std::vector<uint8_t> FirstPixels(const std::wstring &path)
{
    std::atomic<bool> cancel{false};
    Media::Input input(path, cancel);
    Media::Packet packet;
    int index = av_find_best_stream(input.value, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    Media::Check(index, "Find test video");
    Media::Decoder decoder;
    decoder.Open(input.value->streams[index]);
    while (av_read_frame(input.value, packet.value) >= 0)
    {
        if (packet.value->stream_index == index)
        {
            Media::Check(avcodec_send_packet(decoder.context, packet.value), "Decode test video");
            if (avcodec_receive_frame(decoder.context, decoder.frame) == 0)
            {
                auto *f = decoder.frame;
                std::vector<uint8_t> pixels(static_cast<size_t>(f->width) * f->height * 4);
                auto *sws = sws_getContext(f->width, f->height, (AVPixelFormat)f->format, f->width,
                                           f->height, AV_PIX_FMT_RGBA, SWS_BICUBIC, nullptr,
                                           nullptr, nullptr);
                TEST_ASSERT(sws, "frame converter is available");
                uint8_t *data[] = {pixels.data(), nullptr, nullptr, nullptr};
                int stride[] = {f->width * 4, 0, 0, 0};
                sws_scale(sws, f->data, f->linesize, 0, f->height, data, stride);
                sws_freeContext(sws);
                return pixels;
            }
        }
        av_packet_unref(packet.value);
    }
    throw std::runtime_error("No decoded test video frame.");
}
} // namespace

void RegisterMediaToolsTests(TestSuite &suite)
{
    suite.addTest("MediaTools_AlignmentPositiveNegativeGainPolarity", [] {
        const int rate = 2000;
        auto a = Signal(8 * rate);
        std::vector<float> b(a.size());
        for (size_t i = 725; i < b.size(); ++i)
            b[i] = -a[i - 725] * 0.3f;
        auto match = MatchAudio(a, b, rate, 2);
        TEST_ASSERT(match.matched, "delayed gain/polarity changed recording must match");
        TEST_ASSERT_NEAR(match.offsetSeconds, -0.3625, 1.0 / rate, "late recording must advance");
        auto inverse = MatchAudio(b, a, rate, 2);
        TEST_ASSERT(inverse.matched, "inverse ordering must match");
        TEST_ASSERT_NEAR(inverse.offsetSeconds, 0.3625, 1.0 / rate, "early recording must delay");
    });
    suite.addTest("MediaTools_AlignmentRejectsSilenceTonesUnrelated", [] {
        auto a = Signal(16000);
        std::vector<float> silence(a.size()), tone(a.size()),
            other = Signal(static_cast<int>(a.size()));
        std::reverse(other.begin(), other.end());
        for (size_t i = 0; i < tone.size(); ++i)
            tone[i] = static_cast<float>(std::sin(i * 0.1));
        TEST_ASSERT(!MatchAudio(a, silence, 2000, 2).matched, "silence is not a match");
        TEST_ASSERT(!MatchAudio(a, other, 2000, 2).matched, "unrelated audio is not a match");
        TEST_ASSERT(!MatchAudio(tone, tone, 2000, 2).matched, "periodic tones are ambiguous");
        TEST_ASSERT(!MatchAudio({}, a, 2000).matched, "empty input is not a match");
    });
    suite.addTest("MediaTools_AlignmentHonorsStreamTimestampsAndCopy", [] {
        auto input = Path(L"alignment-input.mkv"), output = Path(L"alignment-output.mkv");
        CreateAudioFixture(input);
        std::atomic<bool> cancel{false};
        auto matches = AnalyzeAudioAlignment(input, 0, cancel, {});
        TEST_ASSERT_EQ(matches.size(), 1u, "one candidate track");
        TEST_ASSERT(matches[0].matched, "fixture must match");
        TEST_ASSERT_NEAR(matches[0].offsetSeconds, -0.675, 0.002,
                         "sample delay plus container timestamp delay");
        ProcessMediaCopy(input, output, {{1, matches[0].offsetSeconds}}, nullptr, cancel, {});
        auto aligned = AnalyzeAudioAlignment(output, 0, cancel, {});
        TEST_ASSERT(aligned[0].matched, "rendered copy must still match");
        TEST_ASSERT_NEAR(aligned[0].offsetSeconds, 0, 0.002, "saved samples must be aligned");
        auto original = AnalyzeAudioAlignment(input, 0, cancel, {});
        TEST_ASSERT_NEAR(original[0].offsetSeconds, -0.675, 0.002,
                         "original source must remain unchanged");
        auto delayed = Path(L"alignment-positive.mkv");
        ProcessMediaCopy(input, delayed, {{0, 0.675}}, nullptr, cancel, {});
        auto matchDelayed = AnalyzeAudioAlignment(delayed, 0, cancel, {});
        TEST_ASSERT(matchDelayed[0].matched, "positive offset copy must match");
        TEST_ASSERT_NEAR(matchDelayed[0].offsetSeconds, 0, 0.002,
                         "positive offsets align the earlier stream");
    });
    suite.addTest("MediaTools_CopyCancellationAndExistingDestination", [] {
        auto input = Path(L"cancel-input.mkv"), output = Path(L"cancel-output.mkv");
        CreateAudioFixture(input);
        std::atomic<bool> cancel{false};
        bool threw = false;
        try
        {
            ProcessMediaCopy(input, output, {{1, -0.2}}, nullptr, cancel,
                             [&](int) { cancel = true; });
        }
        catch (...)
        {
            threw = true;
        }
        TEST_ASSERT(threw && !std::filesystem::exists(output),
                    "cancelled output must not be published");
        for (auto &entry : std::filesystem::directory_iterator(g_testTempDir))
            TEST_ASSERT(entry.path().filename().wstring().find(L"cancel-output.mkv.processing-") ==
                            std::wstring::npos,
                        "cancel removes only its staging file");
        cancel = false;
        {
            std::ofstream sentinel(std::filesystem::path(output), std::ios::binary);
            sentinel << "keep this";
        }
        threw = false;
        try
        {
            ProcessMediaCopy(input, output, {}, nullptr, cancel, {});
        }
        catch (...)
        {
            threw = true;
        }
        TEST_ASSERT(threw, "existing destination must be rejected");
        TEST_ASSERT_EQ(std::filesystem::file_size(output), 9u,
                       "existing destination must be preserved");
    });
    suite.addTest("MediaTools_AlignedVideoCopyLoadsAndPreservesVideo", [] {
        auto output = Path(L"aligned-video.mkv");
        std::atomic<bool> cancel{false};
        ProcessMediaCopy(g_testVideoPath, output, {{1, 0.25}}, nullptr, cancel, {});
        TEST_ASSERT_EQ(VideoHash(output), VideoHash(g_testVideoPath),
                       "alignment must copy video packets unchanged");
        VideoPlayer player(g_testHwnd);
        TEST_ASSERT(player.LoadVideo(output), "aligned working copy must load in editor");
        TEST_ASSERT_EQ(player.GetAudioTrackCount(), 2, "preserve both audio tracks");
        TEST_ASSERT(player.SeekToTime(2.0), "aligned copy must seek normally");
        auto exported = Path(L"aligned-export.mp4");
        TEST_ASSERT(player.CutVideo(exported, 0.0, 4.0, false, false, EncoderSelection::Libx264,
                                    L"Medium", 0, nullptr, &cancel),
                    "aligned PCM working copy must export to MP4");
        Media::Input exportInput(exported, cancel);
        for (unsigned i = 0; i < exportInput.value->nb_streams; ++i)
            if (exportInput.value->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
                TEST_ASSERT_EQ(exportInput.value->streams[i]->codecpar->codec_id, AV_CODEC_ID_AAC,
                               "MP4 gets compatible audio");
    });
    suite.addTest("MediaTools_OpenFxRealPluginParametersAndPixels", [] {
        std::atomic<bool> cancel{false};
        OpenFxEffect effect(OPENFX_TEST_PLUGIN, 0, 8, 4, 30, 1, 1, cancel);
        TEST_ASSERT(!effect.Parameters().empty(), "upstream plugin exposes parameters");
        effect.SetParameter("scale", "0.5");
        std::vector<uint8_t> pixels(8 * 4 * 4);
        for (int i = 0; i < 32; ++i)
        {
            pixels[i * 4] = static_cast<uint8_t>(20 + i * 4);
            pixels[i * 4 + 1] = 80;
            pixels[i * 4 + 2] = 120;
            pixels[i * 4 + 3] = 255;
        }
        auto before = pixels;
        effect.Render(pixels.data(), 8 * 4, 0);
        for (int i = 0; i < 32; ++i)
        {
            TEST_ASSERT_NEAR(pixels[i * 4], before[i * 4] * 0.5, 1,
                             "plugin changes actual pixels without flipping rows");
            TEST_ASSERT_NEAR(pixels[i * 4 + 1], 40, 1, "green channel gain");
        }
        bool threw = false;
        try
        {
            effect.SetParameter("scale", "nan");
        }
        catch (...)
        {
            threw = true;
        }
        TEST_ASSERT(threw, "invalid numeric parameter must be rejected");
    });
    suite.addTest("MediaTools_AlignedCopyKeepsSyncThroughMp4Export", [] {
        auto input = Path(L"sync-video.mkv"), aligned = Path(L"sync-aligned.mkv"),
             exported = Path(L"sync-export.mp4");
        CreateAudioFixture(input, true);
        std::atomic<bool> cancel{false};
        ProcessMediaCopy(input, aligned, {{0, 0.675}}, nullptr, cancel, {});
        VideoPlayer player(g_testHwnd);
        TEST_ASSERT(player.LoadVideo(aligned), "aligned fixture loads");
        TEST_ASSERT(player.CutVideo(exported, 0.0, 4.8, false, true, EncoderSelection::Libx264,
                                    L"Medium", 0, nullptr, &cancel),
                    "aligned copy exports with AAC");
        auto matches = AnalyzeAudioAlignment(exported, 1, cancel, {});
        TEST_ASSERT_EQ(matches.size(), 1u, "export keeps two independent audio tracks");
        TEST_ASSERT(matches[0].matched, "exported tracks still match");
        TEST_ASSERT_NEAR(matches[0].offsetSeconds, 0.0, 0.003,
                         "export preserves alignment within 3 ms");
    });
    suite.addTest("MediaTools_OpenFxCopyPlaysAndRetainsAudio", [] {
        std::atomic<bool> cancel{false};
        VideoPlayer source(g_testHwnd);
        TEST_ASSERT(source.LoadVideo(g_testVideoPath), "fixture loads");
        auto output = Path(L"openfx-video.mkv");
        OpenFxEffect effect(OPENFX_TEST_PLUGIN, 0, source.frameWidth, source.frameHeight,
                            source.frameRate, source.duration, 1, cancel);
        effect.SetParameter("scale", "0.5");
        ProcessMediaCopy(g_testVideoPath, output, {}, &effect, cancel, {});
        VideoPlayer rendered(g_testHwnd);
        TEST_ASSERT(rendered.LoadVideo(output), "rendered OpenFX copy must load in editor");
        TEST_ASSERT_EQ(rendered.GetAudioTrackCount(), source.GetAudioTrackCount(),
                       "effect rendering preserves audio tracks");
        TEST_ASSERT_NEAR(rendered.duration, source.duration, 0.1,
                         "effect rendering preserves duration");
        TEST_ASSERT(VideoHash(output) != VideoHash(g_testVideoPath), "plugin output was encoded");
        auto before = FirstPixels(g_testVideoPath), after = FirstPixels(output);
        TEST_ASSERT_EQ(after.size(), before.size(), "effect preserves frame dimensions");
        for (size_t i = 0; i < after.size(); i += 128)
            TEST_ASSERT_NEAR(after[i], before[i] * 0.5, 1,
                             "saved video contains the actual OpenFX pixels");
        auto exported = Path(L"openfx-export.mp4");
        TEST_ASSERT(rendered.CutVideo(exported, 0.0, 2.0, false, false, EncoderSelection::Libx264,
                                      L"Medium", 0, nullptr, &cancel),
                    "FFV1 working copy must export to MP4 even with Copy Codec selected");
    });
}
