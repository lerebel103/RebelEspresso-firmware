#include "hw_specs.h"
#include "brew_tec.h"
#include "ready_indicator.h"



void hw_specs_init(esp_event_loop_handle_t event_loop) {
    brew_tec_init(event_loop);
    ready_indicator_init(event_loop);

}

void hw_specs_cfg_to_json(cJSON *root, const char *base_key) {
    auto brew_cfg = brew_tec_get_cfg();
    brew_cfg.to_json(root, base_key);

    auto ready_indicator_cfg = ready_indicator_get_cfg();
    ready_indicator_cfg.to_json(root, base_key);

}

void hw_specs_status_to_json(cJSON *root, const char *base_key) {
    auto brew_status = brew_tec_get_status();
    brew_status.to_json(root, base_key);

    auto ready_indicator_status = ready_indicator_get_status();
    ready_indicator_status.to_json(root, base_key);

}

void hw_specs_handle_new_cfg(const cJSON *cfg) {
    brew_tec_update_cfg(cfg);
    ready_indicator_update_cfg(cfg);

}
