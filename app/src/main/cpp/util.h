//
// Created by Ihor Ilkevych on 8/1/25.
//
#ifndef UTIL_H
#define UTIL_H

#include <android/log.h>
#include <android/set_abort_message.h>
#include <cstdlib>

#define _LOG(priority, tag, fmt, ...) \
  ((void)__android_log_print((priority), (tag), (fmt)__VA_OPT__(, ) __VA_ARGS__))

#define LOGE(tag, fmt, ...) _LOG(ANDROID_LOG_ERROR, tag, (fmt)__VA_OPT__(, ) __VA_ARGS__)
#define LOGW(tag, fmt, ...) _LOG(ANDROID_LOG_WARN, tag, (fmt)__VA_OPT__(, ) __VA_ARGS__)
#define LOGI(tag, fmt, ...) _LOG(ANDROID_LOG_INFO, tag, (fmt)__VA_OPT__(, ) __VA_ARGS__)

[[noreturn]] __attribute__((__format__(__printf__, 2, 3))) static void fatal(
        const char* tag, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    char* buf;
    if (vasprintf(&buf, fmt, ap) < 0) {
        android_set_abort_message("failed for format error message");
    } else {
        android_set_abort_message(buf);
        // Also log directly, since the default Android Studio logcat filter hides
        // the backtrace which would otherwise show the abort message.
        LOGE(tag, "%s", buf);
    }
    std::abort();
}

#define CHECK_NOT_NULL(tag, value)                                           \
  do {                                                                  \
    if ((value) == nullptr) {                                           \
      fatal(tag, "%s:%d:%s must not be null", __PRETTY_FUNCTION__, __LINE__, \
            #value);                                                    \
    }                                                                   \
  } while (false)

#endif // UTIL_H