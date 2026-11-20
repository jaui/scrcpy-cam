/*
 * scrcpy-cam bt-hid: BLE keyboard (+ consumer keys) controlled over USB serial.
 *
 * Target: ESP32-C3 / -C6 / -S3 (Bluetooth LE, HID over GATT), ESP-IDF 5.4,
 * NimBLE. Based on the ESP-IDF example "esp_hid_device"
 * (Unlicense OR CC0-1.0).
 *
 * Line protocol on the USB serial port (115200 8N1, lines end with \n):
 *
 *   PING                 -> OK pong
 *   STATUS               -> OK connected=<0|1> bonds=<n> pairing=<0|1>
 *   PAIR [seconds]       -> OK   allow a new pairing for <seconds> (def. 120)
 *   UNPAIR               -> OK   delete all bonds (and disconnect)
 *   TYPE <text>          -> OK   type ASCII text (US key positions)
 *   KEY <k>[+<k>...]     -> OK   press a key combination, e.g. KEY ENTER,
 *                                KEY CTRL+A, KEY GUI+H
 *   CC <name>            -> OK   consumer key: HOME BACK POWER VOLUP VOLDOWN
 *                                MUTE PLAYPAUSE MENU SEARCH
 *
 * Errors: "ERR <reason>". Asynchronous events: "EVT <...>".
 * Logging is limited to warnings so that the protocol stays readable.
 */

#include <ctype.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"

#include "host/ble_hs.h"
#include "host/ble_store.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"

void ble_store_config_init(void);

#include "esp_hidd.h"
#include "esp_hid_gap.h"

static const char *TAG = "bt-hid";

#define DEVICE_NAME "scrcpy-cam HID"
#define KEY_PRESS_MS 20
#define KEY_RELEASE_MS 20
#define DEFAULT_PAIRING_SECONDS 120

#define RPT_ID_KEYBOARD 1
#define RPT_ID_CONSUMER 2

/* Keyboard (report 1: modifiers, reserved, 6 keys; LED output) and consumer
 * control (report 2: one 16-bit usage) */
static const uint8_t report_map[] = {
    0x05, 0x01,        // Usage Page (Generic Desktop)
    0x09, 0x06,        // Usage (Keyboard)
    0xA1, 0x01,        // Collection (Application)
    0x85, RPT_ID_KEYBOARD, // Report ID
    0x05, 0x07,        //   Usage Page (Keyboard/Keypad)
    0x19, 0xE0,        //   Usage Minimum (Left Control)
    0x29, 0xE7,        //   Usage Maximum (Right GUI)
    0x15, 0x00,        //   Logical Minimum (0)
    0x25, 0x01,        //   Logical Maximum (1)
    0x75, 0x01,        //   Report Size (1)
    0x95, 0x08,        //   Report Count (8)
    0x81, 0x02,        //   Input (Data,Var,Abs)        modifiers
    0x95, 0x01,        //   Report Count (1)
    0x75, 0x08,        //   Report Size (8)
    0x81, 0x01,        //   Input (Const)               reserved
    0x95, 0x05,        //   Report Count (5)
    0x75, 0x01,        //   Report Size (1)
    0x05, 0x08,        //   Usage Page (LEDs)
    0x19, 0x01,        //   Usage Minimum (Num Lock)
    0x29, 0x05,        //   Usage Maximum (Kana)
    0x91, 0x02,        //   Output (Data,Var,Abs)       LEDs
    0x95, 0x01,        //   Report Count (1)
    0x75, 0x03,        //   Report Size (3)
    0x91, 0x01,        //   Output (Const)              padding
    0x95, 0x06,        //   Report Count (6)
    0x75, 0x08,        //   Report Size (8)
    0x15, 0x00,        //   Logical Minimum (0)
    0x25, 0xFF,        //   Logical Maximum (255)
    0x05, 0x07,        //   Usage Page (Keyboard/Keypad)
    0x19, 0x00,        //   Usage Minimum (0)
    0x29, 0xFF,        //   Usage Maximum (255)
    0x81, 0x00,        //   Input (Data,Array,Abs)      keys
    0xC0,              // End Collection

    0x05, 0x0C,        // Usage Page (Consumer)
    0x09, 0x01,        // Usage (Consumer Control)
    0xA1, 0x01,        // Collection (Application)
    0x85, RPT_ID_CONSUMER, // Report ID
    0x15, 0x00,        //   Logical Minimum (0)
    0x26, 0xFF, 0x03,  //   Logical Maximum (0x3FF)
    0x19, 0x00,        //   Usage Minimum (0)
    0x2A, 0xFF, 0x03,  //   Usage Maximum (0x3FF)
    0x75, 0x10,        //   Report Size (16)
    0x95, 0x01,        //   Report Count (1)
    0x81, 0x00,        //   Input (Data,Array,Abs)
    0xC0,              // End Collection
};

