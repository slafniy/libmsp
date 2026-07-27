#pragma once

#include <stddef.h>
#include <pthread.h>

#define MSP_Q_SIZE 10

typedef enum {
    MSP_PLAY,
    MSP_STOP,
    MSP_TOGGLE_PAUSE,
    MSP_SET_POSITION,
    MSP_SET_VOLUME,
    MSP_EXIT // special case to stop the playback thread
} msp_command_type_t;

// String representation of the enum above
static const char *msp_command_to_str(msp_command_type_t type) {
    static const char *const names[] = {
        [MSP_PLAY] = "PLAY",
        [MSP_STOP] = "STOP",
        [MSP_TOGGLE_PAUSE] = "TOGGLE_PAUSE",
        [MSP_SET_POSITION] = "SET_POSITION",
        [MSP_SET_VOLUME] = "SET_VOLUME",
        [MSP_EXIT] = "EXIT",
    };

    if ((size_t) type < sizeof(names) / sizeof(names[0]) && names[type]) {
        return names[type];
    }
    return "UNKNOWN";
}

typedef struct {
    msp_command_type_t type;

    union {
        // If msp_q_push() returns true - worker thread MUST free() *filename
        // If msp_q_push() returns false (q overflow case) - caller thread MUST free() it itself
        char *filename;
        int position_ms;
        float volume;
    } payload;
} msp_command_t;

typedef struct {
    msp_command_t data[MSP_Q_SIZE];
    size_t head;
    size_t tail;
    size_t count;

    pthread_mutex_t mutex;
    pthread_cond_t cond;
} msp_command_q_t;

void msp_q_init(msp_command_q_t *q);

void msp_q_destroy(msp_command_q_t *q);

bool msp_q_push(msp_command_q_t *q, msp_command_t command);

msp_command_t msp_q_pop(msp_command_q_t *q);
