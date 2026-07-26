#pragma once

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

// Creates playback thread and it's context. Should be called before any other function.
int msp_init(void);

// Shuts playback thread, frees all allocated memory
void msp_deinit(void);


/// Playback control interface
int msp_play(const char *filename);

int msp_toggle_pause(void);

int msp_stop(void);


/// Data obtaining interface
msp_status_t msp_get_status(void);

int msp_get_position_sec(void);

int msp_get_metadata(msp_track_meta_t *out_meta);

void msp_free_metadata(msp_track_meta_t *meta);  // to free memory
