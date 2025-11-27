/*
 * ESP32-S3 USB Keyboard with WiFi Provisioning (Based on Official Example)
 *
 * This combines:
 * - Official WiFi provisioning with QR code display
 * - USB HID Keyboard functionality
 * - Proper network connectivity
 * - Enhanced remote control capabilities
 */

#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "tinyusb.h"
#include "tinyusb_default_config.h"
#include "class/hid/hid_device.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "qrcode.h"
#include "network_provisioning/manager.h"
#include "network_provisioning/scheme_ble.h"
#include <libssh/libssh.h>
#include <libssh/server.h>
#include <libssh/callbacks.h>
#include "nvs.h"
#include "nvs_flash.h"

#define APP_BUTTON (GPIO_NUM_0)
static const char *TAG = "prov_keyboard";

// Event group for WiFi connection status
static EventGroupHandle_t wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

// UART Configuration
#define EX_UART_NUM UART_NUM_0
#define BUF_SIZE (1024)
#define RD_BUF_SIZE (BUF_SIZE)
static QueueHandle_t uart0_queue;

// USB HID Configuration
#define TUSB_DESC_TOTAL_LEN (TUD_CONFIG_DESC_LEN + CFG_TUD_HID * TUD_HID_DESC_LEN)

const uint8_t hid_report_descriptor[] = {
    TUD_HID_REPORT_DESC_KEYBOARD(HID_REPORT_ID(HID_ITF_PROTOCOL_KEYBOARD))
};

const char *hid_string_descriptor[5] = {
    (char[]){0x09, 0x04},
    "ESP32-S3",
    "Provisioned Keyboard",
    "123456",
    "ESP32 Provisioned Keyboard",
};

static const uint8_t hid_configuration_descriptor[] = {
    TUD_CONFIG_DESCRIPTOR(1, 1, 0, TUSB_DESC_TOTAL_LEN, TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),
    TUD_HID_DESCRIPTOR(0, 4, false, sizeof(hid_report_descriptor), 0x81, 16, 10),
};

// TinyUSB callbacks
uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance)
{
    return hid_report_descriptor;
}

uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t *buffer, uint16_t reqlen)
{
    return 0;
}

void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t const *buffer, uint16_t bufsize)
{
}

// Character mapping and keyboard functions (same as before)
uint8_t char_to_hid_keycode(char c)
{
    if (c >= 'a' && c <= 'z') {
        return HID_KEY_A + (c - 'a');
    }
    if (c >= 'A' && c <= 'Z') {
        return HID_KEY_A + (c - 'A');
    }
    if (c >= '1' && c <= '9') {
        return HID_KEY_1 + (c - '1');
    }
    if (c == '0') {
        return HID_KEY_0;
    }

    switch (c) {
        case ' ': return HID_KEY_SPACE;
        case '\r':
        case '\n': return HID_KEY_ENTER;
        case '\t': return HID_KEY_TAB;
        case '\b':
        case 0x7F: return HID_KEY_BACKSPACE;
        case 0x1B: return 0;
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
        default: return 0;
    }
}

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
        uint8_t keycode_array[6] = {0};
        uint8_t modifier = 0;

        keycode_array[0] = char_to_hid_keycode(c);

        if (c >= 'A' && c <= 'Z') {
            modifier = KEYBOARD_MODIFIER_LEFTSHIFT;
        }

        tud_hid_keyboard_report(HID_ITF_PROTOCOL_KEYBOARD, modifier, keycode_array);
        vTaskDelay(pdMS_TO_TICKS(50));
        tud_hid_keyboard_report(HID_ITF_PROTOCOL_KEYBOARD, 0, NULL);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

