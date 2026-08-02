#include "identity.h"

#include <esp_mac.h>
#include <nvs.h>
#include <nvs_flash.h>

#include <cstring>
#include <string>

#define NVS_IDENTITY_NAMESPACE "identity"

#define NVS_THING_TYPE_KEY "thing_type"
#define NVS_HARDWARE_MAJOR_KEY "hw_major"
#define NVS_HARDWARE_MINOR_KEY "hw_minor"
#define NVS_STAGE_NAME_KEY "stage_name"
#define NVS_PROV_TEMPLATE_KEY "prov_templ"
#define NVS_ROTATE_TEMPLATE_KEY "rotate_templ"
#define NVS_ATS_ENDPOINT_KEY "ats_ep"
#define NVS_JOBS_ENDPOINT_KEY "jobs_ep"
#define NVS_CA_CERT_KEY "ca_cert"
#define NVS_PROV_CERT_KEY "prov_cert"
#define NVS_PROV_PRIVATE_KEY_KEY "prov_key"
#define NVS_DEVICE_CERT_KEY "device_cert"
#define NVS_DEVICE_PRIVATE_KEY_KEY "device_key"

static char g_macStr[32] = {0};

static identity_t s_identity = {};
static bool _loaded = false;

static void _load_client_credentials(nvs_handle_t nvs_handle) {
  size_t len;

  esp_err_t err = nvs_get_str(nvs_handle, NVS_DEVICE_CERT_KEY, nullptr, &len);
  if (err == ESP_OK) {
    free(s_identity.device_cert);
    s_identity.device_cert = (char *)calloc(len + 1, 1);
    ESP_ERROR_CHECK(nvs_get_str(nvs_handle, NVS_DEVICE_CERT_KEY, s_identity.device_cert, &len));
  }

  err = nvs_get_str(nvs_handle, NVS_DEVICE_PRIVATE_KEY_KEY, nullptr, &len);
  if (err == ESP_OK) {
    free(s_identity.device_private_key);
    s_identity.device_private_key = (char *)calloc(len + 1, 1);
    ESP_ERROR_CHECK(nvs_get_str(nvs_handle, NVS_DEVICE_PRIVATE_KEY_KEY, s_identity.device_private_key, &len));
  }
}

