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
#include "libswresample/swresample.h"
#include "SDL3/SDL.h"

#define DEFERRED_CLEANUP(clean_func) __attribute__((cleanup(clean_func)))

#define LOG_INFO(who, fmt, ...) do { \
printf("[%s]: " fmt "\n", (who) __VA_OPT__(,) __VA_ARGS__); \
} while(0)

#define LOG_ERROR(who, fmt, ...) do { \
fprintf(stderr, "[%s]: " fmt "\n", (who) __VA_OPT__(,) __VA_ARGS__); \
} while(0)

#define PLAYBACK_THREAD_NAME "libmsp playback"
#define MAIN_THREAD_NAME "libmsp main"

#define SAMPLE_RATE 96000
#define SDL_FORMAT SDL_AUDIO_F32
#define PLAYBACK_BUFFER_SIZE_SEC 0.5f
// How long is allowed to wait if playback buffer is already full
#define SDL_DELAY_MS 5

const SDL_AudioSpec msp_sdl_wanted_spec = {
    .freq = SAMPLE_RATE,
    .format = SDL_FORMAT,
    .channels = 2
};

// should match msp_sdl_wanted_spec!
const AVChannelLayout msp_av_channel_layout = AV_CHANNEL_LAYOUT_STEREO;
constexpr auto msp_av_sample_format = AV_SAMPLE_FMT_FLT; // this is for float in sdl2

typedef struct {
    // libmsp specific data
    char *filename; // path to file we want to play
    msp_status_t status; // playback status, used to control playback thread

    // ffmpeg data - open file, find stream, decode etc
    int audio_stream_index;
    AVFormatContext *av_format_context;
    AVCodecContext *av_codec_context;
    SwrContext *swr_context;
    AVPacket *av_packet;
    AVFrame *av_frame;
} playback_context_t;

//======================================================================================================================
// Global variables
//======================================================================================================================
static pthread_t msp_playback_thread; // handles commands and uses ffmpeg libs to open and play files
static msp_command_q_t *msp_command_q = nullptr; // queue for commands for playback thread
static SDL_AudioStream *msp_sdl_stream = nullptr;
static uint32_t msp_sdl_queue_size_bytes = 0; // to determine SDL max queue size depending on audio parameters

// ReSharper disable once CppDeclaratorNeverUsed - it is used via macro.
// Final de-initialization before application exit.
static void msp_free_playback_context(playback_context_t **playback_context) {
    if (!playback_context || !*playback_context) return;
    const auto ctx_ptr = *playback_context;
    if (ctx_ptr->filename) free(ctx_ptr->filename);
    if (ctx_ptr->av_format_context) avformat_close_input(&ctx_ptr->av_format_context); // also sets it to NULL
    if (ctx_ptr->av_codec_context) avcodec_free_context(&ctx_ptr->av_codec_context); // also sets it to NULL
    if (ctx_ptr->swr_context) swr_free(&ctx_ptr->swr_context);
    if (ctx_ptr->av_packet) av_packet_free(&ctx_ptr->av_packet);
    if (ctx_ptr->av_frame) av_frame_free(&ctx_ptr->av_frame);
    free(ctx_ptr);
    *playback_context = nullptr;
}

static void msp_handle_ffmpeg_error(const char *what, const int err) {
    char buf[AV_ERROR_MAX_STRING_SIZE];
    av_strerror(err, buf, sizeof(buf));
    // TODO: figure out why system errors do not show
    fprintf(stderr, "libmsp: %s error: %s\n", what, buf);
}

