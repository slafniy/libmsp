#include <stdio.h>

#include "../src/libmsp.h"

#include <unistd.h>

int main() {
    msp_init();

    const char *song1 = "/mnt/data/Music/Avatar/2023 - Dance Devil Dance/01. Dance Devil Dance.mp3";
    // const char *song1 = "../testapp/test.ogg";

    msp_play(song1);

    const char *keys[] = {"artist", "title"};
    constexpr size_t keys_count = sizeof(keys) / sizeof(keys[0]);
    char **values = msp_get_metadata(song1, keys, keys_count);
    if (values) {
        printf("NOW PLAYING >> %s - %s\n", values[0], values[1]);
    }
    msp_free_metadata_result(values, keys_count);

    sleep(3);
    msp_deinit();
    return 0;
}