static esp_hid_raw_report_map_t report_maps[] = {
    {.data = report_map, .len = sizeof(report_map)},
};

static esp_hid_device_config_t hid_config = {
    .vendor_id = 0x16C0,
    .product_id = 0x05DF,
    .version = 0x0100,
    .device_name = DEVICE_NAME,
    .manufacturer_name = "scrcpy-cam",
    .serial_number = "0001",
    .report_maps = report_maps,
    .report_maps_len = 1,
};

static esp_hidd_dev_t *s_hid_dev;
static SemaphoreHandle_t s_out_mutex;
static volatile bool s_connected;
// An odd value identifies an encrypted session. A reconnect changes the value.
static atomic_uint s_hid_session;
static volatile int64_t s_pairing_until_us; // pairing allowed until (esp_timer)
static int s_known_bonds;                   // bonds accepted so far

/* --- serial output ------------------------------------------------------ */

static void
out(const char *fmt, ...) {
    xSemaphoreTake(s_out_mutex, portMAX_DELAY);
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    printf("\n");
    fflush(stdout);
    xSemaphoreGive(s_out_mutex);
}

/* --- bonds and pairing mode --------------------------------------------- */

static int
bond_count(void) {
    int count = 0;
    if (ble_store_util_count(BLE_STORE_OBJ_TYPE_OUR_SEC, &count)) {
        return -1;
    }
    return count;
}

bool
app_pairing_allowed(void) {
    return esp_timer_get_time() < s_pairing_until_us;
}

static void
set_hid_ready(bool ready) {
    unsigned session = atomic_load(&s_hid_session);
    if ((session & 1u) != (unsigned) ready) {
        atomic_store(&s_hid_session, session + 1);
    }
}

void
app_on_gap_event(struct ble_gap_event *event) {
    struct ble_gap_conn_desc desc;

    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            s_connected = true;
            out("EVT connected");
        }
        break;
    case BLE_GAP_EVENT_DISCONNECT:
        set_hid_ready(false);
        s_connected = false;
        out("EVT disconnected reason=%d", event->disconnect.reason);
        break;
    case BLE_GAP_EVENT_ENC_CHANGE:
        if (event->enc_change.status != 0) {
            set_hid_ready(false);
            out("EVT encryption_failed status=%d", event->enc_change.status);
            break;
        }
        if (ble_gap_conn_find(event->enc_change.conn_handle, &desc)) {
            break;
        }
        int bonds = bond_count();
        if (bonds > s_known_bonds) {
            // A new bond was created by this connection
            if (!app_pairing_allowed()) {
                out("EVT pairing_rejected (pairing mode off)");
                ble_store_util_delete_peer(&desc.peer_id_addr);
                ble_gap_terminate(event->enc_change.conn_handle,
                                  BLE_ERR_AUTH_FAIL);
                break;
            }
            s_known_bonds = bonds;
            s_pairing_until_us = 0; // one pairing per PAIR command
            out("EVT paired bonds=%d", bonds);
        }
        set_hid_ready(desc.sec_state.encrypted);
        if (desc.sec_state.encrypted) {
            out("EVT ready");
        }
        break;
    default:
        break;
    }
}

