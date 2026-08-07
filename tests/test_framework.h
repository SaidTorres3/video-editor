#pragma once

#include <string>
#include <vector>
#include <functional>
#include <iostream>
#include <cmath>
#include <sstream>
#include <iomanip>
#include <cstdlib>
#include <windows.h>

// ============================================================================
// Minimal test framework — no external dependencies required
// ============================================================================

// ANSI color codes for console output
namespace TestColors {
    inline void SetGreen()  { SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), FOREGROUND_GREEN | FOREGROUND_INTENSITY); }
    inline void SetRed()    { SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), FOREGROUND_RED | FOREGROUND_INTENSITY); }
    inline void SetYellow() { SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY); }
    inline void SetCyan()   { SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY); }
    inline void SetWhite()  { SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE); }
    inline void Reset()     { SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE); }
}

// Exception thrown on test failure
struct TestFailure {
    std::string message;
    std::string file;
    int line;
    TestFailure(const std::string& msg, const std::string& f, int l) : message(msg), file(f), line(l) {}
};

// Assertion macros
#define TEST_ASSERT(cond, msg) \
    do { if (!(cond)) throw TestFailure(std::string(msg) + " [" #cond "]", __FILE__, __LINE__); } while(0)

#define TEST_ASSERT_EQ(a, b, msg) \
    do { \
        auto _a = (a); auto _b = (b); \
        if (_a != _b) { \
            std::ostringstream _oss; \
            _oss << msg << " — expected: " << _b << ", got: " << _a; \
            throw TestFailure(_oss.str(), __FILE__, __LINE__); \
        } \
    } while(0)

#define TEST_ASSERT_NEAR(a, b, tol, msg) \
    do { \
        double _a = static_cast<double>(a); double _b = static_cast<double>(b); double _t = static_cast<double>(tol); \
        if (std::fabs(_a - _b) > _t) { \
            std::ostringstream _oss; \
            _oss << msg << " — expected: " << _b << " ±" << _t << ", got: " << _a; \
            throw TestFailure(_oss.str(), __FILE__, __LINE__); \
        } \
    } while(0)

#define TEST_ASSERT_GT(a, b, msg) \
    do { \
        auto _a = (a); auto _b = (b); \
        if (!(_a > _b)) { \
            std::ostringstream _oss; \
            _oss << msg << " — expected > " << _b << ", got: " << _a; \
            throw TestFailure(_oss.str(), __FILE__, __LINE__); \
        } \
    } while(0)

#define TEST_ASSERT_LT(a, b, msg) \
    do { \
        auto _a = (a); auto _b = (b); \
        if (!(_a < _b)) { \
            std::ostringstream _oss; \
            _oss << msg << " — expected < " << _b << ", got: " << _a; \
            throw TestFailure(_oss.str(), __FILE__, __LINE__); \
        } \
    } while(0)

#define TEST_ASSERT_GE(a, b, msg) \
    do { \
        auto _a = (a); auto _b = (b); \
        if (!(_a >= _b)) { \
            std::ostringstream _oss; \
            _oss << msg << " — expected >= " << _b << ", got: " << _a; \
            throw TestFailure(_oss.str(), __FILE__, __LINE__); \
        } \
    } while(0)

// A single test case
struct TestCase {
    std::string name;
    std::function<void()> func;
};

// A named collection of test cases
class TestSuite {
public:
    std::string name;
    std::vector<TestCase> tests;
    int passed = 0;
    int failed = 0;
    std::vector<std::string> failures;

    TestSuite(const std::string& suiteName) : name(suiteName) {}

    void addTest(const std::string& testName, std::function<void()> fn) {
        tests.push_back({testName, fn});
    }

    void run() {
        TestColors::SetCyan();
        std::cout << "\n========================================" << std::endl;
        std::cout << "  " << name << std::endl;
        std::cout << "========================================" << std::endl;
        TestColors::Reset();

        char* filterValue = nullptr;
        size_t filterLength = 0;
        _dupenv_s(&filterValue, &filterLength, "VIDEO_EDITOR_TEST_FILTER");
        const std::string filter = filterValue ? filterValue : "";
        std::free(filterValue);

        for (auto& tc : tests) {
            if (!filter.empty() && tc.name.find(filter) == std::string::npos)
                continue;
            std::cout << "  ";
            try {
                tc.func();
                TestColors::SetGreen();
                std::cout << "[PASS] ";
                TestColors::Reset();
                std::cout << tc.name << std::endl;
                passed++;
            } catch (const TestFailure& f) {
                TestColors::SetRed();
                std::cout << "[FAIL] ";
                TestColors::Reset();
                std::cout << tc.name << std::endl;
                TestColors::SetRed();
                std::cout << "         " << f.message << std::endl;
                std::cout << "         at " << f.file << ":" << f.line << std::endl;
                TestColors::Reset();
                failures.push_back(tc.name + ": " + f.message);
                failed++;
            } catch (const std::exception& e) {
                TestColors::SetRed();
                std::cout << "[FAIL] ";
                TestColors::Reset();
                std::cout << tc.name << std::endl;
                TestColors::SetRed();
                std::cout << "         Exception: " << e.what() << std::endl;
                TestColors::Reset();
                failures.push_back(tc.name + ": Exception: " + e.what());
                failed++;
            } catch (...) {
                TestColors::SetRed();
                std::cout << "[FAIL] ";
                TestColors::Reset();
                std::cout << tc.name << std::endl;
                TestColors::SetRed();
                std::cout << "         Unknown exception" << std::endl;
                TestColors::Reset();
                failures.push_back(tc.name + ": Unknown exception");
                failed++;
            }
        }

        std::cout << "  ----" << std::endl;
        std::cout << "  " << passed << " passed, " << failed << " failed" << std::endl;
    }
};

// Global test runner that collects all suite results
class TestRunner {
public:
    std::vector<TestSuite*> suites;
    
    void addSuite(TestSuite* suite) {
        suites.push_back(suite);
    }

    int runAll() {
        int totalPassed = 0, totalFailed = 0;
        std::vector<std::string> allFailures;

        for (auto* suite : suites) {
            suite->run();
            totalPassed += suite->passed;
            totalFailed += suite->failed;
            for (auto& f : suite->failures)
                allFailures.push_back("[" + suite->name + "] " + f);
        }

        std::cout << "\n";
        TestColors::SetCyan();
        std::cout << "========================================" << std::endl;
        std::cout << "  RESULTS SUMMARY" << std::endl;
        std::cout << "========================================" << std::endl;
        TestColors::Reset();

        if (totalFailed == 0) {
            TestColors::SetGreen();
            std::cout << "  ALL " << totalPassed << " TESTS PASSED" << std::endl;
            TestColors::Reset();
        } else {
            TestColors::SetRed();
            std::cout << "  " << totalFailed << " FAILED";
            TestColors::Reset();
            std::cout << ", " << totalPassed << " passed" << std::endl;
            std::cout << std::endl;
            TestColors::SetRed();
            std::cout << "  Failed tests:" << std::endl;
            for (auto& f : allFailures)
                std::cout << "    - " << f << std::endl;
            TestColors::Reset();
        }

        std::cout << std::endl;
        return totalFailed > 0 ? 1 : 0;
    }
};
