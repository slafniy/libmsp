#include <stdio.h>

#include "../src/libmsp.h"

#include <unistd.h>

int main() {
    msp_init();
    msp_play("/mnt/data/Music/Avatar/2023 - Dance Devil Dance/01. Dance Devil Dance.mp3");
    const msp_track_meta_t meta = msp_get_metadata();
    printf("NOW PLAYING >> %s - %s", meta.artist, meta.title);
    sleep(3);
    msp_deinit();
    return 0;
}