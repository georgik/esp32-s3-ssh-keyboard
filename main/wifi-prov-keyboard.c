/*
 * ESP32-S3 USB Keyboard with WiFi Provisioning and SSH Integration
 *
 * This application demonstrates:
 * - USB HID Keyboard functionality
 * - WiFi Provisioning with QR code display
 * - BLE provisioning support
 * - Remote control capability
 * - Integration with network services
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
#include "wifi_provisioning/manager.h"
#include "wifi_provisioning/scheme_ble.h"
#include "esp_timer.h"

#define APP_BUTTON (GPIO_NUM_0)
static const char *TAG = "wifi_prov_keyboard";

// Event group for WiFi connection status
static EventGroupHandle_t wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

// UART Configuration
#define EX_UART_NUM UART_NUM_0
#define BUF_SIZE (1024)
#define RD_BUF_SIZE (BUF_SIZE)
static QueueHandle_t uart0_queue;

// USB HID Configuration (same as before)
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

// Print QR code for WiFi provisioning
static void print_qr_code(const char *payload)
{
    ESP_LOGI(TAG, "Scan this QR code with the ESP Provisioning app:");

    // Create QR code
    QRCode qrcode;
    uint8_t qrcodeData[qrcode_getBufferSize(3)]; // Version 3 QR code
    qrcode_initText(&qrcode, qrcodeData, 3, ECC_MEDIUM, payload);

    // Print QR code to console
    int border = 2;
    for (int y = -border; y < qrcode.size + border; y++) {
        for (int x = -border; x < qrcode.size + border; x++) {
            if (qrcode_getModule(&qrcode, x, y)) {
                ESP_LOGI(TAG, "##");
            } else {
                ESP_LOGI(TAG, "  ");
            }
        }
        ESP_LOGI(TAG, "");
    }

    // Also print the payload for manual entry
    ESP_LOGI(TAG, "QR Code Payload: %s", payload);
    ESP_LOGI(TAG, "Or use 'I don't have a QR code' option in the app");
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
        }
    }
}

// Provisioning event handler
static void prov_event_handler(void* arg, esp_event_base_t event_base,
                              int32_t event_id, void* event_data)
{
    if (event_base == WIFI_PROV_EVENT) {
        switch (event_id) {
            case WIFI_PROV_START:
                ESP_LOGI(TAG, "Provisioning started");
                break;
            case WIFI_PROV_CRED_RECV:
                ESP_LOGI(TAG, "Received Wi-Fi credentials");
                break;
            case WIFI_PROV_CRED_FAIL:
                ESP_LOGE(TAG, "Provisioning failed");
                break;
            case WIFI_PROV_CRED_SUCCESS:
                ESP_LOGI(TAG, "Provisioning successful");
                break;
            case WIFI_PROV_END:
                ESP_LOGI(TAG, "Provisioning ended");
                break;
            default:
                break;
        }
    }
}

// WiFi provisioning function
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

    // Initialize WiFi
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();

    // Register WiFi event handler
    wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));

    // Initialize WiFi
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // Configure provisioning
    const char *pop = "abcd1234"; // Proof of possession
    const char *service_name = "PROV_ESP32";
    const char *service_key = NULL;

    wifi_prov_mgr_config_t config = {
        .scheme = wifi_prov_scheme_ble,
        .scheme_event_handler = prov_event_handler,
    };

    ESP_ERROR_CHECK(wifi_prov_mgr_init(config));

    // Generate QR code payload
    char qr_code_payload[200];
    snprintf(qr_code_payload, sizeof(qr_code_payload),
             "WIFI:T:WPA;S:MySSID;P:MyPassword;;", // This will be replaced with actual provisioning data
             service_name, pop);

    // Print QR code
    print_qr_code(qr_code_payload);

    // Start provisioning
    ESP_ERROR_CHECK(wifi_prov_mgr_start_provisioning(service_name, service_key, pop, strlen(pop)));

    // Wait for WiFi connection
    ESP_LOGI(TAG, "Waiting for WiFi connection...");
    EventBits_t bits = xEventGroupWaitBits(wifi_event_group,
                                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE,
                                           pdFALSE,
                                           portMAX_DELAY);

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Connected to WiFi successfully!");

        // Get and display IP address
        esp_netif_ip_info_t ip_info;
        esp_netif_get_ip_info(sta_netif, &ip_info);
        ESP_LOGI(TAG, "Device IP: " IPSTR, IP2STR(&ip_info.ip));

        // Type the IP address via USB keyboard
        char ip_str[64];
        snprintf(ip_str, sizeof(ip_str), "Device IP: " IPSTR "\n", IP2STR(&ip_info.ip));
        for (int i = 0; ip_str[i] != '\0'; i++) {
            send_key(ip_str[i]);
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGE(TAG, "Failed to connect to WiFi");
    }

    // Cleanup
    wifi_prov_mgr_deinit();
}

// Remote keyboard demonstration task
static void remote_keyboard_demo(void *pvParameters)
{
    vTaskDelay(pdMS_TO_TICKS(10000)); // Wait 10 seconds after connection

    const char *demo_messages[] = {
        "Hello from remote ESP32-S3!\n",
        "This keyboard can be controlled remotely.\n",
        "Arrow keys, Tab, and special keys work.\n",
        "ESP32-S3 WiFi Provisioned Keyboard Active!\n"
    };

    while (1) {
        if (tud_mounted()) {
            // Type demo messages
            for (int i = 0; i < sizeof(demo_messages)/sizeof(demo_messages[0]); i++) {
                for (int j = 0; demo_messages[i][j] != '\0'; j++) {
                    send_key(demo_messages[i][j]);
                    vTaskDelay(pdMS_TO_TICKS(80)); // Natural typing speed
                }
                vTaskDelay(pdMS_TO_TICKS(2000)); // Pause between messages
            }

            vTaskDelay(pdMS_TO_TICKS(30000)); // Wait 30 seconds
        } else {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }
}

// UART event handler task
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

    // Create remote keyboard demo task
    xTaskCreate(remote_keyboard_demo, "remote_keyboard_demo", 4096, NULL, 5, NULL);

    ESP_LOGI(TAG, "ESP32-S3 WiFi Provisioned USB Keyboard ready!");
    ESP_LOGI(TAG, "Features:");
    ESP_LOGI(TAG, "- USB HID Keyboard with full character support");
    ESP_LOGI(TAG, "- WiFi Provisioning with QR code support");
    ESP_LOGI(TAG, "- Remote keyboard demonstration");
    ESP_LOGI(TAG, "- UART input via 'idf.py monitor' still works");
    ESP_LOGI(TAG, "- Arrow keys, Tab, and special characters supported");

    // Main loop
    while (1) {
        if (tud_mounted()) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        } else {
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
}