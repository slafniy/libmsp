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

//======================================================================================================================
// Global constants
//======================================================================================================================
static constexpr char PLAYBACK_THREAD_NAME[] = "libmsp playback";
static constexpr char MAIN_THREAD_NAME[] = "libmsp main";

// Params of SDL audio output
static constexpr int SAMPLE_RATE = 48000;
static constexpr int AUDIO_OUT_FORMAT = SDL_AUDIO_S16; // should be in pair with SAMPLE_FORMAT!
static constexpr SDL_AudioSpec AUDIO_OUT_SPEC = {.freq = SAMPLE_RATE, .format = AUDIO_OUT_FORMAT, .channels = 2};
static constexpr AVChannelLayout CHANNEL_LAYOUT = AV_CHANNEL_LAYOUT_STEREO; // should match msp_sdl_wanted_spec!
static constexpr enum AVSampleFormat SAMPLE_FORMAT = AV_SAMPLE_FMT_S16; // should match AUDIO_OUT_FORMAT!
constexpr int MAX_STACK_SAMPLES = 8192; // should be enough for S16 * 2 channels

static constexpr float PLAYBACK_BUFFER_SIZE_SEC = 0.25f;
static constexpr int ALLOWED_AUDIO_DELAY_MS = 5; // How long is allowed to wait if playback buffer is already full

typedef struct status_callback_data_t {
    player_status_callback_t status_callback; // callback which called when status changes
    void *user_data; // callback context
    pthread_mutex_t mutex; // for thread-safe read and set fields above
} status_callback_data_t;

// Playback context, used to carry playback thread context between playback thread functions
typedef struct playback_context_t {
    // libmsp specific data
    char *filename; // path to current file
    _Atomic int64_t decoded_pos_ms; // current playback decoder position, slightly ahead of playback
    _Atomic int64_t duration_ms; // full track length
    _Atomic player_status_t status; // playback status, used to check playback thread state
    status_callback_data_t status_callback_data;

    // ffmpeg data about the current file: stream, decoder, metadata etc
    int audio_stream_index;
    AVFormatContext *av_format_context;
    AVCodecContext *av_codec_context;

    // decoder temporary data
    AVPacket *av_packet;
    AVFrame *av_frame;

    // resampler data
    SwrContext *swr_context;
    alignas(16) uint8_t out_buffer[MAX_STACK_SAMPLES * 2 * 2]; // 2 channels * 2 bytes for S16 format

    // other playback thread data
    pthread_t playback_thread; // handles commands and uses ffmpeg libs to open and play files
    bool playback_thread_started;
    cq_command_queue_t *command_q; // queue for commands for playback thread
    SDL_AudioStream *sdl_stream;
    uint32_t sdl_queue_size_bytes; // to determine SDL max queue size depending on audio parameters
} playback_context_t;

static void handle_ffmpeg_error(const char *what, const int err) {
    char buf[AV_ERROR_MAX_STRING_SIZE];
    av_strerror(err, buf, sizeof(buf));
    // TODO: figure out why system errors do not show
    fprintf(stderr, "libmsp: %s error: %s\n", what, buf);
}

static void calculate_duration_ms(playback_context_t *ctx) {
    if (!ctx || !ctx->av_format_context) return;

    const int64_t duration = ctx->av_format_context->duration;

    // happy case - container knows the duration
    if (duration != AV_NOPTS_VALUE && duration > 0) {
        ctx->duration_ms = (uint32_t) av_rescale(
            ctx->av_format_context->duration,
            1000,
            AV_TIME_BASE
        );
    } else {
        LOG_WARN(PLAYBACK_THREAD_NAME, "Cannot get track duration from format context, fallback to stream");
        const AVStream *stream = ctx->av_format_context->streams[ctx->audio_stream_index];

        if (stream->duration != AV_NOPTS_VALUE) {
            ctx->duration_ms = (uint32_t) av_rescale_q(
                stream->duration,
                stream->time_base,
                (AVRational){1, 1000}
            );
        } else {
            LOG_ERROR(PLAYBACK_THREAD_NAME, "Cannot get file duration!");
        }
    }
}


