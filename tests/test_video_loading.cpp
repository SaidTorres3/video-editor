#include "test_framework.h"
#include "../src/video_player.h"

// ============================================================================
// Integration tests for video loading, metadata, and unloading
// ============================================================================

// The test video path is set by test_main.cpp before this suite runs
extern std::wstring g_testVideoPath;
extern HWND g_testHwnd;

void RegisterVideoLoadingTests(TestSuite& suite) {

    suite.addTest("LoadVideo_Success", []() {
        VideoPlayer player(g_testHwnd);
        bool result = player.LoadVideo(g_testVideoPath);
        TEST_ASSERT(result, "LoadVideo should succeed for valid test video");
        TEST_ASSERT(player.IsLoaded(), "IsLoaded should be true after successful load");
    });

    suite.addTest("LoadVideo_InvalidFile", []() {
        VideoPlayer player(g_testHwnd);
        bool result = player.LoadVideo(L"C:\\nonexistent_test_video_12345.mp4");
        TEST_ASSERT(!result, "LoadVideo should fail for nonexistent file");
        TEST_ASSERT(!player.IsLoaded(), "IsLoaded should be false for failed load");
    });

    suite.addTest("LoadVideo_Duration", []() {
        VideoPlayer player(g_testHwnd);
        player.LoadVideo(g_testVideoPath);
        double dur = player.GetDuration();
        TEST_ASSERT_NEAR(dur, 5.0, 0.5, "Duration should be ~5.0 seconds");
    });

    suite.addTest("LoadVideo_FrameRate", []() {
        VideoPlayer player(g_testHwnd);
        player.LoadVideo(g_testVideoPath);
        TEST_ASSERT_NEAR(player.frameRate, 30.0, 1.0, "Frame rate should be ~30 fps");
    });

    suite.addTest("LoadVideo_Dimensions", []() {
        VideoPlayer player(g_testHwnd);
        player.LoadVideo(g_testVideoPath);
        TEST_ASSERT_EQ(player.frameWidth, 1280, "Frame width should be 1280");
        TEST_ASSERT_EQ(player.frameHeight, 720, "Frame height should be 720");
    });

    suite.addTest("LoadVideo_TotalFrames", []() {
        VideoPlayer player(g_testHwnd);
        player.LoadVideo(g_testVideoPath);
        int64_t total = player.GetTotalFrames();
        TEST_ASSERT_GT(total, (int64_t)100, "Total frames should be > 100");
        TEST_ASSERT_LT(total, (int64_t)200, "Total frames should be < 200");
    });

    suite.addTest("LoadVideo_AudioTracks", []() {
        VideoPlayer player(g_testHwnd);
        player.LoadVideo(g_testVideoPath);
        int count = player.GetAudioTrackCount();
        TEST_ASSERT_GE(count, 1, "Should have at least 1 audio track");
    });

    suite.addTest("LoadVideo_HasFormatContext", []() {
        VideoPlayer player(g_testHwnd);
        player.LoadVideo(g_testVideoPath);
        TEST_ASSERT(player.formatContext != nullptr, "formatContext should be non-null after load");
        TEST_ASSERT(player.codecContext != nullptr, "codecContext should be non-null after load");
    });

    suite.addTest("UnloadVideo_ClearsState", []() {
        VideoPlayer player(g_testHwnd);
        player.LoadVideo(g_testVideoPath);
        TEST_ASSERT(player.IsLoaded(), "Should be loaded before unload");
        player.UnloadVideo();
        TEST_ASSERT(!player.IsLoaded(), "Should not be loaded after unload");
        TEST_ASSERT_NEAR(player.GetDuration(), 0.0, 0.01, "Duration should be 0 after unload");
        TEST_ASSERT_EQ(player.GetTotalFrames(), (int64_t)0, "TotalFrames should be 0 after unload");
    });

    suite.addTest("LoadVideo_ReloadAfterUnload", []() {
        VideoPlayer player(g_testHwnd);
        player.LoadVideo(g_testVideoPath);
        player.UnloadVideo();
        bool result = player.LoadVideo(g_testVideoPath);
        TEST_ASSERT(result, "Should be able to reload after unload");
        TEST_ASSERT(player.IsLoaded(), "Should be loaded after reload");
    });
}
