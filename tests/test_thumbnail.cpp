#include "test_framework.h"
#include "../src/video_player.h"
#include <vector>
#include <cstdint>
#include <algorithm>

// ============================================================================
// Tests for thumbnail generation
// ============================================================================

extern std::wstring g_testVideoPath;
extern HWND g_testHwnd;

void RegisterThumbnailTests(TestSuite& suite) {

    suite.addTest("Thumbnail_Basic", []() {
        VideoPlayer player(g_testHwnd);
        player.LoadVideo(g_testVideoPath);
        std::vector<uint8_t> pixels;
        bool result = player.GetThumbnailPixels(1.0, 160, 90, pixels);
        TEST_ASSERT(result, "GetThumbnailPixels should succeed");
    });

    suite.addTest("Thumbnail_PixelDataSize", []() {
        VideoPlayer player(g_testHwnd);
        player.LoadVideo(g_testVideoPath);
        std::vector<uint8_t> pixels;
        player.GetThumbnailPixels(1.0, 160, 90, pixels);
        size_t expected = 160 * 90 * 4; // BGRA
        TEST_ASSERT_EQ(pixels.size(), expected, "Pixel buffer should be 160*90*4 bytes");
    });

    suite.addTest("Thumbnail_NotAllBlack", []() {
        VideoPlayer player(g_testHwnd);
        player.LoadVideo(g_testVideoPath);
        std::vector<uint8_t> pixels;
        player.GetThumbnailPixels(2.0, 160, 90, pixels);

        // Check that not every pixel is black (R=G=B=0)
        bool allBlack = true;
        for (size_t i = 0; i < pixels.size(); i += 4) {
            if (pixels[i] != 0 || pixels[i+1] != 0 || pixels[i+2] != 0) {
                allBlack = false;
                break;
            }
        }
        TEST_ASSERT(!allBlack, "Thumbnail should not be all-black (test video has colored content)");
    });

    suite.addTest("Thumbnail_DifferentSizes", []() {
        VideoPlayer player(g_testHwnd);
        player.LoadVideo(g_testVideoPath);

        std::vector<uint8_t> smallThumb;
        std::vector<uint8_t> largeThumb;
        bool r1 = player.GetThumbnailPixels(1.0, 80, 45, smallThumb);
        bool r2 = player.GetThumbnailPixels(1.0, 320, 180, largeThumb);
        TEST_ASSERT(r1, "Small thumbnail should succeed");
        TEST_ASSERT(r2, "Large thumbnail should succeed");
        TEST_ASSERT_EQ(smallThumb.size(), (size_t)(80 * 45 * 4), "Small thumbnail size correct");
        TEST_ASSERT_EQ(largeThumb.size(), (size_t)(320 * 180 * 4), "Large thumbnail size correct");
    });

    suite.addTest("Thumbnail_DifferentTimes", []() {
        VideoPlayer player(g_testHwnd);
        player.LoadVideo(g_testVideoPath);

        std::vector<uint8_t> t1, t2;
        // Use timestamps far apart to maximize chance of hitting different keyframes
        player.GetThumbnailPixels(0.0, 160, 90, t1);
        player.GetThumbnailPixels(4.0, 160, 90, t2);

        // Both should succeed and have correct size
        TEST_ASSERT_EQ(t1.size(), (size_t)(160 * 90 * 4), "First thumbnail size correct");
        TEST_ASSERT_EQ(t2.size(), (size_t)(160 * 90 * 4), "Second thumbnail size correct");
        // Note: with keyframe-based seeking, thumbnails at different times MAY
        // return identical frames if they hit the same keyframe. This is acceptable
        // behavior. We primarily verify both calls succeed without error.
    });

    suite.addTest("Thumbnail_Fast", []() {
        VideoPlayer player(g_testHwnd);
        player.LoadVideo(g_testVideoPath);
        std::vector<uint8_t> pixels;
        bool result = player.GetThumbnailPixelsFast(1.5, 160, 90, pixels);
        TEST_ASSERT(result, "GetThumbnailPixelsFast should succeed");
        TEST_ASSERT_EQ(pixels.size(), (size_t)(160 * 90 * 4), "Fast thumbnail size correct");
    });

    suite.addTest("Thumbnail_Fast_NotAllBlack", []() {
        VideoPlayer player(g_testHwnd);
        player.LoadVideo(g_testVideoPath);
        std::vector<uint8_t> pixels;
        player.GetThumbnailPixelsFast(2.0, 160, 90, pixels);

        bool allBlack = true;
        for (size_t i = 0; i < pixels.size(); i += 4) {
            if (pixels[i] != 0 || pixels[i+1] != 0 || pixels[i+2] != 0) {
                allBlack = false;
                break;
            }
        }
        TEST_ASSERT(!allBlack, "Fast thumbnail should not be all-black");
    });

    suite.addTest("Thumbnail_NotLoaded", []() {
        VideoPlayer player(g_testHwnd);
        // Don't load a video
        std::vector<uint8_t> pixels;
        bool result = player.GetThumbnailPixels(1.0, 160, 90, pixels);
        TEST_ASSERT(!result, "Thumbnail should fail when no video loaded");
    });

    suite.addTest("Thumbnail_InvalidSize", []() {
        VideoPlayer player(g_testHwnd);
        player.LoadVideo(g_testVideoPath);
        std::vector<uint8_t> pixels;
        bool result = player.GetThumbnailPixels(1.0, 0, 0, pixels);
        TEST_ASSERT(!result, "Thumbnail with 0x0 should fail");
    });
}