void set_status_call_callback(const player_status_t new_status, playback_context_t *ctx) {
    if (!ctx) return;

    // activate status change callback if we 1. have the callback 2. status is actually changed
    pthread_mutex_lock(&ctx->status_callback_data.mutex);
    const auto cb = ctx->status_callback_data.status_callback;
    const auto user_data = ctx->status_callback_data.user_data;
    pthread_mutex_unlock(&ctx->status_callback_data.mutex);

    if (cb && ctx->status != new_status) {
        cb(new_status, user_data);
    }

    ctx->status = new_status;
}

// Clears context, preparing it for the new file. Context should be initialized.
static void clear_playback_context(playback_context_t *ctx) {
    if (!ctx) {
        LOG_WARN("clear_playback_context()", "An attempt to clean an uninitialized playback context");
        return;
    }
    if (ctx->filename) {
        free(ctx->filename);
        ctx->filename = nullptr;
    }
    ctx->duration_ms = 0;
    ctx->decoded_pos_ms = 0;
    ctx->audio_stream_index = -1;
    set_status_call_callback(MSP_STATUS_IDLE, ctx);

    // Close per-file-unique contexts
    if (ctx->av_format_context) avformat_close_input(&ctx->av_format_context);
    if (ctx->av_codec_context) avcodec_free_context(&ctx->av_codec_context);
    if (ctx->swr_context) swr_free(&ctx->swr_context);
    // Do not free packet and frame, just unref, they could be reused without reallocation
    if (ctx->av_packet) av_packet_unref(ctx->av_packet);
    if (ctx->av_frame) av_frame_unref(ctx->av_frame);
}

static bool init_sdl3(playback_context_t *ctx) {
    if (!SDL_Init(SDL_INIT_AUDIO)) {
        LOG_ERROR(PLAYBACK_THREAD_NAME, "Cannot init SDL: %s", SDL_GetError());
        return false;
    }

    ctx->sdl_stream = SDL_OpenAudioDeviceStream(
        SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,
        &AUDIO_OUT_SPEC,
        nullptr,
        nullptr);

    if (!ctx->sdl_stream) {
        LOG_ERROR(MAIN_THREAD_NAME, "Failed to open audio device stream: %s", SDL_GetError());
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        return false;
    }

    // Calculate audio device max allowed queue size
    constexpr uint32_t bytes_per_sample = SDL_AUDIO_BYTESIZE(AUDIO_OUT_SPEC.format);
    constexpr uint32_t bytes_per_frame = AUDIO_OUT_SPEC.channels * bytes_per_sample;
    ctx->sdl_queue_size_bytes = (uint32_t) (
        PLAYBACK_BUFFER_SIZE_SEC * (float) AUDIO_OUT_SPEC.freq * (float) bytes_per_frame);

    LOG_INFO(MAIN_THREAD_NAME, "SDL device initialized! Frequency: %i, Format: %s", AUDIO_OUT_SPEC.freq,
             SDL_GetAudioFormatName(AUDIO_OUT_SPEC.format));

    SDL_ResumeAudioStreamDevice(ctx->sdl_stream); // unpause because it's paused by default
    return true;
}

// Stops playback. Does nothing wrong if there is no one.
static void stop_playback(playback_context_t *ctx) {
    // clear audio stream
    SDL_PauseAudioStreamDevice(ctx->sdl_stream);
    SDL_ClearAudioStream(ctx->sdl_stream);
    // clear existing data from context
    clear_playback_context(ctx);
}

