/*
 * ESP32-S3 USB Keyboard with SSH Server Integration
 *
 * This application combines:
 * - USB HID Keyboard functionality (existing)
 * - SSH Server for remote control
 * - WiFi Provisioning for easy network setup
 * - Integration of SSH input with USB keyboard output
 */

#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"
#include "tinyusb.h"
#include "tinyusb_default_config.h"
#include "class/hid/hid_device.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "protocol_examples_common.h"
#include <libssh/libssh.h>
#include <libssh/server.h>
#include <libssh/callbacks.h>

// WiFi and SSH Configuration
#define APP_BUTTON (GPIO_NUM_0) // Use BOOT signal by default
static const char *TAG = "ssh_keyboard";

// UART Configuration
#define EX_UART_NUM UART_NUM_0
#define BUF_SIZE (1024)
#define RD_BUF_SIZE (BUF_SIZE)
static QueueHandle_t uart0_queue;

// SSH Server Configuration
#define SSH_PORT 22
#define SSH_MAX_CLIENTS 3
#define SSH_BUFFER_SIZE 1024
static ssh_session ssh_sessions[SSH_MAX_CLIENTS] = {NULL};
static ssh_channel ssh_channels[SSH_MAX_CLIENTS] = {NULL};
static TaskHandle_t ssh_server_task_handle = NULL;

// Escape sequence buffer for arrow keys and other special keys
static char escape_seq_buf[8] = {0};
static int escape_seq_idx = 0;

// USB HID Descriptors (same as before)
#define TUSB_DESC_TOTAL_LEN      (TUD_CONFIG_DESC_LEN + CFG_TUD_HID * TUD_HID_DESC_LEN)

// HID report descriptor for keyboard only
const uint8_t hid_report_descriptor[] = {
    TUD_HID_REPORT_DESC_KEYBOARD(HID_REPORT_ID(HID_ITF_PROTOCOL_KEYBOARD))
};

// String descriptor
const char *hid_string_descriptor[5] = {
    (char[]){0x09, 0x04},  // 0: is supported language is English (0x0409)
    "ESP32-S3",            // 1: Manufacturer
    "SSH Keyboard",        // 2: Product
    "123456",              // 3: Serials, should use chip ID
    "ESP32 SSH HID Keyboard", // 4: HID
};

// Configuration descriptor for keyboard only
static const uint8_t hid_configuration_descriptor[] = {
    // Configuration number, interface count, string index, total length, attribute, power in mA
    TUD_CONFIG_DESCRIPTOR(1, 1, 0, TUSB_DESC_TOTAL_LEN, TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),
    // Interface number, string index, boot protocol, report descriptor len, EP In address, size & polling interval
    TUD_HID_DESCRIPTOR(0, 4, false, sizeof(hid_report_descriptor), 0x81, 16, 10),
};

// TinyUSB HID callbacks (same as before)
uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance)
{
    return hid_report_descriptor;
}

uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t *buffer, uint16_t reqlen)
{
    (void) instance;
    (void) report_id;
    (void) report_type;
    (void) buffer;
    (void) reqlen;
    return 0;
}

void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t const *buffer, uint16_t bufsize)
{
}

// Character and keycode functions (same as before)
uint8_t char_to_hid_keycode(char c)
{
    // Handle lowercase letters
    if (c >= 'a' && c <= 'z') {
        return HID_KEY_A + (c - 'a');
    }
    // Handle uppercase letters
    if (c >= 'A' && c <= 'Z') {
        return HID_KEY_A + (c - 'A');
    }
    // Handle numbers
    if (c >= '1' && c <= '9') {
        return HID_KEY_1 + (c - '1');
    }
    if (c == '0') {
        return HID_KEY_0;
    }

    // Handle special characters and control codes
    switch (c) {
        case ' ': return HID_KEY_SPACE;
        case '\r':
        case '\n': return HID_KEY_ENTER;
        case '\t': return HID_KEY_TAB;   // Tab key (same as 0x09)
        case '\b':
        case 0x7F: return HID_KEY_BACKSPACE;  // Backspace and Delete
        case 0x1B: return 0;  // ESC - let escape sequence handler deal with this
        case '!': return HID_KEY_1;
        case '@': return HID_KEY_2;
        case '#': return HID_KEY_3;
        case '$': return HID_KEY_4;
        case '%': return HID_KEY_5;
        case '^': return HID_KEY_6;
        case '&': return HID_KEY_7;
        case '*': return HID_KEY_8;
        case '(': return HID_KEY_9;
        case ')': return HID_KEY_0;
        case '-': return HID_KEY_MINUS;
        case '=': return HID_KEY_EQUAL;
        case '[': return HID_KEY_BRACKET_LEFT;
        case ']': return HID_KEY_BRACKET_RIGHT;
        case '\\': return HID_KEY_BACKSLASH;
        case ';': return HID_KEY_SEMICOLON;
        case '\'': return HID_KEY_APOSTROPHE;
        case '`': return HID_KEY_GRAVE;
        case ',': return HID_KEY_COMMA;
        case '.': return HID_KEY_PERIOD;
        case '/': return HID_KEY_SLASH;
        default: return 0; // Unknown character
    }
}