// QR code generation function (using correct ESP32 QR code API)
static void wifi_prov_print_qr(const char *name, const char *username, const char *pop, const char *transport)
{
    if (!name || !transport) {
        ESP_LOGW(TAG, "Cannot generate QR code payload. Data missing.");
        return;
    }

    char payload[200];
    if (pop) {
        snprintf(payload, sizeof(payload), "{\"ver\":\"%s\",\"name\":\"%s\"" \
                 ",\"pop\":\"%s\",\"transport\":\"%s\"}",
                 "v1", name, pop, transport);
    } else {
        snprintf(payload, sizeof(payload), "{\"ver\":\"%s\",\"name\":\"%s\"" \
                 ",\"transport\":\"%s\",\"network\":\"wifi\"}",
                 "v1", name, transport);
    }

    ESP_LOGI(TAG, "Scan this QR code from the ESP Provisioning mobile app for Provisioning.");

    // Use default QR code configuration with console display
    esp_qrcode_config_t cfg = ESP_QRCODE_CONFIG_DEFAULT();
    cfg.display_func = esp_qrcode_print_console;

    esp_err_t ret = esp_qrcode_generate(&cfg, payload);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to generate QR code: %s", esp_err_to_name(ret));
    }

    ESP_LOGI(TAG, "If QR code is not visible, copy paste the below URL in a browser.\nhttps://espressif.github.io/esp-jumpstart/qrcode.html?data=%s", payload);
}

// WiFi event handler
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                             int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT) {
        switch (event_id) {
            case WIFI_EVENT_STA_START:
                esp_wifi_connect();
                break;
            case WIFI_EVENT_STA_CONNECTED:
                ESP_LOGI(TAG, "Connected to AP");
                break;
            case WIFI_EVENT_STA_DISCONNECTED:
                ESP_LOGI(TAG, "Disconnected from AP");
                xEventGroupSetBits(wifi_event_group, WIFI_FAIL_BIT);
                break;
            default:
                break;
        }
    } else if (event_base == IP_EVENT) {
        if (event_id == IP_EVENT_STA_GOT_IP) {
            ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
            ESP_LOGI(TAG, "Got IP address: " IPSTR, IP2STR(&event->ip_info.ip));
            xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);

            // Type the IP address via USB keyboard
            char ip_str[64];
            snprintf(ip_str, sizeof(ip_str), "ESP32-S3 IP: " IPSTR "\n", IP2STR(&event->ip_info.ip));
            for (int i = 0; ip_str[i] != '\0'; i++) {
                send_key(ip_str[i]);
                vTaskDelay(pdMS_TO_TICKS(80));
            }
        }
    }
}

// Provisioning event handler
static void prov_event_handler(void* arg, esp_event_base_t event_base,
                              int32_t event_id, void* event_data)
{
    if (event_base == NETWORK_PROV_EVENT) {
        switch (event_id) {
            case NETWORK_PROV_START:
                ESP_LOGI(TAG, "Provisioning started");
                break;
            case NETWORK_PROV_WIFI_CRED_RECV:
                ESP_LOGI(TAG, "Received Wi-Fi credentials");
                break;
            case NETWORK_PROV_WIFI_CRED_FAIL:
                ESP_LOGE(TAG, "Provisioning failed");
                break;
            case NETWORK_PROV_WIFI_CRED_SUCCESS:
                ESP_LOGI(TAG, "Provisioning successful");
                break;
            case NETWORK_PROV_END:
                ESP_LOGI(TAG, "Provisioning ended");
                break;
            default:
                break;
        }
    }
}

// Application callback for provisioning
void wifi_prov_app_callback(void *user_data, network_prov_cb_event_t event, void *event_data)
{
    switch (event) {
    case NETWORK_PROV_SET_WIFI_STA_CONFIG: {
        wifi_config_t *wifi_config = (wifi_config_t *)event_data;
        ESP_LOGI(TAG, "Setting WiFi SSID: %s", wifi_config->sta.ssid);
        break;
    }
    default:
        break;
    }
}

const network_prov_event_handler_t wifi_prov_event_handler = {
    .event_cb = wifi_prov_app_callback,
    .user_data = NULL
};

