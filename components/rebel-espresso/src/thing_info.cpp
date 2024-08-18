#include "thing_info.h"
#include <lwipopts.h>
#include <inttypes.h>

#include <esp_log.h>
#include <memory.h>
#include <src/hw/base/eeprom.h>
#include <freertos/task.h>
#include <_generated/version.h>
#include <esp_mac.h>

#include "esp_console.h"
#include "hw_config.h"

/**
 * Magic 2-bytes to identify that EEPROM contains valid data
 */
#define MAGIC_HEADER 0x6B4A

#define TAG "thing_info"

#define THING_ID_MAX 32

static bool s_quit = false;
static char s_thing_id[THING_ID_MAX] = {0};
static thing_info_ext_t g_thing_info_ext = {};

static int _set_serial(int argc, char **argv) {
  if (argc != 2) {
    printf("Wrong number of arguments: %d\n", argc);
    return -1;
  }

  int n = sscanf(argv[1], "%llu", &g_thing_info_ext.serial);
  if (n != 1) {
    printf("Did not parse a serial\n");
    return -1;
  }

  printf("Serial set to %07llu\n", g_thing_info_ext.serial);
  return 0;
}

static int _set_hardware_revision_minor(int argc, char **argv) {
  if (argc != 2) {
    printf("Wrong number of arguments: %d\n", argc);
    return -1;
  }

  int n = sscanf(argv[1], "%" SCNu8, &g_thing_info_ext.hardware_version_minor);
  if (n != 1) {
    printf("Did not parse a hardware minor revision number\n");
    return -1;
  }

  printf("Hardware minor revision set to %" PRIu8 "\n", g_thing_info_ext.hardware_version_minor);
  return 0;
}

static int _set_manufacturer(int argc, char **argv) {
  if (argc != 2) {
    printf("Wrong number of arguments: %d\n", argc);
    return -1;
  }

  int n = sscanf(argv[1], "%" SCNu16, &g_thing_info_ext.manufacturer_id);
  if (n != 1) {
    printf("Did not parse manufacturer number\n");
    return -1;
  }

  printf("Hardware minor revision set to %04d\n", g_thing_info_ext.manufacturer_id);
  return 0;
}

static int _set_epoch(int argc, char **argv) {
  if (argc != 2) {
    printf("Wrong number of arguments: %d\n", argc);
    return -1;
  }

  int n = sscanf(argv[1], "%" SCNu64, &g_thing_info_ext.build_epoch_s);
  if (n != 1) {
    printf("Did not parse epoch number\n");
    return -1;
  }

  printf("Build epoch set to %llu\n", g_thing_info_ext.build_epoch_s);
  return 0;
}

static int _show(int argc, char **argv) {
  printf("Hardware Info:\n");
  printf("    Type:                %s\r\n", g_thing_info_ext.thing_type);
  printf("    Hardware revision:   %" PRIu8 ".%" PRIu8 "\r\n", g_thing_info_ext.hardware_version_major,
         g_thing_info_ext.hardware_version_minor);
  printf("    Serial:              %07llu\r\n", g_thing_info_ext.serial);
  printf("    Manufacturer id:     %04u\r\n", g_thing_info_ext.manufacturer_id);
  printf("    Build epoch (s):     %llu\r\n", g_thing_info_ext.build_epoch_s);
  return 0;
}

static int _commit_eeprom(int argc, char **argv) {
  printf("Commit to EEPROM ...\n");
  uint16_t addr = 0;
  esp_err_t err;

  uint16_t header = MAGIC_HEADER;
  size_t len = sizeof(header);
  err = eeprom_write_stream(addr, (const uint8_t *) (&header), len);
  if (err != ESP_OK) {
    return err;
  }
  addr += len;

  len = sizeof(g_thing_info_ext);
  auto *data = (const uint8_t *) (&g_thing_info_ext);
  err = eeprom_write_stream(addr, data, len);

  printf("Done\n");
  return err;
}

static int _quit(int argc, char **argv) {
  printf("Quitting\n");
  s_quit = true;
  return 0;
}

