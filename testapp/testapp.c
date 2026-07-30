#include <stdio.h>
#include <time.h>

#include "../src/libmsp.h"

const char *song1 = "../testapp/test.ogg";

static void sleep_ms(const int milliseconds) {
    struct timespec ts;
    ts.tv_sec = milliseconds / 1000;
    ts.tv_nsec = milliseconds % 1000 * 1000000L;
    nanosleep(&ts, nullptr);
}

static void print_status_with_delay(const int delay_ms) {
    sleep_ms(delay_ms);
    printf("Status: %i\n", msp_get_status());
}

static void print_metadata(const char *filename) {
    const char *keys[] = {"artist", "title", "album", "year", "date", "filename", "album artist", "composer"};
    constexpr size_t keys_count = sizeof(keys) / sizeof(keys[0]);
    char **values = msp_get_metadata(filename, keys, keys_count);
    if (values) {
        for (size_t i = 0; i < keys_count; i++) {
            printf(">> %s: %s\n", keys[i], values[i]);
        }
    }
    msp_free_metadata_result(values, keys_count);
}

static void print_duration(void) {
    const auto dur = msp_get_duration();
    printf("Duration: %ld ms\n", dur);
}

int main() {
    print_status_with_delay(0);
    print_duration();
    msp_init();
    msp_set_volume(0.35f);
    print_status_with_delay(10);

    msp_play(song1);
    print_duration();
    print_status_with_delay(10);
    print_duration();

    msp_toggle_pause();
    print_metadata(song1);
    msp_set_position(1000 * 1);
    msp_toggle_pause();
    print_status_with_delay(10);
    sleep_ms(2500);

    msp_stop();
    sleep_ms(50);
    print_duration();
    print_status_with_delay(10);

    msp_play(song1);

    sleep_ms(2000);
    msp_deinit();
    print_metadata(song1);
    return 0;
}
