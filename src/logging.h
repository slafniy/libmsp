#pragma once

#include <stdio.h>

#include "../ffmpeg-static/include/libavutil/error.h"
#include "../ffmpeg-static/include/libavutil/log.h"

#ifdef NDEBUG
#define LOG_DEBUG(who, fmt, ...) ((void)0)
#else
#define LOG_DEBUG(who, fmt, ...) do { \
printf("[DEBUG][%s]: " fmt "\n", (const char *)(who) __VA_OPT__(,) __VA_ARGS__); \
} while(0)
#endif

#define LOG_INFO(who, fmt, ...) do { \
printf("[INFO][%s]: " fmt "\n", (const char *)(who) __VA_OPT__(,) __VA_ARGS__); \
} while(0)

#define LOG_WARN(who, fmt, ...) do { \
fprintf(stderr, "[WARN][%s]: " fmt "\n", (const char *)(who) __VA_OPT__(,) __VA_ARGS__); \
} while(0)

#define LOG_ERROR(who, fmt, ...) do { \
fprintf(stderr, "[ERROR][%s]: " fmt "\n", (const char *)(who) __VA_OPT__(,) __VA_ARGS__); \
} while(0)

// ffmpeg logging level
#ifdef NDEBUG
static constexpr int FFMPEG_LOG_LEVEL = AV_LOG_ERROR;
#else
static constexpr int FFMPEG_LOG_LEVEL = AV_LOG_DEBUG;
#endif


static void handle_ffmpeg_error(const char *what, const int err) {
    char buf[AV_ERROR_MAX_STRING_SIZE];
    av_strerror(err, buf, sizeof(buf));
    // TODO: figure out why system errors do not show
    fprintf(stderr, "libmsp: %s error: %s\n", what, buf);
}
