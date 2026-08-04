/**
 * @file libmsp.h
 * @brief Public interface of libmsp.
 *
 * Features:
 *  - non-blocking audio playback (uses background thread)
 *  - supports .mp3, .ogg (opus & vorbis), .m4a, .flac, .wav, .aac
 *  - pause/resume
 *  - seeking
 *  - volume control
 *  - metadata extraction
 *  - current playback position and track duration queries
 *
 * Usage example:
 *
 *   playback_context_t *player = msp_init();
 *   msp_play(player, "music.mp3");
 *   ...
 *   // your code can do other tasks here, or just sleep()
 *   ...
 *   msp_deinit(player);
 */

#pragma once

#include <stdint.h>

/**
 * Describes current playback thread status
 */
typedef enum {
    MSP_STATUS_UNINITIALIZED = 0, // special case to return when the context does not exist yet/anymore
    MSP_STATUS_IDLE,
    MSP_STATUS_PLAYING,
    MSP_STATUS_PAUSED,
    MSP_STATUS_ERROR
} player_status_t;

/**
 * Handle for a playback context.
 */
typedef struct playback_context_t playback_context_t;

typedef void (*player_status_callback_t)(player_status_t new_status, void *user_data);

/**
 * Initializes the playback context. Returns context pointer which is used for other functions with a little exception:
 * the only allowed function to call without initialization is msp_get_metadata().
 * @return handle to initialized playback context.
 */
playback_context_t *msp_init(void);

/**
 * Gracefully destroys playback context. Should be called once per every msp_init().
 * @param ctx handle returned by msp_init(). An attempt to de-init NULL only causes a warning in the log.
 */
void msp_deinit(playback_context_t *ctx);

/**
 * Requests to open file and start playback in background. Does not block.
 * @note You cannot be sure that playback is started in a moment this function returns.
 * @param ctx handle returned by msp_init().
 * @param filename file to play.
 * @return true if the command successfully placed in the command queue, false otherwise.
 */
bool msp_play(const playback_context_t *ctx, const char *filename);

/**
 * Toggle playback pause. Does nothing if nothing's open.
 * @param ctx handle returned by msp_init().
 * @note You cannot call msp_get_status() right after and expect the new status.
 * @return true if the command successfully placed in the command queue, false otherwise.
 */
bool msp_toggle_pause(const playback_context_t *ctx);

/**
 * Stops current playback. Works like a pause. DO NOT USE https://github.com/slafniy/libmsp/issues/5
 * @param ctx handle returned by msp_init().
 * @return true if the command successfully placed in the command queue, false otherwise.
 */
bool msp_stop(const playback_context_t *ctx);

/**
 * Adjusts volume level.
 * @param ctx handle returned by msp_init().
 * @param volume wanted volume level, from 0.0 to 1.0. Any value out of this range will be silently clamped to it.
 * @return true if the command successfully placed in the command queue, false otherwise.
 */
bool msp_set_volume(const playback_context_t *ctx, float volume);

/**
 * Tries to set a new current playback position. Silently clamps any out of track range values.
 * @param ctx handle returned by msp_init().
 * @param position_ms the place in the file where you want to move current playback.
 * @return true if the command successfully placed in the command queue, false otherwise.
 */
bool msp_set_position(const playback_context_t *ctx, uint32_t position_ms);

/**
 * Gets current playback position in milliseconds.
 * @param ctx handle returned by msp_init().
 * @note calling this right after msp_play() most likely will return -1.
 * You should give some time for playback to start.
 * @return current playback position if it knows it, -1 otherwise (no file, broken file, not opened yet)
 */
int64_t msp_get_position(const playback_context_t *ctx);

/**
 * Receives a duration of current file. To get a sentient result make sure libmsp is initialized and some file
 * is opened.
 * @param ctx handle returned by msp_init().
 * @return current file duration in milliseconds, or -1 if there is no file opened.
 */
int64_t msp_get_duration(const playback_context_t *ctx);

/**
 * Used to obtain current playback status.
 * @param ctx handle returned by msp_init().
 * @return player_status_t enum value. Returns MSP_STATUS_UNINITIALIZED in case if playback context is not initialized.
 * @note You cannot expect this function to return the new status when called right
 * after playback control commands, e.g. msp_toggle_pause(), because playback thread works in background.
 * The delay won't be huge though. You can expect it to react in several ms.
 */
player_status_t msp_get_status(const playback_context_t *ctx);

bool msp_register_on_status_change_callback(playback_context_t *ctx, player_status_callback_t callback, void *user_data);

/**
 * Opens file, reads its metadata. Does NOT require msp_init(), can be called freely at any moment.
 * Returns values for requested metadata keys (or NULL if not found).
 * Caller is responsible to call msp_free_metadata_result() to free returned values.
 * @param filename file from which we want metadata.
 * @param keys each key could be e.g. "artist", "title" etc., case-insensitive.
 * There is no strict rules how fields named, every format has its own scheme, and some has several different.
 * @param keys_count how many keys you've passed into function.
 * @return pointer to char* values, you can access them by indexer. Count is equal to keys_count.
 */
char **msp_get_metadata(const char *filename, const char **keys, uint64_t keys_count);

/**
 * Frees msp_get_metadata() result
 * @param values char** returned by msp_get_metadata().
 * @param keys_count should be equal to keys_count you've used to call msp_get_metadata()
 * @example
 *  const char *keys[] = {"artist", "title"};
    constexpr size_t keys_count = sizeof(keys) / sizeof(keys[0]);
    char **values = msp_get_metadata("song.mp3", keys, keys_count);
    if (values) {
        printf("NOW PLAYING >> %s - %s\n", values[0], values[1]);
    }
    msp_free_metadata_result(values, keys_count);
 */
void msp_free_metadata_result(char **values, uint64_t keys_count);
