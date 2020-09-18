#pragma once

#include "../common/common.h"
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>


// normalize the *_CHECK feature flags
static constexpr bool use_ptr_check =
#ifdef PTR_CHECK
    true
#else
    false
#endif
    ;

static constexpr bool use_usb_check =
#ifdef USB_CHECK
    true
#else
    false
#endif
    ;

static constexpr bool use_top_check =
#ifdef TOP_CHECK
    true
#else
    false
#endif
    ;

static constexpr bool use_tca_check =
#ifdef TCA_CHECK
    true
#else
    false
#endif
    ;

static constexpr bool use_leak_check =
#ifdef LEAK_CHECK
    true
#else
    false
#endif
    ;

/// Read an environment variable, possibly returning some error.
/// If the variable doesn't exist, the out-parameter remains unmodified.
template <class T>
inline const char* getenv_parsed(const char* name, T& value);

template <>
inline const char* getenv_parsed<bool>(const char* name, bool& value) {
    auto* str = getenv(name);

    // ignore empty values
    if (!str || !*str) return nullptr;

    if (0 == std::strcmp(str, "1")) {
        value = true;
        return nullptr;
    }

    if (0 == std::strcmp(str, "0")) {
        value = false;
        return nullptr;
    }

    return "value must be '1' or '0'";
}

template <>
inline const char* getenv_parsed<unsigned long>(const char* name, unsigned long& value) {
    auto str = getenv(name);

    // ignore empty values
    if (!str || !*str) return nullptr;

    char* end;
    auto old_errno = errno;
    errno = 0;
    auto parsed = std::strtoul(str, &end, 0);

    if (errno) {
        return strerror(errno);
    }

    errno = old_errno;

    if (*end) {
        return "contains non-numeric chars";
    }

    value = parsed;
    return nullptr;
}

class ModeReader {
    bool isInitialized = false;

public:
    bool ptrMode = use_ptr_check;
    bool usbMode = use_usb_check;
    bool topMode = use_top_check;
    bool leakMode = use_leak_check;
    bool tcaMode = use_tca_check;

    size_t initialStoreSize = 0;

    ModeReader() {
    }

    ~ModeReader() {
    }

    void ensure_initialized() {
        // only perform initialization once
        if (this->isInitialized) return;


        auto disable_via_env = [](auto name) {
            auto disable = false;
            auto problem = getenv_parsed(name, disable);
            if (problem) {
                warn("ShadowHeap: ERROR: variable %s: %s", name, problem);
                std::exit(1);
            }
            return disable;
        };

        if (ptrMode && disable_via_env("SHADOWHEAP_DISABLE_PTRCHECKS"))
            ptrMode = false;

        if (usbMode && disable_via_env("SHADOWHEAP_DISABLE_USBCHECKS"))
            usbMode = false;

        if (topMode && disable_via_env("SHADOWHEAP_DISABLE_TOPCHECKS"))
            topMode = false;

        if (leakMode && disable_via_env("SHADOWHEAP_DISABLE_LEAKCHECKS"))
            leakMode = false;

        if (tcaMode && disable_via_env("SHADOWHEAP_DISABLE_TCACHECKS"))
            tcaMode = false;

        if (auto problem = getenv_parsed("SHADOWHEAP_SIZE_INITIAL", this->initialStoreSize)) {
            warn("ShadowHeap: ERROR: variable SHADOWHEAP_SIZE_INITIAL: %s", problem);
            std::exit(1);
        }

        auto consume = [](const char* p, const char* prefix) -> const char* {
            while (*p && *prefix) {
                if (*p == *prefix) {
                    ++p;
                    ++prefix;
                } else {
                    // failed to consume next character
                    return nullptr;
                }
            }

            // return pointer after prefix
            return p;
        };

        auto is_allowed_env_var = [consume](const char* kv) -> bool {
            if (auto s = consume(kv, "SHADOWHEAP_")) {
                // restrict allowed variables that start with shadowheap
                return consume(s, "DISABLE_PTRCHECKS=") ||
                       consume(s, "DISABLE_USBCHECKS=") ||
                       consume(s, "DISABLE_TOPCHECKS=") ||
                       consume(s, "DISABLE_TCACHECKS=") ||
                       consume(s, "DISABLE_LEAKCHECKS=") || consume(s, "SIZE_INITIAL=");
            } else {
                // other variables are allowed
                return true;
            }
        };

        // since we only use POSIX platforms, we can check for illegal environment variables:
        // any variable other than the above that starts with "SHADOWHEAP"
        extern char** environ;
        for (char const* const* envp = environ; *envp; ++envp) {
            if (!is_allowed_env_var(*envp)) {
                warn("ShadowHeap: ERROR: unrecognized environment variable: %s", *envp);
                std::exit(1);
            }
        }

        this->isInitialized = true;
    }
};
