#include "libmsp.h"
#include "command_q.h"

#include <stdio.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include "libavformat/avformat.h"
#include "libavutil/error.h"
#include "libavcodec/avcodec.h"

#define DEFERRED_CLEANUP(clean_func) __attribute__((cleanup(clean_func)))

#define LOG_INFO(who, fmt, ...) do { \
printf("[%s]: " fmt "\n", (who) __VA_OPT__(,) __VA_ARGS__); \
} while(0)

#define LOG_ERROR(who, fmt, ...) do { \
fprintf(stderr, "[%s]: " fmt "\n", (who) __VA_OPT__(,) __VA_ARGS__); \
} while(0)

#define PLAYBACK_THREAD_NAME "libmsp playback"
#define MAIN_THREAD_NAME "libmsp main"

static pthread_t playback_thread;
static msp_command_q_t *q;


typedef struct {
    char *filename;
    AVFormatContext *av_format_context;
    AVCodecContext *av_codec_context;
} playback_context_t;


// ReSharper disable once CppDeclaratorNeverUsed - uses via macro
static void msp_free_playback_context(playback_context_t **playback_context) {
    if (!playback_context || !*playback_context) return;
    const auto ctx_ptr = *playback_context;
    if (ctx_ptr->filename) free(ctx_ptr->filename);
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

// Playback thread function
bool open_new_file(playback_context_t *playback_context) {
    int ret = avformat_open_input(&playback_context->av_format_context, playback_context->filename, nullptr, nullptr);
    if (ret < 0) {
        msp_handle_ffmpeg_error("avformat_open_input", ret);
        LOG_ERROR(PLAYBACK_THREAD_NAME, "ffmpeg cannot open %s", playback_context->filename);
        return false;
    }

    ret = avformat_find_stream_info(playback_context->av_format_context, nullptr);
    if (ret < 0) {
        msp_handle_ffmpeg_error("avformat_find_stream_info", ret);
        LOG_ERROR(PLAYBACK_THREAD_NAME, "ffmpeg cannot find stream info in file %s", playback_context->filename);
        return false;
    }

    LOG_INFO(PLAYBACK_THREAD_NAME,
        "Container <%s> is found in %s", playback_context->av_format_context->iformat->name, playback_context->filename);
    return true;
}

void *playback_thread_func(void *arg) {
    DEFERRED_CLEANUP(msp_free_playback_context)
            // ReSharper disable once CppDFAMemoryLeak
            playback_context_t *playback_context = calloc(1, sizeof(*playback_context));
    if (!playback_context) {
        LOG_ERROR(PLAYBACK_THREAD_NAME, "Failed to create playback context");
        return nullptr;
    }

    // Playback main loop. Listens commands and executes them.
    while (true) {
        const msp_command_t command = msp_q_pop(q);

        if (command.type == MSP_EXIT) {
            LOG_INFO(PLAYBACK_THREAD_NAME, "Exit command received, shutting down");
            break;
        }

        switch (command.type) {
            case MSP_PLAY:
                playback_context->filename = strdup(command.payload.filename);
                free(command.payload.filename);
                LOG_INFO(PLAYBACK_THREAD_NAME, "New music file: %s", playback_context->filename);
                open_new_file(playback_context);
                break;
            case MSP_STOP:
                LOG_INFO(PLAYBACK_THREAD_NAME, "Stopping current playback");
                break;
            case MSP_TOGGLE_PAUSE:
                LOG_INFO(PLAYBACK_THREAD_NAME, "Toggling pause");
                break;
            case MSP_SET_VOLUME:
                LOG_INFO(PLAYBACK_THREAD_NAME, "Setting volume to %f", command.payload.volume);
                break;
            case MSP_SET_POSITION:
                LOG_INFO(PLAYBACK_THREAD_NAME, "Setting playback position to %i ms", command.payload.position_ms);
                break;
            default:
                LOG_ERROR(PLAYBACK_THREAD_NAME, "Unknown command type received: %i", command.type);
                break;
        }
    }

    return nullptr;
}

int msp_init() {
    if (pthread_create(&playback_thread, nullptr, playback_thread_func, nullptr) != 0) {
        return -1;
    }
    const int err = pthread_detach(playback_thread);
    if (err != 0) {
        LOG_ERROR(MAIN_THREAD_NAME, "pthread_detach() failed with error %s", strerror(err));
    }

    q = calloc(1, sizeof(*q));
    msp_q_init(q);

    return 0;
}

void msp_deinit(void) {
    const msp_command_t exit_cmd = {.type = MSP_EXIT};
    while (!msp_q_push(q, exit_cmd)) {
        // to be sure exit command passes. Shouldn't be happening much in the real life
    }
    pthread_join(playback_thread, nullptr);
    msp_q_destroy(q);
}

bool exec_command(const msp_command_t command) {
    if (!msp_q_push(q, command)) {
        LOG_ERROR(MAIN_THREAD_NAME, "%s command failed: the command q is full!", msp_command_to_str(command.type));
        return false;
    }
    LOG_INFO(MAIN_THREAD_NAME, "%s command sent to playback thread", msp_command_to_str(command.type));
    return true;
}

bool msp_play(const char *filename) {
    const msp_command_t command = {
        .type = MSP_PLAY,
        .payload.filename = strdup(filename)
    };

    const bool ret = exec_command(command);
    if (!ret) {
        free(command.payload.filename);
    }
    return ret;
}

bool msp_stop(void) {
    const msp_command_t command = {.type = MSP_STOP};
    return exec_command(command);
}

bool msp_set_volume(const float volume) {
    const msp_command_t command = {
        .type = MSP_SET_VOLUME,
        .payload.volume = volume
    };
    return exec_command(command);
}

bool msp_set_position(const int position_ms) {
    const msp_command_t command = {
        .type = MSP_SET_POSITION,
        .payload.position_ms = position_ms
    };
    return exec_command(command);
}

bool msp_toggle_pause(void) {
    const msp_command_t command = {.type = MSP_TOGGLE_PAUSE};
    return exec_command(command);
}
