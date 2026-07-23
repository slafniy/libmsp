#include "libmsp.h"

#include <stdio.h>
#include <pthread.h>

static pthread_t playback_thread;

void *playback_thread_func(void *arg) {
    printf("Here should be the sound\n");
    return nullptr;
}

int msp_play(const char *filename) {
    pthread_create(
        &playback_thread,
        nullptr,
        playback_thread_func,
        nullptr);
    pthread_join(playback_thread, nullptr);
    return 0;
}
