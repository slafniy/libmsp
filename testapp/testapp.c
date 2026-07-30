#include <stdio.h>
#include <time.h>

#include "../src/libmsp.h"

#include <unistd.h>

static void sleep_ms(const int milliseconds) {
    struct timespec ts;
    ts.tv_sec = milliseconds / 1000;
    ts.tv_nsec = milliseconds % 1000 * 1000000L;
    nanosleep(&ts, nullptr);
}

int main() {
    printf("status: %i\n", msp_get_status());
    msp_init();
    printf("status: %i\n", msp_get_status());
    sleep_ms(100);
    printf("status: %i\n", msp_get_status());
    msp_set_volume(0.5);

    const char *song1 = "/mnt/data/Music/Avatar/2023 - Dance Devil Dance/01. Dance Devil Dance.mp3";
    // const char *song2 = "../testapp/test.ogg";

    for (int j = 0; j < 2; j++) {
        unsigned int dur = 0;
        printf("status: %i\n", msp_get_status());
        msp_play(song1);
        printf("status: %i\n", msp_get_status());
        sleep_ms(100);
        msp_toggle_pause();
        printf("status: %i\n", msp_get_status());
        sleep_ms(100);
        printf("status: %i\n", msp_get_status());
        msp_get_duration(&dur);
        printf("Duration: %u ms\n", dur);

        msp_toggle_pause();

        const char *keys[] = {"artist", "title"};
        constexpr size_t keys_count = sizeof(keys) / sizeof(keys[0]);
        char **values = msp_get_metadata(song1, keys, keys_count);
        if (values) {
            printf("NOW PLAYING >> %s - %s\n", values[0], values[1]);
        }
        msp_free_metadata_result(values, keys_count);


        msp_set_position(1000 * 15);

        unsigned int pos;
        for (int i = 0; i < 2; i++) {
            if (msp_get_position(&pos)) {
                printf("position: %02u:%02u\n", pos / 1000 / 60, pos / 1000 % 60);
            }
            sleep_ms(500);
        }
    }

    msp_deinit();
    printf("status: %i\n", msp_get_status());
    sleep_ms(100);
    printf("status: %i\n", msp_get_status());
    return 0;
}