// SSH authentication callbacks
static int auth_none(ssh_session session, const char *user, void *userdata)
{
    ESP_LOGI(TAG, "SSH auth none requested for user: %s", user);
    ssh_set_auth_methods(session, SSH_AUTH_METHOD_PASSWORD | SSH_AUTH_METHOD_PUBLICKEY);
    return SSH_AUTH_DENIED;
}

static int auth_password(ssh_session session, const char *user, const char *password, void *userdata)
{
    ESP_LOGI(TAG, "SSH password auth attempt for user: %s", user);

    // Simple authentication - in production, use proper authentication
    if (strcmp(user, "esp32") == 0 && strcmp(password, "keyboard") == 0) {
        ESP_LOGI(TAG, "SSH authentication successful for user: %s", user);
        return SSH_AUTH_SUCCESS;
    }

    return SSH_AUTH_DENIED;
}

static int shell_request(ssh_session session, ssh_channel channel, void *userdata)
{
    ESP_LOGI(TAG, "SSH shell requested");
    return SSH_OK;
}

static int pty_request(ssh_session session, ssh_channel channel,
                      const char *term, int cols, int rows,
                      int py, int px, void *userdata)
{
    ESP_LOGI(TAG, "SSH PTY requested: %s (%dx%d)", term, cols, rows);
    return SSH_OK;
}

static int channel_data_callback(ssh_session session, ssh_channel channel,
                                void *data, uint32_t len, int is_stderr, void *userdata)
{
    char *input_data = (char*)data;

    ESP_LOGI(TAG, "SSH received %d bytes", len);

    // Process SSH input and send as USB keyboard input
    for (uint32_t i = 0; i < len; i++) {
        // Use the same character processing as UART
        if (input_data[i] == '\0') continue;

        // Check for escape sequences
        if (input_data[i] == '\x1b') {
            escape_seq_idx = 0;
            escape_seq_buf[escape_seq_idx++] = input_data[i];
        } else if (escape_seq_idx > 0) {
            escape_seq_buf[escape_seq_idx++] = input_data[i];

            // Simple arrow sequence detection (for basic terminal navigation)
            if (escape_seq_idx >= 3 && escape_seq_buf[0] == '\x1b' && escape_seq_buf[1] == '[') {
                switch (escape_seq_buf[2]) {
                    case 'A': send_keycode(HID_KEY_ARROW_UP); break;
                    case 'B': send_keycode(HID_KEY_ARROW_DOWN); break;
                    case 'C': send_keycode(HID_KEY_ARROW_RIGHT); break;
                    case 'D': send_keycode(HID_KEY_ARROW_LEFT); break;
                    default: send_key(input_data[i]); break;
                }
                escape_seq_idx = 0;
                memset(escape_seq_buf, 0, sizeof(escape_seq_buf));
            }
        } else {
            send_key(input_data[i]);
        }
    }

    return len;
}

struct ssh_channel_callbacks_struct channel_cb = {
    .userdata = NULL,
    .channel_pty_request_function = pty_request,
    .channel_shell_request_function = shell_request,
    .channel_data_function = channel_data_callback,
};

