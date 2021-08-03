#include "hw.h"

#include "display.h"

void hw_init(esp_event_loop_handle_t event_loop) {

    display_init(event_loop);

}
