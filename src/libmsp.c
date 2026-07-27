#include "libmsp.h"
#include "command_q.h"

#include <stdio.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "libavformat/avformat.h"
#include "libavutil/error.h"
#include "libavcodec/avcodec.h"
#include "SDL2/SDL.h"

#define DEFERRED_CLEANUP(clean_func) __attribute__((cleanup(clean_func)))

#define LOG_INFO(who, fmt, ...) do { \
printf("[%s]: " fmt "\n", (who) __VA_OPT__(,) __VA_ARGS__); \
} while(0)

#define LOG_ERROR(who, fmt, ...) do { \
fprintf(stderr, "[%s]: " fmt "\n", (who) __VA_OPT__(,) __VA_ARGS__); \
} while(0)

#define PLAYBACK_THREAD_NAME "libmsp playback"
#define MAIN_THREAD_NAME "libmsp main"


typedef struct {
    // libmsp specific data
    char *filename; // path to file we want to play
    msp_status_t status; // playback status, used to control playback thread

    // ffmpeg data - open file, find stream, decode etc
    AVFormatContext *av_format_context;
    AVCodecContext *av_codec_context;
} playback_context_t;

//======================================================================================================================
// Global variables
//======================================================================================================================
static pthread_t msp_playback_thread; // handles commands and uses ffmpeg libs to open and play files
static msp_command_q_t *msp_command_q; // queue for commands for playback thread
static SDL_AudioDeviceID msp_sdl_device_id;

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

const char *msp_sdl_format_to_str(const SDL_AudioFormat format) {
    switch (format) {
        case AUDIO_S8: return "S8 (8-bit signed)";
        case AUDIO_U8: return "U8 (8-bit unsigned)";
        case AUDIO_S16LSB: return "S16LE (16-bit signed Little-Endian)";
        case AUDIO_S16MSB: return "S16BE (16-bit signed Big-Endian)";
        case AUDIO_U16LSB: return "U16LE (16-bit unsigned Little-Endian)";
        case AUDIO_U16MSB: return "U16BE (16-bit unsigned Big-Endian)";
        case AUDIO_S32LSB: return "S32LE (32-bit signed Little-Endian)";
        case AUDIO_S32MSB: return "S32BE (32-bit signed Big-Endian)";
        case AUDIO_F32LSB: return "F32LE (32-bit float Little-Endian)";
        case AUDIO_F32MSB: return "F32BE (32-bit float Big-Endian)";
        default: return "UNKNOWN";
    }
}

bool msp_init_sdl2() {
    if (SDL_Init(SDL_INIT_AUDIO) < 0) {
        LOG_ERROR(MAIN_THREAD_NAME, "Cannot init SDL: %s", SDL_GetError());
        return false;
    }
    const SDL_AudioSpec wanted_spec = {
        .freq = 96000,
        .format = AUDIO_F32,
        .channels = 2,
        .samples = 2048,
        .callback = nullptr
    };
    SDL_AudioSpec obtained_spec;
    msp_sdl_device_id = SDL_OpenAudioDevice(nullptr, 0, &wanted_spec, &obtained_spec, 0);
    if (msp_sdl_device_id == 0) {
        LOG_ERROR(MAIN_THREAD_NAME, "Failed to open audio device: %s", SDL_GetError());
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        return false;
    }
    LOG_INFO(MAIN_THREAD_NAME, "SDL device initialized! Frequency: %i, Format: %s", obtained_spec.freq,
             msp_sdl_format_to_str(obtained_spec.format));

    SDL_PauseAudioDevice(msp_sdl_device_id, 0); // unpause because it's paused by default
    return true;
}

void msp_deinit_sdl2() {
    SDL_PauseAudioDevice(msp_sdl_device_id, 1);
    SDL_ClearQueuedAudio(msp_sdl_device_id);

    if (msp_sdl_device_id != 0) {
        SDL_CloseAudioDevice(msp_sdl_device_id);
        msp_sdl_device_id = 0;
    }

    SDL_QuitSubSystem(SDL_INIT_AUDIO);
}

// Playback thread function
bool msp_open_new_file(playback_context_t *playback_context) {
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
             "Container <%s> is found in %s", playback_context->av_format_context->iformat->name,
             playback_context->filename);
    return true;
}

