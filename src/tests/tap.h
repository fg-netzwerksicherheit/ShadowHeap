#pragma once
#include <iostream>

/// A very simple test framework based on TAP.
/// Prints output to std::cout.

class TAP {
    size_t level;
    size_t count_expected;
    size_t count_total = 0;
    size_t count_pass = 0;
    size_t count_fail = 0;

    std::ostream& out() const {
        return std::cout << std::string(level, ' ');
    }

public:
    /// Create a new test reporter.
    /// Plan a specific number of test.
    /// May optionally take an intendation level for subtests.
    explicit TAP(size_t plan, size_t level = 0)
        : level{ level }, count_expected{ plan } {
        out() << "1.." << plan << std::endl;
    }

    TAP(TAP const&) = delete;  // avoid accidental copies

    ~TAP() {
        if (count_expected != count_total)
            note() << "WARNING: expected " << count_expected << " tests"
                   << " but ran " << count_total << std::endl;
    }

    /// True if no tests failed.

    bool result_ok() const {
        return count_fail == 0;
    }

    /// Print summary. Returns true if no tests failed.

    bool print_result() const {
        bool ok = result_ok();
        note() << (ok ? "PASS" : "FAIL") << " --"
               << " ran " << count_total << " tests"
               << " with " << count_fail << " failures" << std::endl;
        return ok;
    }

    /// Get output stream to which notes can be written.
    /// Must end with a newline and not contain further newlines.

    std::ostream& note() const {
        return out() << "# ";
    }

    /// Register a test result (true if passed, false if failed).

    bool ok(bool result, const char* name) {
        if (result) {
            pass(name);
        } else {
            fail(name);
        }
        return result;
    }

    /// Register a passed test.

    void pass(const char* name) {
        count_total++;
        count_pass++;
        out() << "ok " << count_total << " - " << name << std::endl;
    }

    /// Register a failed test.

    void fail(const char* name) {
        count_total++;
        count_fail++;
        out() << "not ok " << count_total << " - " << name << std::endl;
    }

    /// Check that two values are equal

    template <class Left, class Right>
    bool ok_eq(Left&& left, Right&& right, const char* name) {
        bool is_ok = ok(left == right, name);
        if (!is_ok) {
            note() << " left: " << left << '\n';
            note() << "right: " << right << '\n';
        }
        return is_ok;
    }

    /// Run a subtest that has its own test plan.

    template <class F>
    void subtest(const char* name, size_t plan, F&& callback) {
        bool result_ok = false;
        {
            TAP subtest{ plan, level + 2 };
            subtest.note() << "subtest: " << name << std::endl;
            callback(subtest);
            result_ok = subtest.result_ok();
        }
        ok(result_ok, name);
    }
};