// SSH Server Task
static void ssh_server_task(void *pvParameters)
{
    ssh_bind sshbind;
    int rc;

    // Initialize SSH bind
    sshbind = ssh_bind_new();
    if (sshbind == NULL) {
        ESP_LOGE(TAG, "Failed to create SSH bind");
        return;
    }

    // Set SSH bind options
    ssh_bind_options_set(sshbind, SSH_BIND_OPTIONS_BINDPORT, &SSH_PORT);
    ssh_bind_options_set(sshbind, SSH_BIND_OPTIONS_BINDADDR, "0.0.0.0");
    ssh_bind_options_set(sshbind, SSH_BIND_OPTIONS_HOSTKEY, "/spiffs/ssh_host_ed25519_key");

    // Load host key (you need to provision this)
    ssh_key privkey;
    rc = ssh_pki_import_privkey_base64("-----BEGIN OPENSSH PRIVATE KEY-----\n"
                                       "b3BlbnNzaC1rZXktdjEAAAAABG5vbmUAAAAEbm9uZQAAAAAAAAABAAAAlwAAAAdzc2gtcn\n"
                                       "NhAAAAAwEAAQAAAIEA2HfzZ+xyT4vKk4O8eO8xQsQcO8xQsQcO8xQsQcO8xQsQcO8xQsQcO8\n"
                                       "xQsQcO8xQsQcO8xQsQcO8xQsQcO8xQsQcO8xQsQcO8xQsQcO8xQsQcO8xQsQcO8xQsQcO8xQsQcO8\n"
                                       "xQsQcO8xQsQcO8xQsQcO8xQsQcO8xQsQcO8xQsQcO8xQsQcO8xQsQcO8xQsQcO8xQsQcO8xQsQcO8\n"
                                       "-----END OPENSSH PRIVATE KEY-----",
                                       NULL, NULL, &privkey);

    if (rc != SSH_OK) {
        ESP_LOGE(TAG, "Failed to load SSH host key");
        ssh_bind_free(sshbind);
        return;
    }

    ssh_bind_options_set(sshbind, SSH_BIND_OPTIONS_HOSTKEY, privkey);

    // Listen for connections
    rc = ssh_bind_listen(sshbind);
    if (rc != SSH_OK) {
        ESP_LOGE(TAG, "Error listening on SSH port: %s", ssh_get_error(sshbind));
        ssh_bind_free(sshbind);
        return;
    }

    ESP_LOGI(TAG, "SSH server listening on port %d", SSH_PORT);

    // Accept connections
    while (1) {
        for (int i = 0; i < SSH_MAX_CLIENTS; i++) {
            if (ssh_sessions[i] == NULL) {
                ssh_sessions[i] = ssh_new();
                if (ssh_sessions[i] == NULL) {
                    ESP_LOGE(TAG, "Failed to create SSH session");
                    continue;
                }

                rc = ssh_bind_accept(sshbind, ssh_sessions[i]);
                if (rc == SSH_OK) {
                    ESP_LOGI(TAG, "SSH client connected on slot %d", i);

                    // Set authentication callbacks
                    ssh_set_auth_methods(ssh_sessions[i], SSH_AUTH_METHOD_PASSWORD | SSH_AUTH_METHOD_PUBLICKEY);
                    ssh_server_set_auth_callback(ssh_sessions[i], auth_password);

                    // Handle authentication
                    if (ssh_handle_key_exchange(ssh_sessions[i]) != SSH_OK) {
                        ESP_LOGE(TAG, "SSH key exchange failed");
                        ssh_free(ssh_sessions[i]);
                        ssh_sessions[i] = NULL;
                        continue;
                    }

                    // Create channel
                    ssh_channels[i] = ssh_channel_new(ssh_sessions[i]);
                    if (ssh_channels[i] == NULL) {
                        ESP_LOGE(TAG, "Failed to create SSH channel");
                        ssh_free(ssh_sessions[i]);
                        ssh_sessions[i] = NULL;
                        continue;
                    }

                    ssh_set_channel_callbacks(ssh_channels[i], &channel_cb);

                    if (ssh_channel_open_session(ssh_channels[i]) != SSH_OK) {
                        ESP_LOGE(TAG, "Failed to open SSH channel");
                        ssh_channel_free(ssh_channels[i]);
                        ssh_free(ssh_sessions[i]);
                        ssh_sessions[i] = NULL;
                        ssh_channels[i] = NULL;
                        continue;
                    }

                    // Request shell
                    if (ssh_channel_request_shell(ssh_channels[i]) != SSH_OK) {
                        ESP_LOGE(TAG, "Failed to request shell");
                        ssh_channel_free(ssh_channels[i]);
                        ssh_free(ssh_sessions[i]);
                        ssh_sessions[i] = NULL;
                        ssh_channels[i] = NULL;
                        continue;
                    }

                    ESP_LOGI(TAG, "SSH session established for slot %d", i);
                } else {
                    ESP_LOGE(TAG, "SSH accept failed: %s", ssh_get_error(sshbind));
                    ssh_free(ssh_sessions[i]);
                    ssh_sessions[i] = NULL;
                }
                break;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

// Helper functions (same as before)
void send_keycode(uint8_t keycode)
{
    if (tud_mounted()) {
        uint8_t keycode_array[6] = {0};
        keycode_array[0] = keycode;

        tud_hid_keyboard_report(HID_ITF_PROTOCOL_KEYBOARD, 0, keycode_array);
        vTaskDelay(pdMS_TO_TICKS(50));
        tud_hid_keyboard_report(HID_ITF_PROTOCOL_KEYBOARD, 0, NULL);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void send_key(char c)
{
    if (tud_mounted()) {
        static char prev_char = 0;
        uint8_t keycode = 0;

        // Handle arrow key sequences from idf.py monitor
        if (prev_char == '[') {
            switch (c) {
                case 'A': keycode = HID_KEY_ARROW_UP; break;
                case 'B': keycode = HID_KEY_ARROW_DOWN; break;
                case 'C': keycode = HID_KEY_ARROW_RIGHT; break;
                case 'D': keycode = HID_KEY_ARROW_LEFT; break;
                case 'H': keycode = HID_KEY_HOME; break;
                case 'F': keycode = HID_KEY_END; break;
            }

            if (keycode != 0) {
                prev_char = 0;
                send_keycode(keycode);
                return;
            }
        }

        if (prev_char == '[') {
            uint8_t keycode_array[6] = {0};
            keycode_array[0] = char_to_hid_keycode('[');
            tud_hid_keyboard_report(HID_ITF_PROTOCOL_KEYBOARD, 0, keycode_array);
            vTaskDelay(pdMS_TO_TICKS(50));
            tud_hid_keyboard_report(HID_ITF_PROTOCOL_KEYBOARD, 0, NULL);
            vTaskDelay(pdMS_TO_TICKS(10));
            prev_char = 0;
        }

        if (c == '[') {
            prev_char = c;
            return;
        }

        // Regular character processing
        uint8_t keycode_array[6] = {0};
        uint8_t modifier = 0;

        keycode_array[0] = char_to_hid_keycode(c);

        // Set modifier for shift key
        if (c >= 'A' && c <= 'Z') {
            modifier = KEYBOARD_MODIFIER_LEFTSHIFT;
        }

        tud_hid_keyboard_report(HID_ITF_PROTOCOL_KEYBOARD, modifier, keycode_array);
        vTaskDelay(pdMS_TO_TICKS(50));
        tud_hid_keyboard_report(HID_ITF_PROTOCOL_KEYBOARD, 0, NULL);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

// UART event handler task (same as before)
static void uart_event_task(void *pvParameters)
{
    uart_event_t event;
    uint8_t* dtmp = (uint8_t*) malloc(RD_BUF_SIZE);
    assert(dtmp);

    for (;;) {
        if (xQueueReceive(uart0_queue, (void *)&event, (TickType_t)portMAX_DELAY)) {
            bzero(dtmp, RD_BUF_SIZE);
            switch (event.type) {
            case UART_DATA:
                int len = uart_read_bytes(EX_UART_NUM, dtmp, event.size, portMAX_DELAY);
                if (len > 0) {
                    char hex_str[64] = {0};
                    int hex_idx = 0;
                    for (int i = 0; i < len && hex_idx < sizeof(hex_str) - 3; i++) {
                        hex_idx += snprintf(hex_str + hex_idx, sizeof(hex_str) - hex_idx,
                                         "%02x ", (unsigned char)dtmp[i]);
                    }
                    ESP_LOGI(TAG, "UART received: %s", hex_str);

                    for (int i = 0; i < len; i++) {
                        if (dtmp[i] == '\0') continue;

                        if (dtmp[i] == '\x1b') {
                            escape_seq_idx = 0;
                            escape_seq_buf[escape_seq_idx++] = dtmp[i];
                        } else if (escape_seq_idx > 0) {
                            escape_seq_buf[escape_seq_idx++] = dtmp[i];

                            if (escape_seq_idx >= 2) {
                                uint8_t keycode = 0;
                                if (escape_seq_buf[0] == '\x1b' && escape_seq_buf[1] == '[') {
                                    switch (escape_seq_buf[2]) {
                                        case 'A': keycode = HID_KEY_ARROW_UP; break;
                                        case 'B': keycode = HID_KEY_ARROW_DOWN; break;
                                        case 'C': keycode = HID_KEY_ARROW_RIGHT; break;
                                        case 'D': keycode = HID_KEY_ARROW_LEFT; break;
                                    }
                                }

                                if (keycode != 0) {
                                    send_keycode(keycode);
                                    escape_seq_idx = 0;
                                    memset(escape_seq_buf, 0, sizeof(escape_seq_buf));
                                } else if (escape_seq_idx >= 7) {
                                    escape_seq_idx = 0;
                                    memset(escape_seq_buf, 0, sizeof(escape_seq_buf));
                                }
                            }
                        } else {
                            send_key(dtmp[i]);
                        }
                    }
                }
                break;

            case UART_FIFO_OVF:
            case UART_BUFFER_FULL:
                ESP_LOGW(TAG, "UART buffer overflow");
                uart_flush_input(EX_UART_NUM);
                xQueueReset(uart0_queue);
                break;

            default:
                break;
            }
        }
    }
    free(dtmp);
    vTaskDelete(NULL);
}

void app_main(void)
{
    ESP_LOGI(TAG, "Starting ESP32-S3 SSH USB Keyboard");

    // Initialize button
    const gpio_config_t boot_button_config = {
        .pin_bit_mask = BIT64(APP_BUTTON),
        .mode = GPIO_MODE_INPUT,
        .intr_type = GPIO_INTR_DISABLE,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&boot_button_config));

    // Initialize NVS
    ESP_ERROR_CHECK(nvs_flash_init());

    // Initialize WiFi
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Initialize WiFi using protocol_examples_common
    ESP_ERROR_CHECK(example_connect());

    // Configure UART
    uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    ESP_ERROR_CHECK(uart_driver_install(EX_UART_NUM, BUF_SIZE * 2, BUF_SIZE * 2, 20, &uart0_queue, 0));
    ESP_ERROR_CHECK(uart_param_config(EX_UART_NUM, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(EX_UART_NUM, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));

    // Create UART event handler task
    xTaskCreate(uart_event_task, "uart_event_task", 4096, NULL, 12, NULL);

    // Initialize USB
    ESP_LOGI(TAG, "Initializing USB");
    tinyusb_config_t tusb_cfg = TINYUSB_DEFAULT_CONFIG();

    tusb_cfg.descriptor.device = NULL;
    tusb_cfg.descriptor.full_speed_config = hid_configuration_descriptor;
    tusb_cfg.descriptor.string = hid_string_descriptor;
    tusb_cfg.descriptor.string_count = sizeof(hid_string_descriptor) / sizeof(hid_string_descriptor[0]);
#if (TUD_OPT_HIGH_SPEED)
    tusb_cfg.descriptor.high_speed_config = hid_configuration_descriptor;
#endif // TUD_OPT_HIGH_SPEED

    ESP_ERROR_CHECK(tinyusb_driver_install(&tusb_cfg));
    ESP_LOGI(TAG, "USB initialization DONE");

    // Create SSH server task
    xTaskCreate(ssh_server_task, "ssh_server_task", 8192, NULL, 5, &ssh_server_task_handle);

    // Get and display device IP address
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_netif_ip_info_t ip_info;
    if (esp_netif_get_ip_info(netif, &ip_info) == ESP_OK) {
        ESP_LOGI(TAG, "Device IP: " IPSTR, IP2STR(&ip_info.ip));
        ESP_LOGI(TAG, "SSH available at: ssh esp32@" IPSTR, IP2STR(&ip_info.ip));
        ESP_LOGI(TAG, "Password: keyboard");
    }

    ESP_LOGI(TAG, "ESP32-S3 SSH USB Keyboard ready!");
    ESP_LOGI(TAG, "- Use 'idf.py monitor' for UART input");
    ESP_LOGI(TAG, "- Use SSH for remote control");

    // Main loop
    while (1) {
        if (tud_mounted()) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        } else {
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
}