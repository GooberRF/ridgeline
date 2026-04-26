#pragma once

#ifndef RC_INVOKED
#include <functional>
#include <format>
#include <string>
#include <cstdio>
#include <string_view>
#endif

#ifndef TOSTRING
#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)
#endif

// clang-format off

#define VERSION_TYPE_DEV      0
#define VERSION_TYPE_ALPHA    1
#define VERSION_TYPE_BETA     2
#define VERSION_TYPE_RC       3
#define VERSION_TYPE_RELEASE  4

// Variables to be modified during the release process
#define PRODUCT_NAME           "Ridgeline"
#define VERSION_MAJOR          0
#define VERSION_MINOR          1
#define VERSION_PATCH          0
#define VERSION_TYPE           VERSION_TYPE_DEV
#define VERSION_TYPE_REVISION  0
#define VERSION_CODE           "TBD"

// clang-format on

#if VERSION_TYPE == VERSION_TYPE_DEV
#define VERSION_SUFFIX        "-dev"
#elif VERSION_TYPE == VERSION_TYPE_ALPHA
#define VERSION_SUFFIX        "-alpha" TOSTRING(VERSION_TYPE_REVISION)
#elif VERSION_TYPE == VERSION_TYPE_BETA
#define VERSION_SUFFIX        "-beta" TOSTRING(VERSION_TYPE_REVISION)
#elif VERSION_TYPE == VERSION_TYPE_RC
#define VERSION_SUFFIX        "-rc" TOSTRING(VERSION_TYPE_REVISION)
#elif VERSION_TYPE == VERSION_TYPE_RELEASE
#define VERSION_SUFFIX        ""
#else
#error Unknown version type
#endif

#define VERSION_STR TOSTRING(VERSION_MAJOR) "." TOSTRING(VERSION_MINOR) "." TOSTRING(VERSION_PATCH) VERSION_SUFFIX
#define PRODUCT_NAME_VERSION PRODUCT_NAME " " VERSION_STR

#ifndef RC_INVOKED
inline const std::string& get_build_date() {
    static std::string res = std::invoke([] {
        std::string date = __DATE__;
        // Find and erase double space.
        const size_t pos = date.find("  ");
        if (pos != std::string::npos) {
            date.erase(pos, 1);
        }
        return date;
    });
    return res;
}

inline const std::string& get_build_time() {
    static std::string res = std::invoke([] {
        int hours = 0, minutes = 0, seconds = 0;
        std::sscanf(__TIME__, "%d:%d:%d", &hours, &minutes, &seconds);
        const std::string_view suffix = (hours < 12) ? "AM" : "PM";
        // Use a zero-indexed 12-hour clock for elegance.
        return std::format("{}:{:02} {}", hours % 12, minutes, suffix);
    });
    return res;
}
#endif
