#include "../src/libmsp.h"

#include <unistd.h>

int main() {
    msp_init();
    msp_play("../testapp/test.ogg");
    sleep(1);
    return 0;
}