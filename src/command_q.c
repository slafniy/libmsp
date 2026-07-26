#include "command_q.h"

#include <pthread.h>

void msp_q_init(msp_q_t *q) {
    pthread_mutex_init(&q->mutex, nullptr);
}

void msp_q_destroy(msp_q_t *q) {
    pthread_mutex_destroy(&q->mutex);
}

bool msp_q_push(msp_q_t *q, msp_command_t command) {
    pthread_mutex_lock(&q->mutex);

    if (q->count == MSP_Q_SIZE) {
        pthread_mutex_unlock(&q->mutex);
        return false;
    }

    q->data[q->tail] = command;
    q->tail = (q->tail + 1) % MSP_Q_SIZE;
    q->count++;

    pthread_mutex_unlock(&q->mutex);
    return true;
}

msp_command_t msp_q_pop(msp_q_t *q) {
    pthread_mutex_lock(&q->mutex);
    msp_command_t command = q->data[q->head];
    q->head = (q->head + 1) % MSP_Q_SIZE;
    q->count--;

    pthread_mutex_unlock(&q->mutex);
    return command;
}


