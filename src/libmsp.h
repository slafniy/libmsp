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
 * Note that you cannot be sure that playback is started in a moment this function returns.
 * @param filename file to play.
 * @return true. There is no any special handling for now.
 */
bool msp_play(const char *filename);

/**
 * Toggle playback pause. Does nothing if nothing's open.
 * @return true. There is no special handling.
 */
bool msp_toggle_pause(void);

/**
 * Stops current playback. Works like a pause. DO NOT USE https://github.com/slafniy/libmsp/issues/5
 * @return true
 */
bool msp_stop(void);

/**
 * Adjusts volume level.
 * @param volume wanted volume level, from 0.0 to 1.0. Any value out of this range will be silently clamped to it.
 * @return true. No special handling.
 */
bool msp_set_volume(float volume);

/**
 * Tries to set a new current playback position. Silently clamps any out of track range values.
 * @param position_ms
 * @return true. No special handling.
 */
bool msp_set_position(uint32_t position_ms);

/**
 * Gets current playback position in milliseconds.
 * Note: calling this right after msp_play() most likely will return false and out_position_ms == 0.
 * You should give some time for playback to start.
 * @param out_position_ms current playback position, make sense only if function returned true
 * @return true if it can find playback position, false otherwise (no file, broken file, not opened yet)
 */
bool msp_get_position(uint32_t *out_position_ms);

/**
 * Receives a duration of current file. To get a sentient result make sure libmsp is initialized and some file
 * is opened.
 * @param out_duration_ms - current file duration in milliseconds.
 * @return true if got something sentient, false otherwise.
 */
bool msp_get_duration(uint32_t *out_duration_ms);

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
