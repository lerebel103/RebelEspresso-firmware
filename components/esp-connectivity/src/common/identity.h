#pragma once

#include <esp_err.h>

struct identity_t {
  char *thing_type;
  int8_t hardware_major;
  int8_t hardware_minor;
  char *prov_template;
  char *rotate_template;
  char *ats_ep;
  char *jobs_ep;
  char *ca_cert;
  char *prov_cert;
  char *prov_private_key;
  char *device_cert;
  char *device_private_key;
};

const char *identity_thing_id();

const identity_t *identity_get();

void identity_reload();

esp_err_t identity_save_device_auth(const char *device_cert, size_t device_cert_len, const char *device_private_key,
                                    size_t device_private_key_len);
