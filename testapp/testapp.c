#include "../src/libmsp.h"

#include <unistd.h>

int main() {
    msp_init();
    // msp_play("../testapp/non-existing.ogg");
    // msp_play("../testapp/testapp.c");  // existing but wrong format
    msp_set_volume(0.7f);
    msp_set_volume(1.7f);
    msp_play("../testapp/test.ogg");
    sleep(1);
    // msp_toggle_pause();
    // msp_set_volume(0.75f);
    // msp_toggle_pause();
    // msp_set_position(5431);
    // sleep(2);
    // msp_stop();
    // sleep(1);
    msp_play("/mnt/data/Music/Avatar/2023 - Dance Devil Dance/01. Dance Devil Dance.mp3");
    // sleep(1);
    sleep(600);
    msp_play("/mnt/data/Music/Be'lakor/08 The Smoke of Many Fires.m4a");
    // msp_deinit();
    sleep(900);
    return 0;
}