static void _load_identity() {
  nvs_handle_t nvs_handle;
  size_t len;
  ESP_ERROR_CHECK(nvs_open(NVS_IDENTITY_NAMESPACE, NVS_READWRITE, &nvs_handle));

  ESP_ERROR_CHECK(nvs_get_str(nvs_handle, NVS_THING_TYPE_KEY, nullptr, &len));
  free(s_identity.thing_type);
  s_identity.thing_type = (char *)calloc(len + 1, 1);
  ESP_ERROR_CHECK(nvs_get_str(nvs_handle, NVS_THING_TYPE_KEY, s_identity.thing_type, &len));

  ESP_ERROR_CHECK(nvs_get_i8(nvs_handle, NVS_HARDWARE_MAJOR_KEY, &s_identity.hardware_major));
  ESP_ERROR_CHECK(nvs_get_i8(nvs_handle, NVS_HARDWARE_MINOR_KEY, &s_identity.hardware_minor));

  ESP_ERROR_CHECK(nvs_get_str(nvs_handle, NVS_PROV_TEMPLATE_KEY, nullptr, &len));
  free(s_identity.prov_template);
  s_identity.prov_template = (char *)calloc(len + 1, 1);
  ESP_ERROR_CHECK(nvs_get_str(nvs_handle, NVS_PROV_TEMPLATE_KEY, s_identity.prov_template, &len));

  ESP_ERROR_CHECK(nvs_get_str(nvs_handle, NVS_ROTATE_TEMPLATE_KEY, nullptr, &len));
  free(s_identity.rotate_template);
  s_identity.rotate_template = (char *)calloc(len + 1, 1);
  ESP_ERROR_CHECK(nvs_get_str(nvs_handle, NVS_ROTATE_TEMPLATE_KEY, s_identity.rotate_template, &len));

  ESP_ERROR_CHECK(nvs_get_str(nvs_handle, NVS_ATS_ENDPOINT_KEY, nullptr, &len));
  free(s_identity.ats_ep);
  s_identity.ats_ep = (char *)calloc(len + 1, 1);
  ESP_ERROR_CHECK(nvs_get_str(nvs_handle, NVS_ATS_ENDPOINT_KEY, s_identity.ats_ep, &len));

  ESP_ERROR_CHECK(nvs_get_str(nvs_handle, NVS_JOBS_ENDPOINT_KEY, nullptr, &len));
  free(s_identity.jobs_ep);
  s_identity.jobs_ep = (char *)calloc(len + 1, 1);
  ESP_ERROR_CHECK(nvs_get_str(nvs_handle, NVS_JOBS_ENDPOINT_KEY, s_identity.jobs_ep, &len));

  ESP_ERROR_CHECK(nvs_get_str(nvs_handle, NVS_CA_CERT_KEY, nullptr, &len));
  free(s_identity.ca_cert);
  s_identity.ca_cert = (char *)calloc(len + 1, 1);
  ESP_ERROR_CHECK(nvs_get_str(nvs_handle, NVS_CA_CERT_KEY, s_identity.ca_cert, &len));

  ESP_ERROR_CHECK(nvs_get_str(nvs_handle, NVS_PROV_CERT_KEY, nullptr, &len));
  free(s_identity.prov_cert);
  s_identity.prov_cert = (char *)calloc(len + 1, 1);
  ESP_ERROR_CHECK(nvs_get_str(nvs_handle, NVS_PROV_CERT_KEY, s_identity.prov_cert, &len));

  ESP_ERROR_CHECK(nvs_get_str(nvs_handle, NVS_PROV_PRIVATE_KEY_KEY, nullptr, &len));
  free(s_identity.prov_private_key);
  s_identity.prov_private_key = (char *)calloc(len + 1, 1);
  ESP_ERROR_CHECK(nvs_get_str(nvs_handle, NVS_PROV_PRIVATE_KEY_KEY, s_identity.prov_private_key, &len));

  _load_client_credentials(nvs_handle);
  nvs_close(nvs_handle);
  _loaded = true;
}

static void _save_str_to_nvs(const char *key, const char *outValue, uint32_t outValueLength, nvs_handle_t nvs_handle) {
  std::string str(outValue, outValueLength);
  std::string::size_type index = 0;
  while ((index = str.find("\\n", index)) != std::string::npos) {
    str.replace(index, 2, "\n");
    ++index;
  }

  nvs_set_str(nvs_handle, key, str.c_str());
}

esp_err_t identity_save_device_auth(const char *device_cert, size_t device_cert_len, const char *device_private_key,
                                    size_t device_private_key_len) {
  nvs_handle_t nvs_handle;
  ESP_ERROR_CHECK(nvs_open(NVS_IDENTITY_NAMESPACE, NVS_READWRITE, &nvs_handle));
  _save_str_to_nvs(NVS_DEVICE_CERT_KEY, device_cert, device_cert_len, nvs_handle);
  _save_str_to_nvs(NVS_DEVICE_PRIVATE_KEY_KEY, device_private_key, device_private_key_len, nvs_handle);
  ESP_ERROR_CHECK(nvs_commit(nvs_handle));
  nvs_close(nvs_handle);

  return ESP_OK;
}

const char *identity_thing_id() {
  if (strlen(g_macStr) == 0) {
    uint8_t l_Mac[6];
    esp_efuse_mac_get_default(l_Mac);
    snprintf(g_macStr, sizeof(g_macStr), "%02hx:%02hx:%02hx:%02hx:%02hx:%02hx", l_Mac[0], l_Mac[1], l_Mac[2], l_Mac[3],
             l_Mac[4], l_Mac[5]);
  }
  return g_macStr;
}

const identity_t *identity_get() {
  if (!_loaded) {
    _load_identity();
  }

  return &s_identity;
}

void identity_reload() {
  _load_identity();
}