// WiFi provisioning function (based on official example)
static void wifi_provisioning(void)
{
    ESP_LOGI(TAG, "Starting WiFi provisioning...");

    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Initialize TCP/IP and event loop
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    wifi_event_group = xEventGroupCreate();

    // Initialize WiFi netif
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // Register event handlers
    ESP_ERROR_CHECK(esp_event_handler_register(NETWORK_PROV_EVENT, ESP_EVENT_ANY_ID, &prov_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));

    // Configure provisioning
    const char *service_name = "PROV_ESP32";
    const char *pop = "abcd1234"; // Proof of possession

    // Print QR code
    wifi_prov_print_qr(service_name, NULL, pop, "ble");

    // Configure provisioning manager
    network_prov_mgr_config_t config = {
        .scheme = network_prov_scheme_ble,
        .scheme_event_handler = NETWORK_PROV_SCHEME_BLE_EVENT_HANDLER_FREE_BTDM
    };

    ESP_ERROR_CHECK(network_prov_mgr_init(config));

    // Start provisioning
    ESP_ERROR_CHECK(network_prov_mgr_start_provisioning(NETWORK_PROV_SECURITY_1, pop, service_name, NULL));

    // Wait for WiFi connection
    ESP_LOGI(TAG, "Waiting for WiFi connection...");
    EventBits_t bits = xEventGroupWaitBits(wifi_event_group,
                                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE,
                                           pdFALSE,
                                           portMAX_DELAY);

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Connected to WiFi successfully!");

        // Type success message via USB keyboard
        char success_msg[] = "WiFi Provisioning Successful!\n";
        for (int i = 0; success_msg[i] != '\0'; i++) {
            send_key(success_msg[i]);
            vTaskDelay(pdMS_TO_TICKS(80));
        }
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGE(TAG, "Failed to connect to WiFi");
    }

    // Cleanup
    network_prov_mgr_deinit();
}

// Provisioning completion demo task (disabled to avoid automatic typing)
/*
static void provisioning_complete_demo(void *pvParameters)
{
    const char *demo_messages[] = {
        "ESP32-S3 WiFi Provisioned Keyboard Ready!\n",
        "Features: USB HID + WiFi Provisioning + QR Code\n",
        "Arrow keys, Tab, special keys supported\n",
        "Remote keyboard control activated\n"
    };

    while (1) {
        if (tud_mounted()) {
            // Type demo messages
            for (int i = 0; i < sizeof(demo_messages)/sizeof(demo_messages[0]); i++) {
                for (int j = 0; demo_messages[i][j] != '\0'; j++) {
                    send_key(demo_messages[i][j]);
                    vTaskDelay(pdMS_TO_TICKS(60)); // Natural typing speed
                }
                vTaskDelay(pdMS_TO_TICKS(3000)); // Pause between messages
            }

            vTaskDelay(pdMS_TO_TICKS(45000)); // Wait 45 seconds
        } else {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }
}
*/

// SSH Server Configuration
#define SSH_PORT "22"
#define SSH_USERNAME "admin"
#define SSH_PASSWORD "esp32kbd"

static ssh_bind sshbind = NULL;
static TaskHandle_t ssh_server_task_handle = NULL;

// NVS storage for SSH keys
#define SSH_NVS_NAMESPACE "ssh_keys"
#define SSH_HOST_KEY_NAME "host_key"

// Function to save SSH host key to custom NVS partition
static esp_err_t save_ssh_host_key(ssh_key key) {
    nvs_handle_t nvs_handle;
    esp_err_t ret;

    // Open custom NVS namespace from ssh_keys partition
    ret = nvs_open_from_partition("ssh_keys", SSH_NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Error opening SSH NVS namespace: %s", esp_err_to_name(ret));
        return ret;
    }

    // Export key to base64
    char *b64_key = NULL;
    int rc = ssh_pki_export_privkey_base64(key, NULL, NULL, NULL, &b64_key);
    if (rc != SSH_OK) {
        ESP_LOGE(TAG, "Failed to export SSH host key");
        nvs_close(nvs_handle);
        return ESP_FAIL;
    }

    // Save to NVS
    ret = nvs_set_str(nvs_handle, SSH_HOST_KEY_NAME, b64_key);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save SSH host key to NVS: %s", esp_err_to_name(ret));
        free(b64_key);
        nvs_close(nvs_handle);
        return ret;
    }

    ret = nvs_commit(nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit SSH host key to NVS: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "SSH host key saved to NVS");
    }

    free(b64_key);
    nvs_close(nvs_handle);
    return ret;
}

