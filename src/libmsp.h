#pragma once

#include <pthread.h>

// Player statuses
typedef enum {
    MSP_STATUS_IDLE = 0,
    MSP_STATUS_PLAYING,
    MSP_STATUS_PAUSED
} msp_status_t;

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

bool msp_set_position(int position_ms);

// =====================================================================================================================
// Data obtaining interface
// =====================================================================================================================
msp_status_t msp_get_status(void);

int msp_get_position_sec(void);

// Opens file, reads its metadata. Does NOT require msp_init(), can be called freely at any moment.
// Returns values for requested metadata keys (or NULL if not found),
// each key could be e.g. "artist", "title" etc
char **msp_get_metadata(const char *filename, const char **keys, size_t keys_count);

// Frees msp_get_metadata() result
void msp_free_metadata_result(char **values, size_t keys_count);
// =====================================================================================================================