bool msp_init_sdl3() {
    if (!SDL_Init(SDL_INIT_AUDIO)) {
        LOG_ERROR(MAIN_THREAD_NAME, "Cannot init SDL: %s", SDL_GetError());
        return false;
    }

    msp_sdl_stream = SDL_OpenAudioDeviceStream(
        SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,
        &msp_sdl_wanted_spec,
        nullptr,
        nullptr);

    if (!msp_sdl_stream) {
        LOG_ERROR(MAIN_THREAD_NAME, "Failed to open audio device stream: %s", SDL_GetError());
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        return false;
    }

    // Calculate audio device max allowed queue size
    const uint32_t bytes_per_sample = SDL_AUDIO_BYTESIZE(msp_sdl_wanted_spec.format);
    const uint32_t bytes_per_frame = msp_sdl_wanted_spec.channels * bytes_per_sample;
    msp_sdl_queue_size_bytes = (uint32_t) (
        PLAYBACK_BUFFER_SIZE_SEC * (float) msp_sdl_wanted_spec.freq * (float) bytes_per_frame);

    LOG_INFO(MAIN_THREAD_NAME, "SDL device initialized! Frequency: %i, Format: %s", msp_sdl_wanted_spec.freq,
             SDL_GetAudioFormatName(msp_sdl_wanted_spec.format));

    SDL_ResumeAudioStreamDevice(msp_sdl_stream); // unpause because it's paused by default
    return true;
}

void msp_deinit_sdl3() {
    if (msp_sdl_stream) {
        SDL_PauseAudioStreamDevice(msp_sdl_stream);
        SDL_ClearAudioStream(msp_sdl_stream);
        SDL_DestroyAudioStream(msp_sdl_stream);
        msp_sdl_stream = nullptr;
    }
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
}

// Playback thread function. Opens file, looks for audio stream, codec etc.
bool msp_open_new_file(playback_context_t *ctx) {
    // Trying to open audio file
    int ret = avformat_open_input(&ctx->av_format_context, ctx->filename, nullptr, nullptr);
    if (ret < 0) {
        msp_handle_ffmpeg_error("avformat_open_input", ret);
        LOG_ERROR(PLAYBACK_THREAD_NAME, "ffmpeg cannot open %s", ctx->filename);
        return false;
    }

    // Looking for container format
    ret = avformat_find_stream_info(ctx->av_format_context, nullptr);
    if (ret < 0) {
        msp_handle_ffmpeg_error("avformat_find_stream_info", ret);
        LOG_ERROR(PLAYBACK_THREAD_NAME, "ffmpeg cannot find stream info in file %s", ctx->filename);
        return false;
    }

    LOG_INFO(PLAYBACK_THREAD_NAME, "Container <%s> is found in %s", ctx->av_format_context->iformat->name,
             ctx->filename);

    // Looking for audio stream and decoder
    const AVCodec *codec = nullptr;
    const int stream_idx = av_find_best_stream(
        ctx->av_format_context,
        AVMEDIA_TYPE_AUDIO,
        -1,
        -1,
        &codec,
        0
    );

    if (stream_idx < 0) {
        msp_handle_ffmpeg_error("av_find_best_stream", stream_idx);
        LOG_ERROR(PLAYBACK_THREAD_NAME, "Cannot find audio stream or decoder for %s", ctx->filename);
        return false;
    }

    ctx->audio_stream_index = stream_idx;
    const AVCodecParameters *codec_parameters = ctx->av_format_context->streams[stream_idx]->codecpar;
    LOG_INFO(PLAYBACK_THREAD_NAME, "Found audio stream #%d, codec: %s", stream_idx, codec->name);

    ctx->av_codec_context = avcodec_alloc_context3(codec);
    if (!ctx->av_codec_context) {
        LOG_ERROR(PLAYBACK_THREAD_NAME, "Failed to allocate codec context");
        return false;
    }

    if (avcodec_parameters_to_context(ctx->av_codec_context, codec_parameters) < 0) {
        LOG_ERROR(PLAYBACK_THREAD_NAME, "Failed to copy codec params");
        return false;
    }

    if (avcodec_open2(ctx->av_codec_context, codec, nullptr) < 0) {
        LOG_ERROR(PLAYBACK_THREAD_NAME, "Failed to open codec");
        return false;
    }

    // Prepare resampler
    ret = swr_alloc_set_opts2(
        &ctx->swr_context,
        &msp_av_channel_layout,
        msp_av_sample_format,
        SAMPLE_RATE,
        &ctx->av_codec_context->ch_layout,
        ctx->av_codec_context->sample_fmt,
        ctx->av_codec_context->sample_rate,
        0, nullptr
    );
    if (ret < 0 || swr_init(ctx->swr_context) < 0) {
        LOG_ERROR(PLAYBACK_THREAD_NAME, "Failed to initialize resampler");
        return false;
    }

    // Allocate packet and frame for future decoding
    ctx->av_packet = av_packet_alloc();
    ctx->av_frame = av_frame_alloc();

    LOG_INFO(PLAYBACK_THREAD_NAME, "Successfully loaded container <%s> with codec [%s]",
             ctx->av_format_context->iformat->name, codec->name);

    return true;
}