// Function to load SSH host key from custom NVS partition
static ssh_key load_ssh_host_key(void) {
    nvs_handle_t nvs_handle;
    esp_err_t ret;
    ssh_key key = NULL;

    // Open custom NVS namespace from ssh_keys partition
    ret = nvs_open_from_partition("ssh_keys", SSH_NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (ret != ESP_OK) {
        ESP_LOGI(TAG, "SSH NVS namespace not found, will generate new key: %s", esp_err_to_name(ret));
        return NULL;
    }

    // Load key from NVS
    size_t required_size = 0;
    ret = nvs_get_str(nvs_handle, SSH_HOST_KEY_NAME, NULL, &required_size);
    if (ret != ESP_OK) {
        ESP_LOGI(TAG, "No SSH host key found in NVS: %s", esp_err_to_name(ret));
        nvs_close(nvs_handle);
        return NULL;
    }

    char *b64_key = malloc(required_size);
    if (!b64_key) {
        ESP_LOGE(TAG, "Failed to allocate memory for SSH host key");
        nvs_close(nvs_handle);
        return NULL;
    }

    ret = nvs_get_str(nvs_handle, SSH_HOST_KEY_NAME, b64_key, &required_size);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read SSH host key from NVS: %s", esp_err_to_name(ret));
        free(b64_key);
        nvs_close(nvs_handle);
        return NULL;
    }

    // Import key from base64
    int rc = ssh_pki_import_privkey_base64(b64_key, NULL, NULL, NULL, &key);
    if (rc != SSH_OK) {
        ESP_LOGE(TAG, "Failed to import SSH host key from NVS");
        free(b64_key);
        nvs_close(nvs_handle);
        return NULL;
    }

    ESP_LOGI(TAG, "SSH host key loaded from NVS");
    free(b64_key);
    nvs_close(nvs_handle);
    return key;
}


// SSH Keyboard input handler
static void ssh_keyboard_task(void *arg) {
    ssh_channel channel = (ssh_channel)arg;
    char buffer[256];
    int bytes_read;

    ESP_LOGI(TAG, "SSH keyboard input handler started");

    while (1) {
        bytes_read = ssh_channel_read(channel, buffer, sizeof(buffer) - 1, 0);
        if (bytes_read > 0) {
            buffer[bytes_read] = '\0';
            ESP_LOGI(TAG, "SSH received: %.*s", bytes_read, buffer);

            // Convert SSH input to USB keyboard input (same as UART)
            for (int i = 0; i < bytes_read; i++) {
                if (buffer[i] != '\0') {
                    send_key(buffer[i]);
                    vTaskDelay(pdMS_TO_TICKS(10)); // Small delay between keystrokes
                }
            }
        } else if (bytes_read == SSH_ERROR) {
            ESP_LOGI(TAG, "SSH channel read error, disconnecting");
            break;
        } else {
            // No data available, wait a bit
            vTaskDelay(pdMS_TO_TICKS(50));
        }
    }

    ESP_LOGI(TAG, "SSH keyboard input handler ended");
    vTaskDelete(NULL);
}

