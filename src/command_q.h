#pragma once

#include <stddef.h>
#include <pthread.h>

#define MSP_Q_SIZE 10

typedef enum {
    MSP_PLAY,
    MSP_PAUSE,
    MSP_RESUME,
    MSP_SET_POSITION,
    MSP_SET_VOLUME,
    MSP_EXIT
} msp_command_type_t;

typedef struct {
    msp_command_type_t type;

    union {
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
} msp_q_t;

void msp_q_init(msp_q_t *q);

void msp_q_destroy(msp_q_t *q);

bool msp_q_push(msp_q_t *q, msp_command_t command);

msp_command_t msp_q_pop(msp_q_t *q);
