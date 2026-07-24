#include "libmsp.h"

#include <stdio.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include "libavformat/avformat.h"
#include "libavutil/error.h"

static pthread_t playback_thread;

typedef struct {
    char *filename;
} playback_args_t;

void msp_handle_ffmpeg_error(const char *what, int err) {
    char buf[AV_ERROR_MAX_STRING_SIZE];
    av_strerror(err, buf, sizeof(buf));
    // TODO: figure out why system errors do not show
    fprintf(stderr, "libmsp: %s error: %s\n", what, buf);
}

void *playback_thread_func(void *arg) {
    playback_args_t *args = arg;
    printf("Playing: %s\n", args->filename);

    AVFormatContext *fmt_ctx = nullptr;
    int ret;

    ret = avformat_open_input(&fmt_ctx, args->filename, nullptr, nullptr);
    if (ret < 0) {
        msp_handle_ffmpeg_error("avformat_open_input", ret);
        goto error;
    }

    ret = avformat_find_stream_info(fmt_ctx, nullptr);
    if (ret < 0) {
        msp_handle_ffmpeg_error("avformat_find_stream_info", ret);
        goto error;
    }

    printf("Container: %s\n", fmt_ctx->iformat->name);

error:
    avformat_close_input(&fmt_ctx);
    free(args->filename);
    free(args);

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
        goto error;
    }

    if (pthread_create(&playback_thread, nullptr, playback_thread_func, args) != 0) {
        goto error;
    }

    // ReSharper disable once CppDFAMemoryLeak
    return 0;

error:
    free(args->filename);
    free(args);
    return -1;
}
