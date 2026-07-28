#pragma once

#include <pthread.h>

// Player statuses
typedef enum {
    MSP_STATUS_IDLE = 0,
    MSP_STATUS_PLAYING,
    MSP_STATUS_PAUSED,
    MSP_STATUS_ERROR
} msp_status_t;

// Track metadata
typedef struct {
    char *title;
    char *artist;
    char *album;
    char *year;
    char *format;
    int duration_sec;
    int bitrate;
} msp_track_meta_t;

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

msp_track_meta_t msp_get_metadata();

void msp_free_metadata(msp_track_meta_t *meta); // to free memory
// =====================================================================================================================