/* --- HID reports -------------------------------------------------------- */

static bool
send_input(unsigned session, uint8_t report_id, uint8_t *report, size_t size,
           unsigned press_ms) {
    if (!(session & 1u) || session != atomic_load(&s_hid_session) || !s_hid_dev) {
        return false;
    }
    if (esp_hidd_dev_input_set(s_hid_dev, 0, report_id, report, size) != ESP_OK) {
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(press_ms));
    memset(report, 0, size);
    if (session != atomic_load(&s_hid_session)
            || esp_hidd_dev_input_set(s_hid_dev, 0, report_id, report, size)
                != ESP_OK) {
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(KEY_RELEASE_MS));
    return session == atomic_load(&s_hid_session);
}

static bool
send_keyboard(unsigned session, uint8_t modifiers, uint8_t key) {
    uint8_t report[8] = {modifiers, 0, key, 0, 0, 0, 0, 0};
    return send_input(session, RPT_ID_KEYBOARD, report, sizeof(report),
                      KEY_PRESS_MS);
}

static bool
send_consumer(uint16_t usage) {
    uint8_t report[2] = {usage & 0xFF, usage >> 8};
    return send_input(atomic_load(&s_hid_session), RPT_ID_CONSUMER, report,
                      sizeof(report), KEY_PRESS_MS * 2);
}

#define MOD_CTRL 0x01
#define MOD_SHIFT 0x02
#define MOD_ALT 0x04
#define MOD_GUI 0x08

// ASCII -> HID usage (US key positions). Return false if not typeable.
static bool
ascii_to_key(char c, uint8_t *mod, uint8_t *key) {
    static const char *shifted_digits = ")!@#$%^&*(";
    *mod = 0;
    if (c >= 'a' && c <= 'z') {
        *key = 0x04 + (c - 'a');
    } else if (c >= 'A' && c <= 'Z') {
        *mod = MOD_SHIFT;
        *key = 0x04 + (c - 'A');
    } else if (c >= '1' && c <= '9') {
        *key = 0x1E + (c - '1');
    } else if (c == '0') {
        *key = 0x27;
    } else if (c && strchr(shifted_digits, c)) {
        int d = strchr(shifted_digits, c) - shifted_digits; // 0..9
        *mod = MOD_SHIFT;
        *key = d == 0 ? 0x27 : 0x1E + (d - 1);
    } else {
        static const struct { char c; uint8_t key; bool shift; } map[] = {
            {' ', 0x2C, false}, {'\t', 0x2B, false}, {'\n', 0x28, false},
            {'-', 0x2D, false}, {'_', 0x2D, true},  {'=', 0x2E, false},
            {'+', 0x2E, true},  {'[', 0x2F, false}, {'{', 0x2F, true},
            {']', 0x30, false}, {'}', 0x30, true},  {'\\', 0x31, false},
            {'|', 0x31, true},  {';', 0x33, false}, {':', 0x33, true},
            {'\'', 0x34, false}, {'"', 0x34, true}, {'`', 0x35, false},
            {'~', 0x35, true},  {',', 0x36, false}, {'<', 0x36, true},
            {'.', 0x37, false}, {'>', 0x37, true},  {'/', 0x38, false},
            {'?', 0x38, true},
        };
        for (size_t i = 0; i < sizeof(map) / sizeof(map[0]); ++i) {
            if (map[i].c == c) {
                *key = map[i].key;
                *mod = map[i].shift ? MOD_SHIFT : 0;
                return true;
            }
        }
        return false;
    }
    return true;
}

