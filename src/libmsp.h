#pragma once

#include <pthread.h>

// =====================================================================================================================
// Init and de-init functions.
// =====================================================================================================================
// Creates playback thread and it's context. Should be called before any other function.
int msp_init(void);

// Shuts playback thread, frees all allocated memory
void msp_deinit(void);


// =====================================================================================================================
// Playback control interface
// All functions are non-blocking and only send commands to the background playback thread
// =====================================================================================================================
bool msp_play(const char *filename);

bool msp_toggle_pause(void);

bool msp_stop(void);

bool msp_set_volume(float volume);

bool msp_set_position(unsigned int position_ms);

// =====================================================================================================================
// Data obtaining interface
// =====================================================================================================================

bool msp_get_position(unsigned int *out_position_ms);

// Opens file, reads its metadata. Does NOT require msp_init(), can be called freely at any moment.
// Returns values for requested metadata keys (or NULL if not found),
// each key could be e.g. "artist", "title", etc.
// Caller is responsible to call msp_free_metadata_result() to free returned values.
char **msp_get_metadata(const char *filename, const char **keys, size_t keys_count);

// Frees msp_get_metadata() result
void msp_free_metadata_result(char **values, size_t keys_count);

// =====================================================================================================================