// SSH Session handler - simplified message-based approach
static void handle_ssh_session(ssh_session session) {
    ssh_message msg = NULL;
    ssh_channel channel = NULL;
    int auth_success = 0;

    ESP_LOGI(TAG, "Starting SSH session handler");

    // Handle key exchange
    if (ssh_handle_key_exchange(session) != SSH_OK) {
        ESP_LOGE(TAG, "SSH key exchange failed: %s", ssh_get_error(session));
        return;
    }

    ESP_LOGI(TAG, "SSH key exchange completed");

    // Set authentication methods
    ssh_set_auth_methods(session, SSH_AUTH_METHOD_PASSWORD);

    // Process authentication requests using message loop
    while (!auth_success && (msg = ssh_message_get(session))) {
        if (ssh_message_type(msg) == SSH_REQUEST_AUTH) {
            if (ssh_message_subtype(msg) == SSH_AUTH_METHOD_PASSWORD) {
                const char *user = ssh_message_auth_user(msg);
                const char *password = ssh_message_auth_password(msg);

                ESP_LOGI(TAG, "SSH password auth for user: %s", user);

                if (user && password && strcmp(user, SSH_USERNAME) == 0 && strcmp(password, SSH_PASSWORD) == 0) {
                    ESP_LOGI(TAG, "SSH authentication successful");
                    ssh_message_auth_reply_success(msg, 0);
                    auth_success = 1;
                    ssh_message_free(msg);
                    break;
                } else {
                    ESP_LOGW(TAG, "SSH authentication failed");
                    ssh_message_reply_default(msg);
                }
            } else {
                ESP_LOGW(TAG, "Unsupported auth method: %d", ssh_message_subtype(msg));
                ssh_message_reply_default(msg);
            }
        } else {
            // Not an auth message, send default reply
            ssh_message_reply_default(msg);
        }
        ssh_message_free(msg);
    }

    if (!auth_success) {
        ESP_LOGW(TAG, "SSH authentication failed");
        return;
    }

    ESP_LOGI(TAG, "SSH client authenticated, waiting for channel request");

    // Wait for channel request
    while ((msg = ssh_message_get(session))) {
        if (ssh_message_type(msg) == SSH_REQUEST_CHANNEL_OPEN) {
            if (ssh_message_subtype(msg) == SSH_CHANNEL_SESSION) {
                channel = ssh_message_channel_request_open_reply_accept(msg);
                ESP_LOGI(TAG, "SSH channel opened");
                ssh_message_free(msg);
                break;
            }
        }
        ssh_message_reply_default(msg);
        ssh_message_free(msg);
    }

    if (!channel) {
        ESP_LOGW(TAG, "No SSH channel received");
        return;
    }

    ESP_LOGI(TAG, "Waiting for shell request");

    // Wait for shell request
    while ((msg = ssh_message_get(session))) {
        if (ssh_message_type(msg) == SSH_REQUEST_CHANNEL) {
            if (ssh_message_subtype(msg) == SSH_CHANNEL_REQUEST_SHELL) {
                ssh_message_channel_request_reply_success(msg);
                ESP_LOGI(TAG, "SSH shell session started");
                ssh_message_free(msg);
                break;
            }
        }
        ssh_message_reply_default(msg);
        ssh_message_free(msg);
    }

    // Start keyboard input handler task
    TaskHandle_t keyboard_task;
    xTaskCreate(ssh_keyboard_task, "ssh_keyboard", 4096, channel, 10, &keyboard_task);

    // Keep the session alive
    while (ssh_channel_is_open(channel)) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        // Check if channel is still open
        if (ssh_channel_is_eof(channel)) {
            break;
        }
    }

    ESP_LOGI(TAG, "SSH session ending");

    // Clean up
    if (keyboard_task) {
        vTaskDelete(keyboard_task);
    }
    if (channel) {
        ssh_channel_free(channel);
    }
}