static void _prompt_eeprom_keys() {
  esp_console_repl_t *repl = NULL;
  esp_console_repl_config_t repl_config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
  esp_console_dev_uart_config_t uart_config = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
  repl_config.prompt = "eeprom>";

  const esp_console_cmd_t set_serial_cmd = {
      .command = "set-serial",
      .help = "Sets new serial",
      .hint = nullptr,
      .func = &_set_serial,
      .argtable = nullptr
  };
  ESP_ERROR_CHECK(esp_console_cmd_register(&set_serial_cmd));

  const esp_console_cmd_t set_version_cmd = {
      .command = "set-hardware-revision-minor",
      .help = "Sets new serial",
      .hint = nullptr,
      .func = &_set_hardware_revision_minor,
      .argtable = nullptr
  };
  ESP_ERROR_CHECK(esp_console_cmd_register(&set_version_cmd));

  const esp_console_cmd_t set_manufacturer = {
      .command = "set-manufacturer",
      .help = "Sets new serial",
      .hint = nullptr,
      .func = &_set_manufacturer,
      .argtable = nullptr
  };
  ESP_ERROR_CHECK(esp_console_cmd_register(&set_manufacturer));

  const esp_console_cmd_t set_epoch = {
      .command = "set-epoch",
      .help = "Sets new serial",
      .hint = nullptr,
      .func = &_set_epoch,
      .argtable = nullptr
  };
  ESP_ERROR_CHECK(esp_console_cmd_register(&set_epoch));

  const esp_console_cmd_t read_cmd = {
      .command = "show",
      .help = "Reads entire contents of EEPROM",
      .hint = nullptr,
      .func = &_show,
      .argtable = nullptr
  };
  ESP_ERROR_CHECK(esp_console_cmd_register(&read_cmd));

  const esp_console_cmd_t commit_cmd = {
      .command = "commit",
      .help = "Commits entire contents of EEPROM",
      .hint = nullptr,
      .func = &_commit_eeprom,
      .argtable = nullptr
  };
  ESP_ERROR_CHECK(esp_console_cmd_register(&commit_cmd));

  const esp_console_cmd_t quit_cmd = {
      .command = "quit",
      .help = "Quits eeprom mode",
      .hint = nullptr,
      .func = &_quit,
      .argtable = nullptr
  };
  ESP_ERROR_CHECK(esp_console_cmd_register(&quit_cmd));


  ESP_ERROR_CHECK(esp_console_new_repl_uart(&uart_config, &repl_config, &repl));

  printf("\n ==============================================================\n");
  printf(" |                  Write EEPROM mode                         |\n");
  printf(" |                                                            |\n");
  printf(" |         Try 'help', check all supported commands           |\n");
  printf(" |                                                            |\n");
  printf(" ==============================================================\n");

  // start console REPL
  ESP_ERROR_CHECK(esp_console_start_repl(repl));

  do {
    vTaskDelay(pdMS_TO_TICKS(100));
  } while (!s_quit);
}

static bool _load_eeprom() {
  uint16_t magic;
  size_t len = sizeof(magic);
  uint16_t addr = 0;
  ESP_ERROR_CHECK(eeprom_init());
  ESP_ERROR_CHECK(eeprom_read_stream(addr, (uint8_t *) &magic, len));
  if (magic != MAGIC_HEADER) {
    ESP_LOGE(TAG, "EEPROM not initialised.");
    return false;
  }
  addr += len;

  // Read all keys
  len = sizeof(g_thing_info_ext);
  auto *data = (uint8_t *) (&g_thing_info_ext);
  ESP_ERROR_CHECK(eeprom_read_stream(addr, data, len));
  _show(0, nullptr);

  return true;
}

/**
 * Gives us a native identifier based on the PCB mac address (WiFi)
 */
static void set_default_thing_id() {
  uint8_t l_Mac[6];
  esp_efuse_mac_get_default(l_Mac);

  // Convert MAC address as a unique ID. It must start with a letter and contain no special characters
  // so it can work with GCP
  snprintf(s_thing_id, sizeof(s_thing_id), "m%02hhX%02hhX%02hhX%02hhX%02hhX%02hhX",
           l_Mac[0], l_Mac[1], l_Mac[2], l_Mac[3], l_Mac[4], l_Mac[5]);
  strlwr(s_thing_id);

  // Preload device info from this firmware as best as possible
  strcpy(g_thing_info_ext.thing_type, THING_TYPE);
  sscanf(HARDWARE_REVISION_MAJOR, "%" SCNu8 ".%" SCNu8,
         &g_thing_info_ext.hardware_version_major,
         &g_thing_info_ext.hardware_version_minor);
}

const char *thing_info_id() {
#ifdef I2C_EEPROM_ADDRESS
  static char buf[THING_ID_MAX];
  sprintf(buf, "re-%d.%d-%07llu",
          g_thing_info_ext.hardware_version_major, g_thing_info_ext.hardware_version_minor, g_thing_info_ext.serial);
  return buf;
#else
  return s_thing_id;
#endif
}

const char *thing_info_hardware_revision() {
#ifdef I2C_EEPROM_ADDRESS
  static char buf[8];
  sprintf(buf, "%d.%d", g_thing_info_ext.hardware_version_major, g_thing_info_ext.hardware_version_minor);
  return buf;
#else
  return HARDWARE_REVISION;
#endif
}

const thing_info_ext_t *thing_info_ext() {
  return &g_thing_info_ext;
}


void thing_info_init() {
  set_default_thing_id();

  // Do we have an EEPROM?
#ifdef I2C_EEPROM_ADDRESS
  if (!_load_eeprom()) {
    _prompt_eeprom_keys();

    // Go again now
    ESP_ERROR_CHECK(_load_eeprom() ? ESP_OK : ESP_FAIL);
  }
#endif
}