// Playback thread function. Opens file, looks for audio stream, codec etc.
static bool open_new_file(playback_context_t *ctx) {
    // Trying to open audio file
    int ret = avformat_open_input(&ctx->av_format_context, ctx->filename, nullptr, nullptr);
    if (ret < 0) {
        handle_ffmpeg_error("avformat_open_input", ret);
        LOG_ERROR(PLAYBACK_THREAD_NAME, "ffmpeg cannot open %s", ctx->filename);
        return false;
    }

    // Looking for container format
    ret = avformat_find_stream_info(ctx->av_format_context, nullptr);
    if (ret < 0) {
        handle_ffmpeg_error("avformat_find_stream_info", ret);
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
        handle_ffmpeg_error("av_find_best_stream", stream_idx);
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

    // set duration field
    calculate_duration_ms(ctx);

    // Prepare resampler
    ret = swr_alloc_set_opts2(
        &ctx->swr_context,
        &CHANNEL_LAYOUT,
        SAMPLE_FORMAT,
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
    if (!ctx->av_packet) ctx->av_packet = av_packet_alloc();
    if (!ctx->av_packet) {
        LOG_ERROR(PLAYBACK_THREAD_NAME, "av_packet_alloc() failed");
        return false;
    }
    if (!ctx->av_frame) ctx->av_frame = av_frame_alloc();
    if (!ctx->av_frame) {
        LOG_ERROR(PLAYBACK_THREAD_NAME, "av_frame_alloc() failed");
        return false;
    }

    LOG_INFO(PLAYBACK_THREAD_NAME, "Successfully loaded container <%s> with codec [%s]",
             ctx->av_format_context->iformat->name, codec->name);

    return true;
}

// Playback thread function. Sets current playback position and clears buffers to make transition seamless
static bool playback_set_position(playback_context_t *ctx, const int64_t position_ms) {
    if (!ctx || !ctx->av_format_context || ctx->audio_stream_index < 0) return false;

    const AVStream *stream = ctx->av_format_context->streams[ctx->audio_stream_index];

    const int64_t target_ts = av_rescale_q(
        position_ms,
        (AVRational){1, 1000},
        stream->time_base
    );

    const int ret = av_seek_frame(
        ctx->av_format_context,
        ctx->audio_stream_index,
        target_ts,
        AVSEEK_FLAG_BACKWARD
    );

    if (ret < 0) {
        handle_ffmpeg_error("av_seek_frame", ret);
        return false;
    }

    // Clear buffers to made instant transition
    if (ctx->av_codec_context) {
        avcodec_flush_buffers(ctx->av_codec_context);
    }
    if (ctx->swr_context) {
        swr_convert(ctx->swr_context, nullptr, 0, nullptr, 0);
    }
    if (ctx->sdl_stream) {
        SDL_ClearAudioStream(ctx->sdl_stream);
    }

    return true;
}

// Playback thread function. Determines what thread should do in the next iteration and sets playback_context->status
static void handle_command(playback_context_t *ctx, const cq_command_t *command) {
    switch (command->type) {
        case MSP_PLAY:
            stop_playback(ctx);
            // open new file
            ctx->filename = strdup(command->payload.filename);
            free(command->payload.filename);
            LOG_INFO(PLAYBACK_THREAD_NAME, "New music file: %s", ctx->filename);
            if (!open_new_file(ctx)) {
                LOG_ERROR(PLAYBACK_THREAD_NAME, "Cannot play %s", ctx->filename);
                set_status_call_callback(MSP_STATUS_ERROR, ctx);
                clear_playback_context(ctx);
                break;
            }
            set_status_call_callback(MSP_STATUS_PLAYING, ctx);
            SDL_ResumeAudioStreamDevice(ctx->sdl_stream);
            break;
        case MSP_STOP:
            stop_playback(ctx);
            LOG_INFO(PLAYBACK_THREAD_NAME, "Stopping current playback");
            break;
        case MSP_TOGGLE_PAUSE:
            if (ctx->status == MSP_STATUS_PLAYING) {
                set_status_call_callback(MSP_STATUS_PAUSED, ctx);
                SDL_PauseAudioStreamDevice(ctx->sdl_stream);
                LOG_INFO(PLAYBACK_THREAD_NAME, "Pausing");
            } else if (ctx->status == MSP_STATUS_PAUSED) {
                set_status_call_callback(MSP_STATUS_PLAYING, ctx);
                SDL_ResumeAudioStreamDevice(ctx->sdl_stream);
                LOG_INFO(PLAYBACK_THREAD_NAME, "Resuming");
            }
            break;
        case MSP_SET_VOLUME:
            SDL_SetAudioStreamGain(ctx->sdl_stream, command->payload.volume);
            LOG_INFO(PLAYBACK_THREAD_NAME, "Setting volume to %f", command->payload.volume);
            break;
        case MSP_SET_POSITION:
            LOG_INFO(PLAYBACK_THREAD_NAME, "Setting playback position to %ld ms", command->payload.position_ms);
            if (!playback_set_position(ctx, command->payload.position_ms)) {
                LOG_ERROR(PLAYBACK_THREAD_NAME, "Cannot seek to position %ld ms", command->payload.position_ms);
            }
            break;
        default:
            LOG_ERROR(PLAYBACK_THREAD_NAME, "Unknown command type received: %i", command->type);
            break;
    }
}

// Playback thread function. Updates current_pos_ms in context
static void update_playback_position(playback_context_t *ctx) {
    const int64_t pts = ctx->av_frame->best_effort_timestamp;
    // Normal case - should work almost always
    if (pts != AV_NOPTS_VALUE) {
        const AVStream *stream = ctx->av_format_context->streams[ctx->audio_stream_index];
        const int64_t frame_pts_ms = av_rescale_q(pts, stream->time_base, (AVRational){1, 1000});
        ctx->decoded_pos_ms = frame_pts_ms;
    }
    // If something in the file is broken
    else {
        // TODO: implement case if needed
        // https://github.com/slafniy/libmsp/issues/2
        // Should be something like
        // ctx->accumulated_pts_ms += ((int64_t)ctx->av_frame->nb_samples * 1000) /
        // ctx->av_codec_context->sample_rate;
        // And update accumulated_pts_ms on position change
    }
}

// Playback thread function. Decodes a frame and sends data to SDL2
static void decode_next_frame(playback_context_t *ctx) {
    // do not load CPU too much, keep only small buffer of decoded data
    while (SDL_GetAudioStreamQueued(ctx->sdl_stream) > ctx->sdl_queue_size_bytes) {
        SDL_Delay(ALLOWED_AUDIO_DELAY_MS);
    }

    // Read the next frame
    int ret = av_read_frame(ctx->av_format_context, ctx->av_packet);
    if (ret < 0) {
        // something happened
        if (ret == AVERROR_EOF) {
            // End of file! Flushing decoder
            avcodec_send_packet(ctx->av_codec_context, nullptr);

            // Stop playback if all frames played and SDL q is empty
            if (SDL_GetAudioStreamQueued(ctx->sdl_stream) == 0) {
                LOG_INFO(PLAYBACK_THREAD_NAME, "Playback finished (EOF)");
                set_status_call_callback(MSP_STATUS_IDLE, ctx);
            }
        } else {
            // something bad happened
            handle_ffmpeg_error("av_read_frame", ret);
        }
        av_packet_unref(ctx->av_packet);
        return;
    }

    if (ctx->av_packet->stream_index == ctx->audio_stream_index) {
        // Decode!
        ret = avcodec_send_packet(ctx->av_codec_context, ctx->av_packet);
        if (ret < 0 && ret != AVERROR(EAGAIN)) {
            handle_ffmpeg_error("avcodec_send_packet", ret);
        }

        // Get decoded frames
        while (ret >= 0) {
            ret = avcodec_receive_frame(ctx->av_codec_context, ctx->av_frame);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                break; // not enough packets or EOF reached
            }
            if (ret < 0) {
                handle_ffmpeg_error("avcodec_receive_frame", ret);
                break;
            }

            // We have the frame, so we can try to update current playback position
            update_playback_position(ctx);

            // Calculate buffer size, another ffmpeg magic
            const int64_t max_dst_nb_samples = av_rescale_rnd(
                swr_get_delay(ctx->swr_context, ctx->av_codec_context->sample_rate) + ctx->av_frame->nb_samples,
                AUDIO_OUT_SPEC.freq,
                ctx->av_codec_context->sample_rate,
                AV_ROUND_UP
            );

            // check if we're not out of bounds
            if (max_dst_nb_samples > MAX_STACK_SAMPLES) {
                LOG_ERROR(PLAYBACK_THREAD_NAME, "Sample count %ld exceeds stack limit!", max_dst_nb_samples);
                av_frame_unref(ctx->av_frame);
                break;
            }

            // Resample!
            uint8_t *out_buffer_ptr = ctx->out_buffer;
            const int dst_nb_samples = swr_convert(
                ctx->swr_context,
                &out_buffer_ptr,
                (int) max_dst_nb_samples,
                // ReSharper disable once CppRedundantCastExpression
                // ffmpeg API expects const uint8_t *const *, AVFrame::data is uint8_t *[]
                (const uint8_t * const *) ctx->av_frame->data,
                ctx->av_frame->nb_samples
            );

            if (dst_nb_samples > 0) {
                constexpr uint32_t bytes_per_sample = SDL_AUDIO_BYTESIZE(AUDIO_OUT_SPEC.format);
                constexpr uint32_t bytes_per_frame = (uint32_t) AUDIO_OUT_SPEC.channels * bytes_per_sample;
                const uint32_t buffer_size = (uint32_t) dst_nb_samples * bytes_per_frame;

                // Send data for playback
                SDL_PutAudioStreamData(ctx->sdl_stream, out_buffer_ptr, (int32_t) buffer_size);
            }

            av_frame_unref(ctx->av_frame);
        }
    }

    av_packet_unref(ctx->av_packet);
}

static void *playback_thread_func(void *arg) {
    playback_context_t *ctx = arg;
    set_status_call_callback(MSP_STATUS_IDLE, ctx);

    // Playback main loop. Listens commands and executes them, and decodes frames if needed
    while (true) {
        cq_command_t command;
        bool has_command = false;

        // If playing - quickly check if we got a new command and not pause decoding
        // TODO: add some sync here. If we decode fast enough, we still have plenty of time and can sleep more
        // https://github.com/slafniy/libmsp/issues/1
        if (ctx->status == MSP_STATUS_PLAYING) {
            has_command = cq_try_pop(ctx->command_q, &command);
        } else {
            // if not playing - just wait for a command
            cq_pop(ctx->command_q, &command);
            has_command = true;
        }

        // Exit requested case - breaks playback loop, which causes resource freeing
        if (has_command && command.type == MSP_EXIT) {
            LOG_INFO(PLAYBACK_THREAD_NAME, "Exit command received, shutting down");
            break;
        }

        // Handle other commands
        if (has_command) {
            handle_command(ctx, &command);
        }

        // Decode and send next frame to audio device, if needed.
        if (ctx->status == MSP_STATUS_PLAYING) {
            decode_next_frame(ctx);
        }
    }

    return nullptr;
}

static bool init_playback_context(playback_context_t *ctx) {
    ctx->playback_thread_started = false;

    // Init SDL audio output system
    if (!init_sdl3(ctx)) {
        LOG_ERROR(MAIN_THREAD_NAME, "Cannot initialize SDL3 audio stream");
        return false;
    }

    // Status change callback data
    pthread_mutex_init(&ctx->status_callback_data.mutex, nullptr);
    ctx->status_callback_data.status_callback = nullptr;
    ctx->status_callback_data.user_data = nullptr;

    // Initialize command q which transfers commands from main thread to playback thread
    ctx->command_q = calloc(1, sizeof(*ctx->command_q));
    if (!ctx->command_q) {
        LOG_ERROR(MAIN_THREAD_NAME, "msp_init(): cannot allocate memory for a command queue");
        return false;
    }

    cq_init(ctx->command_q);
    clear_playback_context(ctx);

    if (pthread_create(&ctx->playback_thread, nullptr, playback_thread_func, ctx) != 0) {
        LOG_ERROR(MAIN_THREAD_NAME, "msp_init(): cannot create playback thread");
        return false;
    }
    ctx->playback_thread_started = true;

    return true;
}

static void destroy_sdl3(playback_context_t *ctx) {
    if (ctx->sdl_stream) {
        SDL_PauseAudioStreamDevice(ctx->sdl_stream);
        SDL_ClearAudioStream(ctx->sdl_stream);
        SDL_DestroyAudioStream(ctx->sdl_stream);
        ctx->sdl_stream = nullptr;
    }
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
}

// Final de-initialization before application exit.
static void destroy_playback_context(playback_context_t **playback_context) {
    if (!*playback_context) return;
    const auto ctx_ptr = *playback_context;
    if (ctx_ptr->playback_thread_started) {
        pthread_join(ctx_ptr->playback_thread, nullptr);
        ctx_ptr->playback_thread_started = false;
    }

    if (ctx_ptr->filename) free(ctx_ptr->filename);
    if (ctx_ptr->av_format_context) avformat_close_input(&ctx_ptr->av_format_context); // also sets it to NULL
    if (ctx_ptr->av_codec_context) avcodec_free_context(&ctx_ptr->av_codec_context); // also sets it to NULL
    if (ctx_ptr->swr_context) swr_free(&ctx_ptr->swr_context);
    if (ctx_ptr->av_packet) av_packet_free(&ctx_ptr->av_packet);
    if (ctx_ptr->av_frame) av_frame_free(&ctx_ptr->av_frame);
    if (ctx_ptr->sdl_stream) destroy_sdl3(ctx_ptr); // also sets to NULL
    if (ctx_ptr->command_q) {
        cq_destroy(ctx_ptr->command_q);
        free(ctx_ptr->command_q);
    }
    pthread_mutex_destroy(&ctx_ptr->status_callback_data.mutex);

    free(ctx_ptr);
    *playback_context = nullptr;
}


// Sends a command to Playback thread
static bool exec_command(const cq_command_t command, const playback_context_t *ctx) {
    if (!cq_push(ctx->command_q, command)) {
        LOG_ERROR(MAIN_THREAD_NAME, "%s command failed: the command q is full!", cq_command_to_str(command.type));
        return false;
    }
    LOG_INFO(MAIN_THREAD_NAME, "%s command sent to playback thread", cq_command_to_str(command.type));
    return true;
}


// =====================================================================================================================
// Public interface functions implementation
// =====================================================================================================================

playback_context_t *msp_init() {
    playback_context_t *ctx = calloc(1, sizeof(*ctx));
    if (!ctx) {
        LOG_ERROR(MAIN_THREAD_NAME, "Failed to allocate playback context");
        return nullptr;
    }

    if (!init_playback_context(ctx)) {
        LOG_ERROR(MAIN_THREAD_NAME, "Failed to initialize playback context");
        destroy_playback_context(&ctx);
        return nullptr;
    }

    av_log_set_level(FFMPEG_LOG_LEVEL); // Set ffmpeg logging level to avoid console noise in release

    return ctx;
}

void msp_deinit(playback_context_t *ctx) {
    if (!ctx) {
        LOG_WARN(MAIN_THREAD_NAME, "An attempt to de-init NULL context. "
                 "Possibly there's a flow in your application logic");
        return;
    }
    const cq_command_t exit_cmd = {.type = MSP_EXIT};
    while (!cq_push(ctx->command_q, exit_cmd)) {
        // to be sure exit command passes. Shouldn't be happening much in the real life
    }
    destroy_playback_context(&ctx);
}

bool msp_play(const playback_context_t *ctx, const char *filename) {
    if (!ctx) {
        LOG_WARN(MAIN_THREAD_NAME, "msp_play() called with playback context == NULL");
        return false;
    }
    const cq_command_t command = {
        .type = MSP_PLAY,
        .payload.filename = strdup(filename)
    };

    const bool ret = exec_command(command, ctx);
    if (!ret) {
        free(command.payload.filename);
    }
    return ret;
}

bool msp_stop(const playback_context_t *ctx) {
    if (!ctx) {
        LOG_WARN(MAIN_THREAD_NAME, "msp_stop() called with playback context == NULL");
        return false;
    }
    const cq_command_t command = {.type = MSP_STOP};
    return exec_command(command, ctx);
}

bool msp_set_volume(const playback_context_t *ctx, float volume) {
    if (!ctx) {
        LOG_WARN(MAIN_THREAD_NAME, "msp_set_volume() called with playback context == NULL");
        return false;
    }
    volume = fmaxf(0.0f, fminf(volume, 1.0f));
    const cq_command_t command = {
        .type = MSP_SET_VOLUME,
        .payload.volume = volume
    };
    return exec_command(command, ctx);
}

bool msp_set_position(const playback_context_t *ctx, const uint32_t position_ms) {
    if (!ctx) {
        LOG_WARN(MAIN_THREAD_NAME, "msp_set_position() called with playback context == NULL");
        return false;
    }
    const cq_command_t command = {
        .type = MSP_SET_POSITION,
        .payload.position_ms = (int64_t) position_ms
    };
    return exec_command(command, ctx);
}

int64_t msp_get_position(const playback_context_t *ctx) {
    if (!ctx) {
        LOG_WARN(MAIN_THREAD_NAME, "msp_get_position() called with playback context == NULL");
        return false;
    }
    if (ctx->status == MSP_STATUS_IDLE) return -1;

    const int64_t pos = ctx->decoded_pos_ms;

    // Make correction: <playback position> = <decoded position> - <sdl audio device delay>
    int64_t delay_ms = 0;
    if (ctx->sdl_stream) {
        const uint32_t queued = SDL_GetAudioStreamQueued(ctx->sdl_stream);
        constexpr uint32_t bytes_per_frame = AUDIO_OUT_SPEC.channels * SDL_AUDIO_BYTESIZE(AUDIO_OUT_SPEC.format);
        delay_ms = queued * 1000 / (AUDIO_OUT_SPEC.freq * bytes_per_frame);
    }

    return pos > delay_ms ? pos - delay_ms : 0;
}

int64_t msp_get_duration(const playback_context_t *ctx) {
    if (!ctx) {
        LOG_WARN(MAIN_THREAD_NAME, "msp_get_duration() called with playback context == NULL");
        return false;
    }
    if (ctx->duration_ms == 0) return -1;

    return ctx->duration_ms;
}

player_status_t msp_get_status(const playback_context_t *ctx) {
    if (!ctx) return MSP_STATUS_UNINITIALIZED;
    return ctx->status;
}

bool msp_register_on_status_change_callback(playback_context_t *ctx, const player_status_callback_t callback,
                                            void *user_data) {
    if (!ctx) {
        LOG_ERROR(MAIN_THREAD_NAME, "Cannot register callback: ctx == NULL");
        return false;
    }
    if (!callback) {
        LOG_ERROR(MAIN_THREAD_NAME, "Cannot register callback: callback == NULL");
        return false;
    }

    pthread_mutex_lock(&ctx->status_callback_data.mutex);
    ctx->status_callback_data.status_callback = callback;
    ctx->status_callback_data.user_data = user_data;
    pthread_mutex_unlock(&ctx->status_callback_data.mutex);

    LOG_INFO(MAIN_THREAD_NAME, "player status callback is registered successfully");

    return true;
}

bool msp_toggle_pause(const playback_context_t *ctx) {
    if (!ctx) {
        LOG_WARN(MAIN_THREAD_NAME, "msp_get_duration() called with playback context == NULL");
        return false;
    }
    const cq_command_t command = {.type = MSP_TOGGLE_PAUSE};
    return exec_command(command, ctx);
}

// This function does not interact with playback thread
char **msp_get_metadata(const char *filename, const char **keys, const uint64_t keys_count) {
    if (!filename || !keys || keys_count == 0) {
        LOG_ERROR(MAIN_THREAD_NAME, "Invalid msp_get_metadata() params");
        return nullptr;
    }

    // Open file with deferred cleanup
    DEFERRED_CLEANUP(avformat_close_input)
            AVFormatContext *ctx = nullptr;
    const int ret = avformat_open_input(&ctx, filename, nullptr, nullptr);
    if (ret < 0) {
        handle_ffmpeg_error("avformat_open_input", ret);
        LOG_ERROR(MAIN_THREAD_NAME, "ffmpeg cannot open %s", filename);
        return nullptr;
    }

    // It's a caller responsibility to free this memory
    char **results = calloc(keys_count, sizeof(char *));
    if (!results) {
        LOG_ERROR(MAIN_THREAD_NAME, "Cannot allocate memory for results");
        return nullptr;
    }

    // Here we should have metadata
    for (size_t i = 0; i < keys_count; i++) {
        LOG_DEBUG(MAIN_THREAD_NAME, "Looking for '%s' in metadata", keys[i]);
        AVDictionaryEntry *entry = av_dict_get(ctx->metadata, keys[i], nullptr, 0);
        if (!entry) {
            LOG_DEBUG(MAIN_THREAD_NAME, "%s: not found", keys[i]);
            results[i] = nullptr;
        } else {
            results[i] = strdup(entry->value);
            LOG_DEBUG(MAIN_THREAD_NAME, "Found meta for key '%s': '%s'", keys[i], results[i]);
        }
    }

    return results;
}

void msp_free_metadata_result(char **values, const uint64_t keys_count) {
    if (!values) return;

    for (size_t i = 0; i < keys_count; i++) {
        free(values[i]);
    }
    free(values);
}
