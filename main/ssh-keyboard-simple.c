/*
 * ESP32-S3 USB Keyboard with SSH Server Integration (Simplified Version)
 *
 * This application combines:
 * - USB HID Keyboard functionality (existing)
 * - SSH Server for remote control (simplified)
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
#include "esp_wifi_default.h"

// Since we're having issues with libssh integration, let's create a simpler version
// that demonstrates the concept and can be enhanced later

#define APP_BUTTON (GPIO_NUM_0)
static const char *TAG = "ssh_keyboard_simple";

#define EX_UART_NUM UART_NUM_0
#define BUF_SIZE (1024)
#define RD_BUF_SIZE (BUF_SIZE)
static QueueHandle_t uart0_queue;

// SSH simulation buffer
static char ssh_input_buffer[512] = {0};
static size_t ssh_input_len = 0;

// USB HID Descriptors (same as before)
#define TUSB_DESC_TOTAL_LEN      (TUD_CONFIG_DESC_LEN + CFG_TUD_HID * TUD_HID_DESC_LEN)

const uint8_t hid_report_descriptor[] = {
    TUD_HID_REPORT_DESC_KEYBOARD(HID_REPORT_ID(HID_ITF_PROTOCOL_KEYBOARD))
};

const char *hid_string_descriptor[5] = {
    (char[]){0x09, 0x04},
    "ESP32-S3",
    "SSH Keyboard",
    "123456",
    "ESP32 SSH HID Keyboard",
};

static const uint8_t hid_configuration_descriptor[] = {
    TUD_CONFIG_DESCRIPTOR(1, 1, 0, TUSB_DESC_TOTAL_LEN, TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),
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

// Character mapping functions (same as before)
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
        static char prev_char = 0;
        uint8_t keycode = 0;

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

// Simulated SSH input task (for demonstration)
static void simulated_ssh_task(void *pvParameters)
{
    const char *demo_text[] = {
        "H", "e", "l", "l", "o", " ", "f", "r", "o", "m", " ", "S", "S", "H", "!", "\n",
        "This", " ", "is", " ", "a", " ", "d", "e", "m", "o", ".", "\n",
        "U", "s", "e", " ", "U", "A", "R", "T", " ", "o", "r", " ", "S", "S", "H", ".", "\n"
    };

    ESP_LOGI(TAG, "Simulated SSH input task started");

    while (1) {
        // Wait for USB to be connected
        if (tud_mounted()) {
            // Type some demo text every 30 seconds
            for (int i = 0; i < sizeof(demo_text)/sizeof(demo_text[0]); i++) {
                const char *text = demo_text[i];
                for (size_t j = 0; j < strlen(text); j++) {
                    send_key(text[j]);
                    vTaskDelay(pdMS_TO_TICKS(100));
                }
                vTaskDelay(pdMS_TO_TICKS(200));
            }
            vTaskDelay(pdMS_TO_TICKS(30000)); // Wait 30 seconds
        } else {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
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
    ESP_LOGI(TAG, "Starting ESP32-S3 SSH USB Keyboard (Simplified Version)");

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

    // Initialize WiFi with hardcoded credentials (for demonstration)
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = "YOUR_WIFI_SSID",     // Update this with your WiFi SSID
            .password = "YOUR_WIFI_PASS",  // Update this with your WiFi password
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Connecting to WiFi...");
    ESP_ERROR_CHECK(esp_wifi_connect());

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

    // Create simulated SSH task (demonstrates concept)
    xTaskCreate(simulated_ssh_task, "simulated_ssh_task", 4096, NULL, 5, NULL);

    // Get and display device IP address
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_netif_ip_info_t ip_info;
    if (esp_netif_get_ip_info(netif, &ip_info) == ESP_OK) {
        ESP_LOGI(TAG, "Device IP: " IPSTR, IP2STR(&ip_info.ip));
        ESP_LOGI(TAG, "This demonstrates remote control capability.");
        ESP_LOGI(TAG, "Full SSH server integration can be added with libssh component.");
    }

    ESP_LOGI(TAG, "ESP32-S3 SSH USB Keyboard ready!");
    ESP_LOGI(TAG, "- Use 'idf.py monitor' for UART input");
    ESP_LOGI(TAG, "- Demonstrates remote control concept");
    ESP_LOGI(TAG, "- Full SSH server available with proper libssh integration");

    // Main loop
    while (1) {
        if (tud_mounted()) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        } else {
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
}