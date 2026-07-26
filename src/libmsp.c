#include "libmsp.h"
#include "command_q.h"

#include <stdio.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/eventfd.h>

#include "libavformat/avformat.h"
#include "libavutil/error.h"
#include "libavcodec/avcodec.h"

#define DEFERRED_CLEANUP(clean_func) __attribute__((cleanup(clean_func)))

static pthread_t playback_thread;
static msp_q_t *q;
static int q_event;


static void msp_free_msp_command(msp_command_t **msp_command) {
    if (!msp_command || !*msp_command) return;
    if ((*msp_command)->payload.filename) {
        free((*msp_command)->payload.filename);
    }
    *msp_command = nullptr;
}

typedef struct {
    AVFormatContext *av_format_context;
    AVCodecContext *av_codec_context;
} playback_context_t;


// ReSharper disable once CppDeclaratorNeverUsed - uses via macro
static void msp_free_playback_context(playback_context_t **playback_context) {
    if (!playback_context || !*playback_context) return;
    const auto ctx_ptr = *playback_context;
    if (ctx_ptr->av_format_context) avformat_close_input(&ctx_ptr->av_format_context); // also sets it to NULL
    if (ctx_ptr->av_codec_context) avcodec_free_context(&ctx_ptr->av_codec_context); // also sets it to NULL
    free(ctx_ptr);
    *playback_context = nullptr;
}

static void msp_handle_ffmpeg_error(const char *what, int err) {
    char buf[AV_ERROR_MAX_STRING_SIZE];
    av_strerror(err, buf, sizeof(buf));
    // TODO: figure out why system errors do not show
    fprintf(stderr, "libmsp: %s error: %s\n", what, buf);
}

void *playback_thread_func(void *arg) {
    // DEFERRED_CLEANUP(msp_free_playback_context)
    //         // ReSharper disable once CppDFAMemoryLeak
    //         playback_context_t *playback_context = calloc(1, sizeof(*playback_context));
    // if (!playback_context) return nullptr;

    // Playback main loop. Listens commands and executes them.
    while (1) {

    }


    // printf("Playing: %s\n", playback_args->filename);

    // int ret = avformat_open_input(&playback_context->av_format_context, playback_args->filename, nullptr, nullptr);
    // if (ret < 0) {
    //     msp_handle_ffmpeg_error("avformat_open_input", ret);
    //     // ReSharper disable once CppDFAMemoryLeak
    //     return nullptr;
    // }
    //
    // ret = avformat_find_stream_info(playback_context->av_format_context, nullptr);
    // if (ret < 0) {
    //     msp_handle_ffmpeg_error("avformat_find_stream_info", ret);
    //     // ReSharper disable once CppDFAMemoryLeak
    //     return nullptr;
    // }
    //
    // printf("Container: %s\n", playback_context->av_format_context->iformat->name);
    // // ReSharper disable once CppDFAMemoryLeak
    return nullptr;
}

int msp_init() {
    if (pthread_create(&playback_thread, nullptr, playback_thread_func, nullptr) != 0) {
        // ReSharper disable once CppDFAMemoryLeak
        return -1;
    }
    const int err = pthread_detach(playback_thread);
    if (err != 0) {
        fprintf(stderr, "libmsp: msp_play: pthread_detach() failed with error %s\n", strerror(err));
    }

    q = calloc(1, sizeof(*q));
    msp_q_init(q);

    q_event = eventfd(0, EFD_CLOEXEC);
    if (q_event < 0) {
        perror("eventfd");
    }

    // ReSharper disable once CppDFAMemoryLeak
    return 0;
}

void msp_deinit(void) {
    msp_q_destroy(q);
}

int msp_play(const char *filename) {
    const msp_command_t command = {
        .type = MSP_PLAY,
        .payload.filename = strdup(filename)
    };

    if (msp_q_push(q, command)) {
        constexpr uint64_t one = 1;
        if (write(q_event, &one, sizeof(one)) != sizeof(one)) {
            perror("q_event write");
        }
    }

    return 0;
}
