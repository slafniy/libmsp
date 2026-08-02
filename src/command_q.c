#include "command_q.h"

#include <pthread.h>

void cq_init(cq_command_queue_t *q) {
    q->head = 0;
    q->tail = 0;
    q->count = 0;
    pthread_mutex_init(&q->mutex, nullptr);
    pthread_cond_init(&q->cond, nullptr);
}

void cq_destroy(cq_command_queue_t *q) {
    pthread_mutex_destroy(&q->mutex);
    pthread_cond_destroy(&q->cond);
}

bool cq_push(cq_command_queue_t *q, const cq_command_t command) {
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
void cq_pop(cq_command_queue_t *q, cq_command_t *out_command) {
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
bool cq_try_pop(cq_command_queue_t *q, cq_command_t *out_command) {
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
