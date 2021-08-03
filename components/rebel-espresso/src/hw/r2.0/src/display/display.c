#include "display.h"

static esp_event_loop_handle_t s_event_loop;

void display_init(esp_event_loop_handle_t event_loop) {
    s_event_loop = event_loop;
}