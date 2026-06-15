#include "test_framework.h"
#include "../src/video_player.h"
#include <algorithm>
#include <cmath>

// ============================================================================
// Tests for crop keyframe timeline management
// ============================================================================

extern std::wstring g_testVideoPath;
extern HWND g_testHwnd;

void RegisterCropTests(TestSuite& suite) {

    suite.addTest("Crop_NoCropByDefault", []() {
        VideoPlayer player(g_testHwnd);
        player.LoadVideo(g_testVideoPath);
        TEST_ASSERT(!player.HasAnyCrop(), "No crop should exist by default");
        TEST_ASSERT(!player.hasCrop, "hasCrop should be false by default");
    });

    suite.addTest("Crop_AddKeyframe", []() {
        VideoPlayer player(g_testHwnd);
        player.LoadVideo(g_testVideoPath);
        RECT r = {100, 100, 600, 400};
        bool added = player.AddCropKeyframe(1.0, r);
        TEST_ASSERT(added, "AddCropKeyframe should succeed");
        TEST_ASSERT(player.HasAnyCrop(), "HasAnyCrop should be true after adding keyframe");
    });

    suite.addTest("Crop_KeyframeCount", []() {
        VideoPlayer player(g_testHwnd);
        player.LoadVideo(g_testVideoPath);
        RECT r1 = {100, 100, 600, 400};
        RECT r2 = {200, 200, 800, 500};
        player.AddCropKeyframe(1.0, r1);
        player.AddCropKeyframe(3.0, r2);
        auto times = player.GetCropKeyframeTimes();
        TEST_ASSERT_EQ(static_cast<int>(times.size()), 2, "Should have 2 keyframes");
    });

    suite.addTest("Crop_GetRectForTime_AtKeyframe", []() {
        VideoPlayer player(g_testHwnd);
        player.LoadVideo(g_testVideoPath);
        RECT r = {100, 100, 600, 400};
        player.AddCropKeyframe(1.0, r);
        RECT out;
        bool got = player.GetCropRectForTime(1.0, out);
        TEST_ASSERT(got, "Should get crop rect at keyframe time");
        // Rect values may be adjusted for even dimensions, so we check approximately
        TEST_ASSERT_GE(out.right - out.left, 2, "Crop width should be > 0");
        TEST_ASSERT_GE(out.bottom - out.top, 2, "Crop height should be > 0");
    });

    suite.addTest("Crop_GetRectForTime_AfterKeyframe", []() {
        VideoPlayer player(g_testHwnd);
        player.LoadVideo(g_testVideoPath);
        RECT r = {100, 100, 600, 400};
        player.AddCropKeyframe(1.0, r);
        RECT out;
        bool got = player.GetCropRectForTime(2.5, out);
        TEST_ASSERT(got, "Crop should persist after keyframe time");
    });

    suite.addTest("Crop_GetRectForTime_BeforeKeyframe", []() {
        VideoPlayer player(g_testHwnd);
        player.LoadVideo(g_testVideoPath);
        RECT r = {100, 100, 600, 400};
        player.AddCropKeyframe(2.0, r);
        RECT out;
        bool got = player.GetCropRectForTime(0.5, out);
        TEST_ASSERT(!got, "Should not have crop before first keyframe");
    });

    suite.addTest("Crop_DisabledKeyframe", []() {
        VideoPlayer player(g_testHwnd);
        player.LoadVideo(g_testVideoPath);
        RECT r = {100, 100, 600, 400};
        player.AddCropKeyframe(1.0, r);
        player.AddCropDisabledKeyframe(2.0);
        RECT out;
        bool gotAt1 = player.GetCropRectForTime(1.5, out);
        TEST_ASSERT(gotAt1, "Should have crop between keyframes 1.0 and 2.0");
        bool gotAt2 = player.GetCropRectForTime(2.5, out);
        TEST_ASSERT(!gotAt2, "Should NOT have crop after disabled keyframe");
    });

    suite.addTest("Crop_MultipleKeyframes_CorrectSegments", []() {
        VideoPlayer player(g_testHwnd);
        player.LoadVideo(g_testVideoPath);
        RECT r1 = {100, 100, 500, 300};
        RECT r2 = {200, 200, 800, 500};
        RECT r3 = {50, 50, 400, 250};
        player.AddCropKeyframe(0.5, r1);
        player.AddCropKeyframe(2.0, r2);
        player.AddCropKeyframe(3.5, r3);

        RECT out;
        // Before first keyframe
        bool got0 = player.GetCropRectForTime(0.2, out);
        TEST_ASSERT(!got0, "No crop before first keyframe");

        // At/after first keyframe
        bool got1 = player.GetCropRectForTime(1.0, out);
        TEST_ASSERT(got1, "Should have crop at t=1.0 (after first keyframe)");

        // At/after second keyframe
        bool got2 = player.GetCropRectForTime(3.0, out);
        TEST_ASSERT(got2, "Should have crop at t=3.0 (after second keyframe)");

        auto keyframes = player.GetCropKeyframes();
        TEST_ASSERT_EQ(static_cast<int>(keyframes.size()), 3, "Should have 3 keyframes");
    });

    suite.addTest("Crop_RemoveKeyframe", []() {
        VideoPlayer player(g_testHwnd);
        player.LoadVideo(g_testVideoPath);
        RECT r1 = {100, 100, 500, 300};
        RECT r2 = {200, 200, 800, 500};
        RECT r3 = {50, 50, 400, 250};
        player.AddCropKeyframe(0.5, r1);
        player.AddCropKeyframe(2.0, r2);
        player.AddCropKeyframe(3.5, r3);

        bool removed = player.RemoveCropKeyframe(2.0);
        TEST_ASSERT(removed, "RemoveCropKeyframe should succeed");
        auto times = player.GetCropKeyframeTimes();
        TEST_ASSERT_EQ(static_cast<int>(times.size()), 2, "Should have 2 keyframes after remove");
    });

    suite.addTest("Crop_MoveKeyframe", []() {
        VideoPlayer player(g_testHwnd);
        player.LoadVideo(g_testVideoPath);
        RECT r = {100, 100, 500, 300};
        player.AddCropKeyframe(1.0, r);

        bool moved = player.MoveCropKeyframe(1.0, 3.0);
        TEST_ASSERT(moved, "MoveCropKeyframe should succeed");

        auto keyframes = player.GetCropKeyframes();
        TEST_ASSERT_EQ(static_cast<int>(keyframes.size()), 1, "Should still have 1 keyframe");
        TEST_ASSERT_NEAR(keyframes[0].time, 3.0, 0.1, "Keyframe should be at new time");
    });

    suite.addTest("Crop_MoveKeyframe_SameTime", []() {
        VideoPlayer player(g_testHwnd);
        player.LoadVideo(g_testVideoPath);
        RECT r = {100, 100, 500, 300};
        player.AddCropKeyframe(1.0, r);

        bool moved = player.MoveCropKeyframe(1.0, 1.0);
        TEST_ASSERT(!moved, "MoveCropKeyframe to same time should return false");
    });

    suite.addTest("Crop_ClearKeyframes", []() {
        VideoPlayer player(g_testHwnd);
        player.LoadVideo(g_testVideoPath);
        RECT r = {100, 100, 500, 300};
        player.AddCropKeyframe(1.0, r);
        player.AddCropKeyframe(2.0, r);
        player.ClearCropKeyframes();
        TEST_ASSERT(!player.HasAnyCrop(), "HasAnyCrop should be false after clear");
        auto times = player.GetCropKeyframeTimes();
        TEST_ASSERT_EQ(static_cast<int>(times.size()), 0, "Should have 0 keyframes after clear");
    });

    suite.addTest("Crop_EvenDimensions", []() {
        VideoPlayer player(g_testHwnd);
        player.LoadVideo(g_testVideoPath);
        // Provide odd-pixel crop — should be adjusted to even
        RECT r = {101, 101, 601, 401}; // 500x300 → both even but offset is odd
        player.AddCropKeyframe(1.0, r);
        RECT out;
        bool got = player.GetCropRectForTime(1.0, out);
        if (got) {
            LONG width = out.right - out.left;
            LONG height = out.bottom - out.top;
            TEST_ASSERT_EQ(width % 2, (LONG)0, "Crop width must be even for H.264");
            TEST_ASSERT_EQ(height % 2, (LONG)0, "Crop height must be even for H.264");
        }
    });

    suite.addTest("Crop_UpdateForTime", []() {
        VideoPlayer player(g_testHwnd);
        player.LoadVideo(g_testVideoPath);
        RECT r = {100, 100, 500, 300};
        player.AddCropKeyframe(1.0, r);

        player.UpdateCropForTime(0.5);
        TEST_ASSERT(!player.hasCrop, "Should not have crop before keyframe");

        player.UpdateCropForTime(1.5);
        TEST_ASSERT(player.hasCrop, "Should have crop after keyframe");
    });

    suite.addTest("Crop_OutputDimensions", []() {
        VideoPlayer player(g_testHwnd);
        player.LoadVideo(g_testVideoPath);
        RECT r = {100, 100, 500, 400}; // 400x300
        player.AddCropKeyframe(1.0, r);
        int w = player.GetCropOutputWidth();
        int h = player.GetCropOutputHeight();
        TEST_ASSERT_GT(w, 0, "Crop output width should be > 0");
        TEST_ASSERT_GT(h, 0, "Crop output height should be > 0");
        TEST_ASSERT_EQ(w % 2, 0, "Crop output width must be even");
        TEST_ASSERT_EQ(h % 2, 0, "Crop output height must be even");
    });

    suite.addTest("Crop_SortedOrder", []() {
        VideoPlayer player(g_testHwnd);
        player.LoadVideo(g_testVideoPath);
        RECT r = {100, 100, 500, 300};
        // Add in reverse order
        player.AddCropKeyframe(3.0, r);
        player.AddCropKeyframe(1.0, r);
        player.AddCropKeyframe(2.0, r);

        auto times = player.GetCropKeyframeTimes();
        TEST_ASSERT_EQ(static_cast<int>(times.size()), 3, "Should have 3 keyframes");
        for (size_t i = 1; i < times.size(); i++) {
            TEST_ASSERT(times[i] >= times[i-1], "Keyframes should be sorted by time");
        }
    });
}