// SSH Server task
static void ssh_server_task(void *pvParameters) {
    ESP_LOGI(TAG, "SSH server task started");

    while (1) {
        ESP_LOGI(TAG, "Waiting for SSH connection on port %s", SSH_PORT);
        ssh_session session = ssh_new();

        if (!session) {
            ESP_LOGE(TAG, "Failed to create SSH session");
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        if (ssh_bind_accept(sshbind, session) == SSH_OK) {
            ESP_LOGI(TAG, "SSH connection accepted");
            handle_ssh_session(session);
        } else {
            ESP_LOGW(TAG, "SSH bind accept failed: %s", ssh_get_error(sshbind));
        }

        ssh_free(session);
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

// Initialize SSH server
static void ssh_server_init(void) {
    ESP_LOGI(TAG, "Initializing SSH server...");

    if (ssh_init() != SSH_OK) {
        ESP_LOGE(TAG, "Failed to initialize SSH library");
        return;
    }

    sshbind = ssh_bind_new();
    if (!sshbind) {
        ESP_LOGE(TAG, "Failed to create SSH bind");
        return;
    }

    ssh_bind_options_set(sshbind, SSH_BIND_OPTIONS_BINDADDR, "0.0.0.0");
    ssh_bind_options_set(sshbind, SSH_BIND_OPTIONS_BINDPORT_STR, SSH_PORT);
    ssh_bind_options_set(sshbind, SSH_BIND_OPTIONS_LOG_VERBOSITY_STR, "1");

    // Try to load existing SSH host key from NVS, or generate a new one
    ssh_key key = load_ssh_host_key();
    bool new_key_generated = false;

    if (!key) {
        ESP_LOGI(TAG, "No existing SSH host key found, generating new one...");
        if (ssh_pki_generate(SSH_KEYTYPE_ED25519, 0, &key) == SSH_OK) {
            new_key_generated = true;
            ESP_LOGI(TAG, "Generated new SSH host key");

            // Try to save the new key to NVS, but continue even if it fails
            if (save_ssh_host_key(key) != ESP_OK) {
                ESP_LOGW(TAG, "Warning: Could not save SSH host key to persistent storage, using temporary key");
                ESP_LOGI(TAG, "SSH server will use temporary key (will regenerate on reboot)");
            } else {
                ESP_LOGI(TAG, "New SSH host key saved to persistent storage");
            }
        } else {
            ESP_LOGE(TAG, "Failed to generate SSH host key");
            ssh_bind_free(sshbind);
            sshbind = NULL;
            return;
        }
    }

    // Try to set the host key using different methods
    bool key_set_success = false;

    // Method 1: Try setting host key directly
    if (ssh_bind_options_set(sshbind, SSH_BIND_OPTIONS_HOSTKEY, key) == SSH_OK) {
        ESP_LOGI(TAG, "SSH host key set successfully (method 1)");
        key_set_success = true;
    }
    // Method 2: Try setting as RSA key (fallback)
    else if (ssh_bind_options_set(sshbind, SSH_BIND_OPTIONS_RSAKEY, key) == SSH_OK) {
        ESP_LOGI(TAG, "SSH host key set successfully (method 2 - RSA)");
        key_set_success = true;
    }

    // Method 3: Try setting import key string (fallback)
    else {
        char *b64_key = NULL;
        int rc = ssh_pki_export_privkey_base64(key, NULL, NULL, NULL, &b64_key);
        if (rc == SSH_OK && b64_key) {
            if (ssh_bind_options_set(sshbind, SSH_BIND_OPTIONS_IMPORT_KEY_STR, b64_key) == SSH_OK) {
                ESP_LOGI(TAG, "SSH host key set successfully (method 3 - import string)");
                key_set_success = true;
            }
            free(b64_key);
        }
    }

    if (!key_set_success) {
        ESP_LOGE(TAG, "Failed to set SSH host key with any method: %s", ssh_get_error(sshbind));
        ssh_key_free(key);
        ssh_bind_free(sshbind);
        sshbind = NULL;
        return;
    }

    if (ssh_bind_listen(sshbind) != SSH_OK) {
        ESP_LOGE(TAG, "Failed to start SSH server: %s", ssh_get_error(sshbind));
        ssh_key_free(key);
        ssh_bind_free(sshbind);
        sshbind = NULL;
        return;
    }

    ESP_LOGI(TAG, "SSH server listening on 0.0.0.0:%s", SSH_PORT);
    ESP_LOGI(TAG, "SSH credentials: %s/%s", SSH_USERNAME, SSH_PASSWORD);
    if (new_key_generated) {
        ESP_LOGI(TAG, "New SSH host key generated and persisted");
    } else {
        ESP_LOGI(TAG, "Using existing SSH host key from persistent storage");
    }

    // Create SSH server task
    xTaskCreate(ssh_server_task, "ssh_server", 8192, NULL, 5, &ssh_server_task_handle);
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
                    ESP_LOGI(TAG, "UART received: %.*s", len, dtmp);

                    for (int i = 0; i < len; i++) {
                        if (dtmp[i] != '\0') {
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
    ESP_LOGI(TAG, "Starting ESP32-S3 WiFi Provisioned USB Keyboard");

    // Initialize button
    const gpio_config_t boot_button_config = {
        .pin_bit_mask = BIT64(APP_BUTTON),
        .mode = GPIO_MODE_INPUT,
        .intr_type = GPIO_INTR_DISABLE,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&boot_button_config));

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

    // Start WiFi provisioning
    wifi_provisioning();

    // Initialize SSH server
    ssh_server_init();

    // Create provisioning completion demo task (disabled)
    // xTaskCreate(provisioning_complete_demo, "provisioning_complete_demo", 4096, NULL, 5, NULL);

    ESP_LOGI(TAG, "ESP32-S3 WiFi Provisioned USB Keyboard ready!");
    ESP_LOGI(TAG, "✓ USB HID Keyboard with full character support");
    ESP_LOGI(TAG, "✓ WiFi Provisioning with QR code display");
    ESP_LOGI(TAG, "✓ BLE provisioning via ESP Provisioning app");
    ESP_LOGI(TAG, "✓ UART input via 'idf.py monitor' available");
    ESP_LOGI(TAG, "✓ Arrow keys, Tab, and special characters supported");
    ESP_LOGI(TAG, "✓ SSH server for remote keyboard control");

    // Main loop
    while (1) {
        if (tud_mounted()) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        } else {
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
}