// Playback thread function. Clears context, preparing it for the new file
void msp_clear_playback_context(playback_context_t *ctx) {
    if (ctx->filename) {
        free(ctx->filename);
        ctx->filename = nullptr;
    }
    ctx->audio_stream_index = -1;
    // Close per-file-unique contexts
    if (ctx->av_format_context) avformat_close_input(&ctx->av_format_context);
    if (ctx->av_codec_context) avcodec_free_context(&ctx->av_codec_context);
    if (ctx->swr_context) swr_free(&ctx->swr_context);
    // Do not free packet and frame, just unref, they could be reused without reallocation
    if (ctx->av_packet) av_packet_unref(ctx->av_packet);
    if (ctx->av_frame) av_frame_unref(ctx->av_frame);
}

// Playback thread function. Determines what thread should do in the next iteration and sets playback_context->status
void msp_handle_command(playback_context_t *playback_context, const msp_command_t *command) {
    switch (command->type) {
        case MSP_PLAY:
            // clear audio stream
            SDL_PauseAudioStreamDevice(msp_sdl_stream);
            SDL_ClearAudioStream(msp_sdl_stream);
            // clear existing data from context
            msp_clear_playback_context(playback_context);

            // open new file
            playback_context->filename = strdup(command->payload.filename);
            free(command->payload.filename);
            LOG_INFO(PLAYBACK_THREAD_NAME, "New music file: %s", playback_context->filename);
            if (!msp_open_new_file(playback_context)) {
                LOG_ERROR(PLAYBACK_THREAD_NAME, "Cannot play %s", playback_context->filename);
            }
            playback_context->status = MSP_STATUS_PLAYING;
            SDL_ResumeAudioStreamDevice(msp_sdl_stream);
            break;
        case MSP_STOP:
            playback_context->status = MSP_STATUS_IDLE;
            SDL_ClearAudioStream(msp_sdl_stream);
            LOG_INFO(PLAYBACK_THREAD_NAME, "Stopping current playback");
            break;
        case MSP_TOGGLE_PAUSE:
            if (playback_context->status == MSP_STATUS_PLAYING) {
                playback_context->status = MSP_STATUS_PAUSED;
                SDL_PauseAudioStreamDevice(msp_sdl_stream);
                LOG_INFO(PLAYBACK_THREAD_NAME, "Pausing");
            } else if (playback_context->status == MSP_STATUS_PAUSED) {
                playback_context->status = MSP_STATUS_PLAYING;
                SDL_ResumeAudioStreamDevice(msp_sdl_stream);
                LOG_INFO(PLAYBACK_THREAD_NAME, "Resuming");
            }
            break;
        case MSP_SET_VOLUME:
            SDL_SetAudioStreamGain(msp_sdl_stream, command->payload.volume);
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

// Playback thread function. Decodes a frame and sends data to SDL2
void msp_decode_next_frame(playback_context_t *ctx) {
    // do not load CPU too much, keep only small buffer of decoded data
    if (SDL_GetAudioStreamQueued(msp_sdl_stream) > msp_sdl_queue_size_bytes) {
        SDL_Delay(SDL_DELAY_MS);
    }

    // Read the next frame
    int ret = av_read_frame(ctx->av_format_context, ctx->av_packet);
    if (ret < 0) {
        // something happened
        if (ret == AVERROR_EOF) {
            // End of file! Flushing decoder
            avcodec_send_packet(ctx->av_codec_context, nullptr);

            // Stop playback if all frames played and SDL q is empty
            if (SDL_GetAudioStreamQueued(msp_sdl_stream) == 0) {
                LOG_INFO(PLAYBACK_THREAD_NAME, "Playback finished (EOF)");
                ctx->status = MSP_STATUS_IDLE;
            }
        } else {
            // something bad happened
            msp_handle_ffmpeg_error("av_read_frame", ret);
        }
        av_packet_unref(ctx->av_packet);
        return;
    }

    if (ctx->av_packet->stream_index == ctx->audio_stream_index) {
        // Decode!
        ret = avcodec_send_packet(ctx->av_codec_context, ctx->av_packet);
        if (ret < 0 && ret != AVERROR(EAGAIN)) {
            msp_handle_ffmpeg_error("avcodec_send_packet", ret);
        }

        // Get decoded frames
        while (ret >= 0) {
            ret = avcodec_receive_frame(ctx->av_codec_context, ctx->av_frame);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                break; // not enough packets or EOF reached
            }
            if (ret < 0) {
                msp_handle_ffmpeg_error("avcodec_receive_frame", ret);
                break;
            }

            // Calculate buffer size, another ffmpeg magic
            const int64_t max_dst_nb_samples = av_rescale_rnd(
                swr_get_delay(ctx->swr_context, ctx->av_codec_context->sample_rate) + ctx->av_frame->nb_samples,
                msp_sdl_wanted_spec.freq,
                ctx->av_codec_context->sample_rate,
                AV_ROUND_UP
            );

            // Allocate intermediate buffer
            uint8_t *out_buffer = nullptr;
            int out_line_size = 0;
            ret = av_samples_alloc(
                &out_buffer,
                &out_line_size,
                msp_sdl_wanted_spec.channels,
                (int) max_dst_nb_samples,
                msp_av_sample_format,
                0
            );

            if (ret < 0) {
                LOG_ERROR(PLAYBACK_THREAD_NAME, "Failed to allocate audio samples buffer");
                av_frame_unref(ctx->av_frame);
                break;
            }

            // Resample!
            const int dst_nb_samples = swr_convert(
                ctx->swr_context,
                &out_buffer,
                (int) max_dst_nb_samples,
                // ReSharper disable once CppRedundantCastExpression
                // ffmpeg API expects const uint8_t *const *, AVFrame::data is uint8_t *[]
                (const uint8_t * const *) ctx->av_frame->data,
                ctx->av_frame->nb_samples
            );


            if (dst_nb_samples > 0) {
                const uint32_t bytes_per_sample = SDL_AUDIO_BYTESIZE(msp_sdl_wanted_spec.format);
                const uint32_t bytes_per_frame = (uint32_t) msp_sdl_wanted_spec.channels * bytes_per_sample;
                const uint32_t buffer_size = (uint32_t) dst_nb_samples * bytes_per_frame;

                // 8. Отправляем буфер в очередь через SDL_PutAudioStreamData
                SDL_PutAudioStreamData(msp_sdl_stream, out_buffer, (int32_t) buffer_size);
            }

            av_freep(&out_buffer);
            av_frame_unref(ctx->av_frame);
        }
    }

    av_packet_unref(ctx->av_packet);
}

void *msp_playback_thread_func(void *_) {
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

    msp_command_q = calloc(1, sizeof(*msp_command_q));
    msp_q_init(msp_command_q);

    if (!msp_init_sdl3()) {
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

    msp_deinit_sdl3();
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

bool msp_set_volume(float volume) {
    volume = fmaxf(0.0f, fminf(volume, 1.0f));
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