static const struct {
    const char *name;
    uint8_t key;
} key_names[] = {
    {"ENTER", 0x28}, {"ESC", 0x29}, {"BACKSPACE", 0x2A}, {"TAB", 0x2B},
    {"SPACE", 0x2C}, {"RIGHT", 0x4F}, {"LEFT", 0x50}, {"DOWN", 0x51},
    {"UP", 0x52}, {"HOME", 0x4A}, {"END", 0x4D}, {"PGUP", 0x4B},
    {"PGDN", 0x4E}, {"DEL", 0x4C}, {"INSERT", 0x49}, {"MENU", 0x65},
    {"F1", 0x3A}, {"F2", 0x3B}, {"F3", 0x3C}, {"F4", 0x3D}, {"F5", 0x3E},
    {"F6", 0x3F}, {"F7", 0x40}, {"F8", 0x41}, {"F9", 0x42}, {"F10", 0x43},
    {"F11", 0x44}, {"F12", 0x45},
};

static const struct {
    const char *name;
    uint16_t usage;
} consumer_names[] = {
    {"HOME", 0x223},    {"BACK", 0x224},    {"POWER", 0x030},
    {"VOLUP", 0x0E9},   {"VOLDOWN", 0x0EA}, {"MUTE", 0x0E2},
    {"PLAYPAUSE", 0x0CD}, {"MENU", 0x040},  {"SEARCH", 0x221},
};

// Parse "CTRL+SHIFT+A" etc.
static bool
parse_combo(char *s, uint8_t *mod, uint8_t *key) {
    *mod = 0;
    *key = 0;
    for (char *tok = strtok(s, "+"); tok; tok = strtok(NULL, "+")) {
        if (!strcasecmp(tok, "CTRL")) {
            *mod |= MOD_CTRL;
        } else if (!strcasecmp(tok, "SHIFT")) {
            *mod |= MOD_SHIFT;
        } else if (!strcasecmp(tok, "ALT")) {
            *mod |= MOD_ALT;
        } else if (!strcasecmp(tok, "GUI") || !strcasecmp(tok, "META")) {
            *mod |= MOD_GUI;
        } else if (strlen(tok) == 1) {
            uint8_t m;
            if (!ascii_to_key(tolower((unsigned char) tok[0]), &m, key)) {
                return false;
            }
            *mod |= m;
        } else {
            bool found = false;
            for (size_t i = 0; i < sizeof(key_names) / sizeof(key_names[0]);
                    ++i) {
                if (!strcasecmp(tok, key_names[i].name)) {
                    *key = key_names[i].key;
                    found = true;
                    break;
                }
            }
            if (!found) {
                return false;
            }
        }
    }
    return *key || *mod;
}

/* --- command handling --------------------------------------------------- */

static void
handle_line(char *line) {
    char *arg = strchr(line, ' ');
    if (arg) {
        *arg++ = '\0';
    } else {
        arg = "";
    }

    if (!strcasecmp(line, "PING")) {
        out("OK pong");
    } else if (!strcasecmp(line, "STATUS")) {
        out("OK connected=%d bonds=%d pairing=%d", s_connected ? 1 : 0,
            bond_count(), app_pairing_allowed() ? 1 : 0);
    } else if (!strcasecmp(line, "PAIR")) {
        int seconds = *arg ? atoi(arg) : DEFAULT_PAIRING_SECONDS;
        if (seconds <= 0 || seconds > 3600) {
            out("ERR invalid duration");
            return;
        }
        s_known_bonds = bond_count();
        s_pairing_until_us = esp_timer_get_time() + (int64_t) seconds * 1000000;
        out("OK pairing for %d s", seconds);
    } else if (!strcasecmp(line, "UNPAIR")) {
        struct ble_gap_conn_desc desc;
        for (uint16_t h = 0; h < 8; ++h) {
            if (!ble_gap_conn_find(h, &desc)) {
                ble_gap_terminate(h, BLE_ERR_REM_USER_CONN_TERM);
            }
        }
        int rc = ble_store_clear();
        s_known_bonds = 0;
        out(rc ? "ERR could not clear bonds" : "OK");
    } else if (!strcasecmp(line, "TYPE")) {
        unsigned session = atomic_load(&s_hid_session);
        if (!(session & 1u)) {
            out("ERR HID not ready");
            return;
        }
        for (const char *p = arg; *p; ++p) {
            uint8_t mod, key;
            if (!ascii_to_key(*p, &mod, &key)) {
                out("ERR cannot type character 0x%02x", (unsigned char) *p);
                return;
            }
            if (!send_keyboard(session, mod, key)) {
                out("ERR keyboard report failed");
                return;
            }
        }
        out("OK");
    } else if (!strcasecmp(line, "KEY")) {
        uint8_t mod, key;
        if (!parse_combo(arg, &mod, &key)) {
            out("ERR unknown key");
            return;
        }
        out(send_keyboard(atomic_load(&s_hid_session), mod, key)
                ? "OK" : "ERR keyboard report failed");
    } else if (!strcasecmp(line, "CC")) {
        for (size_t i = 0; i < sizeof(consumer_names) / sizeof(consumer_names[0]);
                ++i) {
            if (!strcasecmp(arg, consumer_names[i].name)) {
                out(send_consumer(consumer_names[i].usage)
                        ? "OK" : "ERR consumer report failed");
                return;
            }
        }
        out("ERR unknown consumer key");
    } else if (*line) {
        out("ERR unknown command");
    }
}

