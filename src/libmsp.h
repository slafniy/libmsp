/**
 * @file libmsp.h
 * @brief Public interface of libmsp.
 *
 * Features:
 *  - non-blocking audio playback (uses background thread)
 *  - supports mp3, ogg, m4a, flac
 *  - pause/resume
 *  - seeking
 *  - volume control
 *  - metadata extraction
 *  - current playback position and track duration queries
 *
 * Usage example:
 *
 *   msp_init();
 *   msp_play("music.mp3");
 *   ...
 *   // your code can do other tasks here, or just sleep()
 *   ...
 *   msp_deinit();
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
    MSP_STATUS_PAUSED
} player_status_t;

/**
 * Initializes the library. Should be called once before any other functions with a little exception:
 * the only allowed function to call without initialization is msp_get_metadata().
 * @return true if initialized successfully, false otherwise.
 */
bool msp_init(void);

/**
 * Gracefully destroys inner library context. Should be called once when you do not need to use the library anymore.
 */
void msp_deinit(void);

/**
 * Requests to open file and start playback in background. Does not block.
 * @note You cannot be sure that playback is started in a moment this function returns.
 * @param filename file to play.
 * @return true if the command successfully placed in the command queue, false otherwise.
 */
bool msp_play(const char *filename);

/**
 * Toggle playback pause. Does nothing if nothing's open.
 * @note You cannot call msp_get_status() right after and expect the new status.
 * @return true if the command successfully placed in the command queue, false otherwise.
 */
bool msp_toggle_pause(void);

/**
 * Stops current playback. Works like a pause. DO NOT USE https://github.com/slafniy/libmsp/issues/5
 * @return true if the command successfully placed in the command queue, false otherwise.
 */
bool msp_stop(void);

/**
 * Adjusts volume level.
 * @param volume wanted volume level, from 0.0 to 1.0. Any value out of this range will be silently clamped to it.
 * @return true if the command successfully placed in the command queue, false otherwise.
 */
bool msp_set_volume(float volume);

/**
 * Tries to set a new current playback position. Silently clamps any out of track range values.
 * @param position_ms the place in the file where you want to move current playback.
 * @return true if the command successfully placed in the command queue, false otherwise.
 */
bool msp_set_position(uint32_t position_ms);

/**
 * Gets current playback position in milliseconds.
 * @note calling this right after msp_play() most likely will return -1.
 * You should give some time for playback to start.
 * @return current playback position if it knows it, -1 otherwise (no file, broken file, not opened yet)
 */
int64_t msp_get_position();

/**
 * Receives a duration of current file. To get a sentient result make sure libmsp is initialized and some file
 * is opened.
 * @return current file duration in milliseconds, or -1 if there is no file opened.
 */
int64_t msp_get_duration();

/**
 * Used to obtain current playback status.
 * @return player_status_t enum value. Returns MSP_STATUS_UNINITIALIZED in case if playback context is not initialized.
 * @note You cannot expect this function to return the new status when called right
 * after playback control commands, e.g. msp_toggle_pause(), because playback thread works in background.
 * The delay won't be huge though. You can expect it to react in several ms.
 */
player_status_t msp_get_status();

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
