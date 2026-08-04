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

static void print_status_with_delay(const playback_context_t *ctx, const int delay_ms) {
    sleep_ms(delay_ms);
    printf("Status: %i\n", msp_get_status(ctx));
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

static void print_duration(const playback_context_t *ctx) {
    const auto dur = msp_get_duration(ctx);
    printf("Duration: %ld ms\n", dur);
}

static void on_new_status_callback(const player_status_t status, const void *user_data) {
    printf(">> NEW PLAYER STATUS: %d\n", status);
}

int main() {
    constexpr int n = 10;
    playback_context_t *players[n];

    for (int i = 0; i < n; i++) {
        players[i] = msp_init();
        msp_register_on_status_change_callback(players[i], (player_status_callback_t)on_new_status_callback, nullptr);
        msp_play(players[i], song1);
        sleep_ms(500);
    }
    for (int i = 0; i < n; i++) {
        msp_deinit(players[i]);
    }


    playback_context_t *player = msp_init();
    msp_play(player, song1);
    print_status_with_delay(player, 0);
    print_duration(player);
    msp_set_volume(player, 0.35f);
    print_status_with_delay(player, 10);

    msp_play(player, song1);
    print_duration(player);
    print_status_with_delay(player, 10);
    print_duration(player);

    msp_toggle_pause(player);
    print_metadata(song1);
    msp_set_position(player, 1000 * 1);
    msp_toggle_pause(player);
    print_status_with_delay(player, 10);
    sleep_ms(2500);

    msp_stop(player);
    sleep_ms(50);
    print_duration(player);
    print_status_with_delay(player, 10);

    msp_play(player, song1);

    sleep_ms(2000);
    msp_deinit(player);
    print_metadata(song1);
    return 0;
}
