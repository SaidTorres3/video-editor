#include "test_framework.h"
#include "../src/utils.h"

// ============================================================================
// Unit tests for FormatTime() and ParseTimeString()
// ============================================================================

void RegisterUtilsTests(TestSuite& suite) {

    suite.addTest("FormatTime_ZeroSeconds", []() {
        std::wstring result = FormatTime(0.0);
        TEST_ASSERT(result == L"00:00", "FormatTime(0) should be 00:00");
    });

    suite.addTest("FormatTime_UnderOneMinute", []() {
        std::wstring result = FormatTime(45.0);
        TEST_ASSERT(result == L"00:45", "FormatTime(45) should be 00:45");
    });

    suite.addTest("FormatTime_ExactMinute", []() {
        std::wstring result = FormatTime(60.0);
        TEST_ASSERT(result == L"01:00", "FormatTime(60) should be 01:00");
    });

    suite.addTest("FormatTime_WithHours", []() {
        std::wstring result = FormatTime(3661.0);
        TEST_ASSERT(result == L"1:01:01", "FormatTime(3661) should be 1:01:01");
    });

    suite.addTest("FormatTime_WithMilliseconds", []() {
        std::wstring result = FormatTime(65.5, true);
        TEST_ASSERT(result == L"01:05.50", "FormatTime(65.5, true) should be 01:05.50");
    });

    suite.addTest("FormatTime_WithHoursAndMilliseconds", []() {
        std::wstring result = FormatTime(3723.5, true);
        TEST_ASSERT(result == L"1:02:03.50", "FormatTime(3723.5, true) should be 1:02:03.50");
    });

    suite.addTest("FormatTime_NegativeClamp", []() {
        std::wstring result = FormatTime(-5.0);
        TEST_ASSERT(result == L"00:00", "FormatTime(-5) should clamp to 00:00");
    });

    suite.addTest("FormatTime_LargeValue", []() {
        std::wstring result = FormatTime(36000.0); // 10 hours
        TEST_ASSERT(result == L"10:00:00", "FormatTime(36000) should be 10:00:00");
    });

    suite.addTest("ParseTimeString_HMS", []() {
        double result = ParseTimeString(L"1:02:03");
        TEST_ASSERT_NEAR(result, 3723.0, 0.01, "ParseTimeString H:M:S");
    });

    suite.addTest("ParseTimeString_HMS_WithFraction", []() {
        double result = ParseTimeString(L"1:02:03.5");
        TEST_ASSERT_NEAR(result, 3723.5, 0.01, "ParseTimeString H:M:S.f");
    });

    suite.addTest("ParseTimeString_MS", []() {
        double result = ParseTimeString(L"5:30");
        TEST_ASSERT_NEAR(result, 330.0, 0.01, "ParseTimeString M:S");
    });

    suite.addTest("ParseTimeString_MS_WithFraction", []() {
        double result = ParseTimeString(L"1:30.75");
        TEST_ASSERT_NEAR(result, 90.75, 0.01, "ParseTimeString M:S.f");
    });

    suite.addTest("ParseTimeString_Invalid", []() {
        double result = ParseTimeString(L"abc");
        TEST_ASSERT_NEAR(result, -1.0, 0.01, "ParseTimeString invalid input should return -1");
    });

    suite.addTest("ParseTimeString_Empty", []() {
        double result = ParseTimeString(L"");
        TEST_ASSERT_NEAR(result, -1.0, 0.01, "ParseTimeString empty should return -1");
    });

    suite.addTest("ParseTimeString_Zero", []() {
        double result = ParseTimeString(L"0:00");
        TEST_ASSERT_NEAR(result, 0.0, 0.01, "ParseTimeString 0:00 should be 0");
    });
}