// Playback thread function. Determines what thread should do in the next iteration and sets playback_context->status
void msp_handle_command(playback_context_t *playback_context, const msp_command_t *command) {
    switch (command->type) {
        case MSP_PLAY:
            playback_context->filename = strdup(command->payload.filename);
            free(command->payload.filename);
            LOG_INFO(PLAYBACK_THREAD_NAME, "New music file: %s", playback_context->filename);
            if (!msp_open_new_file(playback_context)) {
                LOG_ERROR(PLAYBACK_THREAD_NAME, "Cannot play %s", playback_context->filename);
            }
            playback_context->status = MSP_STATUS_PLAYING;
            break;
        case MSP_STOP:
            playback_context->status = MSP_STATUS_IDLE;
            LOG_INFO(PLAYBACK_THREAD_NAME, "Stopping current playback");
            break;
        case MSP_TOGGLE_PAUSE:
            if (playback_context->status == MSP_STATUS_PLAYING) {
                playback_context->status = MSP_STATUS_PAUSED;
            } else if (playback_context->status == MSP_STATUS_PAUSED) {
                playback_context->status = MSP_STATUS_PLAYING;
            }
            LOG_INFO(PLAYBACK_THREAD_NAME, "Toggling pause");
            break;
        case MSP_SET_VOLUME:
            LOG_INFO(PLAYBACK_THREAD_NAME, "Setting volume to %f", command->payload.volume);
            break;
        case MSP_SET_POSITION:
            LOG_INFO(PLAYBACK_THREAD_NAME, "Setting playback position to %i ms", command->payload.position_ms);
            break;
        default:
            LOG_ERROR(PLAYBACK_THREAD_NAME, "Unknown command type received: %i", command->type);
            break;
    }
}

void msp_decode_next_frame(playback_context_t *playback_context) {
    // TODO: implement
    printf("Sleeping!\n");
    sleep(1);
}

void *msp_playback_thread_func(void *arg) {
    DEFERRED_CLEANUP(msp_free_playback_context)
            // ReSharper disable once CppDFAMemoryLeak
            playback_context_t *playback_context = calloc(1, sizeof(*playback_context));
    if (!playback_context) {
        LOG_ERROR(PLAYBACK_THREAD_NAME, "Failed to create playback context");
        return nullptr;
    }

    playback_context->status = MSP_STATUS_IDLE;

    // Playback main loop. Listens commands and executes them, and decodes frames if needed
    while (true) {
        msp_command_t command;
        bool has_command = false;

        // If playing - quickly check if we got a new command and not pause decoding
        if (playback_context->status == MSP_STATUS_PLAYING) {
            has_command = msp_q_try_pop(msp_command_q, &command);
        } else {
            // if not playing - just wait for a command
            msp_q_pop(msp_command_q, &command);
            has_command = true;
        }

        // Exit requested case - breaks playback loop, which causes resource freeing
        if (has_command && command.type == MSP_EXIT) {
            LOG_INFO(PLAYBACK_THREAD_NAME, "Exit command received, shutting down");
            break;
        }

        // Handle other commands
        if (has_command) {
            msp_handle_command(playback_context, &command);
        }

        // Decode and send next frame to audio device, if needed.
        if (playback_context->status == MSP_STATUS_PLAYING) {
            msp_decode_next_frame(playback_context);
        }
    }

    // ReSharper disable once CppDFAMemoryLeak - DEFERRED_CLEANUP macro should do the job
    return nullptr;
}

bool exec_command(const msp_command_t command) {
    if (!msp_q_push(msp_command_q, command)) {
        LOG_ERROR(MAIN_THREAD_NAME, "%s command failed: the command q is full!", msp_command_to_str(command.type));
        return false;
    }
    LOG_INFO(MAIN_THREAD_NAME, "%s command sent to playback thread", msp_command_to_str(command.type));
    return true;
}

// =====================================================================================================================
// Public interface functions implementation
// =====================================================================================================================

int msp_init() {
    if (pthread_create(&msp_playback_thread, nullptr, msp_playback_thread_func, nullptr) != 0) {
        return -1;
    }
    const int err = pthread_detach(msp_playback_thread);
    if (err != 0) {
        LOG_ERROR(MAIN_THREAD_NAME, "pthread_detach() failed with error %s", strerror(err));
    }

    msp_command_q = calloc(1, sizeof(*msp_command_q));
    msp_q_init(msp_command_q);

    if (!msp_init_sdl2()) {
        return -1;
    }

    return 0;
}

void msp_deinit(void) {
    const msp_command_t exit_cmd = {.type = MSP_EXIT};
    while (!msp_q_push(msp_command_q, exit_cmd)) {
        // to be sure exit command passes. Shouldn't be happening much in the real life
    }
    pthread_join(msp_playback_thread, nullptr);
    msp_q_destroy(msp_command_q);

    msp_deinit_sdl2();
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
