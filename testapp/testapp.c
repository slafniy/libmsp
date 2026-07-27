#include "../src/libmsp.h"

#include <unistd.h>

int main() {
    msp_init();
    msp_play("../testapp/test.ogg");
    msp_toggle_pause();
    msp_set_volume(0.75f);
    msp_toggle_pause();
    msp_set_position(5431);
    msp_stop();
    sleep(1);
    msp_deinit();
    // sleep(1);
    return 0;
}