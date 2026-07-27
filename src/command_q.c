#include "command_q.h"

#include <pthread.h>

void msp_q_init(msp_command_q_t *q) {
    q->head = 0;
    q->tail = 0;
    q->count = 0;
    pthread_mutex_init(&q->mutex, nullptr);
    pthread_cond_init(&q->cond, nullptr);
}

void msp_q_destroy(msp_command_q_t *q) {
    pthread_mutex_destroy(&q->mutex);
    pthread_cond_destroy(&q->cond);
}

bool msp_q_push(msp_command_q_t *q, const msp_command_t command) {
    pthread_mutex_lock(&q->mutex);

    if (q->count >= MSP_Q_SIZE) {
        pthread_mutex_unlock(&q->mutex);
        return false;
    }

    q->data[q->tail] = command;
    q->tail = (q->tail + 1) % MSP_Q_SIZE;
    q->count++;

    pthread_cond_signal(&q->cond);
    pthread_mutex_unlock(&q->mutex);

    return true;
}

// Blocks until something appears
void msp_q_pop(msp_command_q_t *q, msp_command_t *out_command) {
    pthread_mutex_lock(&q->mutex);

    while (q->count == 0) {
        pthread_cond_wait(&q->cond, &q->mutex);
    }

    *out_command = q->data[q->head];
    q->head = (q->head + 1) % MSP_Q_SIZE;
    q->count--;

    pthread_mutex_unlock(&q->mutex);
}

// Instant result
bool msp_q_try_pop(msp_command_q_t *q, msp_command_t *out_command) {
    pthread_mutex_lock(&q->mutex);

    if (q->count == 0) {
        pthread_mutex_unlock(&q->mutex);
        return false;
    }

    *out_command = q->data[q->head];
    q->head = (q->head + 1) % MSP_Q_SIZE;
    q->count--;

    pthread_mutex_unlock(&q->mutex);
    return true;
}


