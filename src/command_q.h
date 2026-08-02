#pragma once

#include <stddef.h>
#include <pthread.h>
#include <stdint.h>

static constexpr int MSP_Q_SIZE = 10;

typedef enum {
    MSP_PLAY,
    MSP_STOP,
    MSP_TOGGLE_PAUSE,
    MSP_SET_POSITION,
    MSP_SET_VOLUME,
    MSP_EXIT // special case to stop the playback thread
} cq_command_type_t;

// String representation of the enum above
static const char *cq_command_to_str(const cq_command_type_t type) {
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
    cq_command_type_t type;

    union {
        // If msp_q_push() returns true - worker thread MUST free() *filename
        // If msp_q_push() returns false (q overflow case) - caller thread MUST free() it itself
        char *filename;
        int64_t position_ms;
        float volume;
    } payload;
} cq_command_t;

typedef struct {
    cq_command_t data[MSP_Q_SIZE];
    size_t head;
    size_t tail;
    size_t count;

    pthread_mutex_t mutex;
    pthread_cond_t cond;
} cq_command_queue_t;

void cq_init(cq_command_queue_t *q);

void cq_destroy(cq_command_queue_t *q);

bool cq_push(cq_command_queue_t *q, cq_command_t command);

void cq_pop(cq_command_queue_t *q, cq_command_t *out_command);

bool cq_try_pop(cq_command_queue_t *q, cq_command_t *out_command);
