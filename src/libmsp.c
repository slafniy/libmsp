#include "libmsp.h"

#include <stdio.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include "libavformat/avformat.h"
#include "libavutil/error.h"
#include "libavcodec/avcodec.h"

#define CLEAN(clean_func) __attribute__((cleanup(clean_func)))

static pthread_t playback_thread;

typedef struct {
    char *filename;
} playback_args_t;

// ReSharper disable once CppDeclaratorNeverUsed - uses via macro
static void msp_free_playback_args(playback_args_t **playback_args) {
    free((*playback_args)->filename);
    (*playback_args)->filename = nullptr;
    free(*playback_args);
    *playback_args = nullptr;
}

typedef struct {
    AVFormatContext *av_format_context;
    AVCodecContext *av_codec_context;
} playback_context_t;

static void msp_free_playback_context(playback_context_t **playback_context) {
    if (!playback_context || !*playback_context) return;
    const auto ctx_ptr = *playback_context;
    if (ctx_ptr->av_format_context) avformat_close_input(&ctx_ptr->av_format_context); // also sets it to NULL
    if (ctx_ptr->av_codec_context) avcodec_free_context(&ctx_ptr->av_codec_context); // also sets it to NULL
    free(ctx_ptr);
    *playback_context = nullptr;
}

void msp_handle_ffmpeg_error(const char *what, int err) {
    char buf[AV_ERROR_MAX_STRING_SIZE];
    av_strerror(err, buf, sizeof(buf));
    // TODO: figure out why system errors do not show
    fprintf(stderr, "libmsp: %s error: %s\n", what, buf);
}

void *playback_thread_func(void *arg) {
    CLEAN(msp_free_playback_args) playback_args_t *playback_args = arg;

    // ReSharper disable once CppDFAMemoryLeak
    CLEAN(msp_free_playback_context) playback_context_t *playback_context = malloc(sizeof(playback_context_t));

    printf("Playing: %s\n", playback_args->filename);

    int ret = avformat_open_input(&playback_context->av_format_context, playback_args->filename, nullptr, nullptr);
    if (ret < 0) {
        msp_handle_ffmpeg_error("avformat_open_input", ret);
        return nullptr;
    }

    ret = avformat_find_stream_info(playback_context->av_format_context, nullptr);
    if (ret < 0) {
        msp_handle_ffmpeg_error("avformat_find_stream_info", ret);
        return nullptr;
    }

    printf("Container: %s\n", playback_context->av_format_context->iformat->name);

    return nullptr;
}

int msp_play(const char *filename) {
    // ReSharper disable once CppDFAMemoryLeak
    // The thread we create will own and free this memory
    playback_args_t *args = malloc(sizeof(*args));
    if (!args) {
        return -1;
    }

    args->filename = strdup(filename);
    if (!args->filename) {
        // ReSharper disable once CppDFAMemoryLeak
        return -1;
    }

    if (pthread_create(&playback_thread, nullptr, playback_thread_func, args) != 0) {
        // ReSharper disable once CppDFAMemoryLeak
        return -1;
    }

    // ReSharper disable once CppDFAMemoryLeak
    return 0;
}