static void
serial_task(void *param) {
    (void) param;
    char line[256];
    size_t len = 0;
    for (;;) {
        int c = fgetc(stdin);
        if (c == EOF) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        if (c == '\r' || c == '\n') {
            if (len) {
                line[len] = '\0';
                handle_line(line);
                len = 0;
            }
        } else if (len < sizeof(line) - 1) {
            line[len++] = (char) c;
        }
    }
}

/* --- HID device events -------------------------------------------------- */

static void
hidd_event_callback(void *handler_args, esp_event_base_t base, int32_t id,
                    void *event_data) {
    (void) handler_args;
    (void) base;
    (void) event_data;
    switch ((esp_hidd_event_t) id) {
    case ESP_HIDD_START_EVENT:
        esp_hid_ble_gap_adv_start();
        break;
    case ESP_HIDD_DISCONNECT_EVENT:
        esp_hid_ble_gap_adv_start();
        break;
    default:
        break;
    }
}

static void
ble_host_task(void *param) {
    (void) param;
    nimble_port_run(); // returns only after nimble_port_stop()
    nimble_port_freertos_deinit();
}

void
app_main(void) {
    s_out_mutex = xSemaphoreCreateMutex();

    esp_log_level_set("*", ESP_LOG_WARN);

    // Blocking stdin/stdout over the USB Serial/JTAG peripheral
    usb_serial_jtag_driver_config_t cfg = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(usb_serial_jtag_driver_install(&cfg));
    usb_serial_jtag_vfs_use_driver();
    setvbuf(stdin, NULL, _IONBF, 0);

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES
            || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(esp_hid_gap_init(HID_DEV_MODE));
    ESP_ERROR_CHECK(esp_hid_ble_gap_adv_init(ESP_HID_APPEARANCE_KEYBOARD,
                                             DEVICE_NAME));
    ESP_ERROR_CHECK(esp_hidd_dev_init(&hid_config, ESP_HID_TRANSPORT_BLE,
                                      hidd_event_callback, &s_hid_dev));

    ble_store_config_init();
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
    ret = esp_nimble_enable(ble_host_task);
    if (ret) {
        ESP_LOGE(TAG, "esp_nimble_enable failed: %d", ret);
    }

    // Bonds stored in NVS from previous runs are known (accepted) bonds
    vTaskDelay(pdMS_TO_TICKS(500));
    s_known_bonds = bond_count();

    xTaskCreate(serial_task, "serial", 4096, NULL, 5, NULL);
    out("EVT boot name=\"%s\" bonds=%d", DEVICE_NAME, s_known_bonds);
}
