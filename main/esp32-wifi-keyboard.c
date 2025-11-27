#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "tinyusb.h"
#include "tinyusb_default_config.h"
#include "class/hid/hid_device.h"
#include "driver/uart.h"
#include "driver/gpio.h"

#define APP_BUTTON (GPIO_NUM_0) // Use BOOT signal by default
static const char *TAG = "esp32_keyboard";

#define EX_UART_NUM UART_NUM_0
#define BUF_SIZE (1024)
#define RD_BUF_SIZE (BUF_SIZE)
static QueueHandle_t uart0_queue;

// Escape sequence buffer for arrow keys and other special keys
static char escape_seq_buf[8] = {0};
static int escape_seq_idx = 0;

// USB HID Descriptors
#define TUSB_DESC_TOTAL_LEN      (TUD_CONFIG_DESC_LEN + CFG_TUD_HID * TUD_HID_DESC_LEN)

// HID report descriptor for keyboard only
const uint8_t hid_report_descriptor[] = {
    TUD_HID_REPORT_DESC_KEYBOARD(HID_REPORT_ID(HID_ITF_PROTOCOL_KEYBOARD))
};

// String descriptor
const char *hid_string_descriptor[5] = {
    (char[]){0x09, 0x04},  // 0: is supported language is English (0x0409)
    "ESP32",               // 1: Manufacturer
    "ESP32 Keyboard",      // 2: Product
    "123456",              // 3: Serials, should use chip ID
    "ESP32 HID Keyboard",  // 4: HID
};

// Configuration descriptor for keyboard only
static const uint8_t hid_configuration_descriptor[] = {
    // Configuration number, interface count, string index, total length, attribute, power in mA
    TUD_CONFIG_DESCRIPTOR(1, 1, 0, TUSB_DESC_TOTAL_LEN, TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),

    // Interface number, string index, boot protocol, report descriptor len, EP In address, size & polling interval
    TUD_HID_DESCRIPTOR(0, 4, false, sizeof(hid_report_descriptor), 0x81, 16, 10),
};

// TinyUSB HID callbacks
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

// Character to HID keycode mapping function
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
        // Additional control character that might be sent specifically for Tab
        // The Tab key is already handled above with '\t' and 0x09
        default: return 0; // Unknown character
    }
}

// Process escape sequences and return HID keycode
uint8_t process_escape_sequence(const char *seq, int len)
{
    if (len >= 3 && seq[0] == '\x1b' && seq[1] == '[') {
        // ANSI escape sequences
        switch (seq[2]) {
            case 'A': return HID_KEY_ARROW_UP;
            case 'B': return HID_KEY_ARROW_DOWN;
            case 'C': return HID_KEY_ARROW_RIGHT;
            case 'D': return HID_KEY_ARROW_LEFT;
            case 'H': return HID_KEY_HOME;      // Home key
            case 'F': return HID_KEY_END;        // End key
        }

        // Extended escape sequences
        if (len >= 4) {
            switch (seq[3]) {
                case '~':
                    switch (seq[2]) {
                        case '1': return HID_KEY_HOME;      // Home (ESC [1~)
                        case '2': return HID_KEY_INSERT;    // Insert (ESC [2~)
                        case '3': return HID_KEY_DELETE;    // Delete (ESC [3~)
                        case '4': return HID_KEY_END;       // End (ESC [4~)
                        case '5': return HID_KEY_PAGE_UP;   // Page Up (ESC [5~)
                        case '6': return HID_KEY_PAGE_DOWN; // Page Down (ESC [6~)
                    }
                    break;
                case '7': return HID_KEY_HOME;   // Home (ESC [1~7)
                case '8': return HID_KEY_END;     // End (ESC [1~8)
            }
        }
    }

    return 0; // Unknown escape sequence
}

// Check if character could be start of escape sequence
bool is_escape_start(char c)
{
    return c == '\x1b'; // ESC character
}

// Check if character needs shift key
bool char_needs_shift(char c)
{
    if (c >= 'A' && c <= 'Z') return true;
    if (c >= '1' && c <= '9') return false;

    switch (c) {
        case '!': case '@': case '#': case '$': case '%': case '^': case '&': case '*': case '(': case ')':
        case '_': case '+': case '{': case '}': case '|': case ':': case '"': case '<': case '>': case '?':
            return true;
        default:
            return false;
    }
}

// Send HID keycode (for special keys that don't need character mapping)
void send_keycode(uint8_t keycode)
{
    if (tud_mounted()) {
        uint8_t keycode_array[6] = {0};
        keycode_array[0] = keycode;

        // Send key press
        tud_hid_keyboard_report(HID_ITF_PROTOCOL_KEYBOARD, 0, keycode_array);
        vTaskDelay(pdMS_TO_TICKS(50));

        // Send key release
        tud_hid_keyboard_report(HID_ITF_PROTOCOL_KEYBOARD, 0, NULL);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

// Enhanced send_key function that handles arrow keys from idf.py monitor
void send_key(char c)
{
    if (tud_mounted()) {
        // Check if this character is part of an arrow key sequence from idf.py monitor
        // The monitor sends sequences like: [A for up, [B for down, [C for right, [D for left

        static char prev_char = 0;
        uint8_t keycode = 0;

        // Handle arrow key sequences from idf.py monitor
        if (prev_char == '[') {
            switch (c) {
                case 'A': keycode = HID_KEY_ARROW_UP; break;    // Up arrow
                case 'B': keycode = HID_KEY_ARROW_DOWN; break;  // Down arrow
                case 'C': keycode = HID_KEY_ARROW_RIGHT; break; // Right arrow
                case 'D': keycode = HID_KEY_ARROW_LEFT; break;  // Left arrow
                case 'H': keycode = HID_KEY_HOME; break;       // Home
                case 'F': keycode = HID_KEY_END; break;        // End
                case '1': case '2': case '3': case '4': case '5': case '6':
                    // These are part of extended escape sequences like [1~, [2~, etc.
                    // Let the escape sequence handler deal with these
                    prev_char = 0;
                    return;
            }

            if (keycode != 0) {
                prev_char = 0; // Reset previous character
                send_keycode(keycode);
                return;
            }
        }

        // If we have a [ but it's not followed by a valid arrow sequence, treat it as normal
        if (prev_char == '[') {
            // Send the [ character first - send it directly without recursion
            uint8_t keycode_array[6] = {0};
            keycode_array[0] = char_to_hid_keycode('[');
            tud_hid_keyboard_report(HID_ITF_PROTOCOL_KEYBOARD, 0, keycode_array);
            vTaskDelay(pdMS_TO_TICKS(50));
            tud_hid_keyboard_report(HID_ITF_PROTOCOL_KEYBOARD, 0, NULL);
            vTaskDelay(pdMS_TO_TICKS(10));
            prev_char = 0;
        }

        // Check for start of arrow sequence
        if (c == '[') {
            prev_char = c;
            return;
        }

        // Regular character processing
        uint8_t keycode_array[6] = {0};
        uint8_t modifier = 0;

        keycode_array[0] = char_to_hid_keycode(c);

        // Set modifier for shift key
        if (char_needs_shift(c)) {
            modifier = KEYBOARD_MODIFIER_LEFTSHIFT;
        }

        // Send key press
        tud_hid_keyboard_report(HID_ITF_PROTOCOL_KEYBOARD, modifier, keycode_array);
        vTaskDelay(pdMS_TO_TICKS(50));

        // Send key release
        tud_hid_keyboard_report(HID_ITF_PROTOCOL_KEYBOARD, 0, NULL);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

// UART event handler task
static void uart_event_task(void *pvParameters)
{
    uart_event_t event;
    uint8_t* dtmp = (uint8_t*) malloc(RD_BUF_SIZE);
    assert(dtmp);

    for (;;) {
        // Waiting for UART event
        if (xQueueReceive(uart0_queue, (void *)&event, (TickType_t)portMAX_DELAY)) {
            bzero(dtmp, RD_BUF_SIZE);
            switch (event.type) {
            case UART_DATA:
                // Read data from UART
                int len = uart_read_bytes(EX_UART_NUM, dtmp, event.size, portMAX_DELAY);
                if (len > 0) {
                    // Log hex values for debugging arrow keys
                    char hex_str[64] = {0};
                    int hex_idx = 0;
                    for (int i = 0; i < len && hex_idx < sizeof(hex_str) - 3; i++) {
                        hex_idx += snprintf(hex_str + hex_idx, sizeof(hex_str) - hex_idx,
                                         "%02x ", (unsigned char)dtmp[i]);
                    }
                    ESP_LOGI(TAG, "Received %d bytes via UART: %s", len, hex_str);

                    // Process each character, handling escape sequences
                    for (int i = 0; i < len; i++) {
                        if (dtmp[i] == '\0') continue;

                        // Check if this is an escape character
                        if (is_escape_start(dtmp[i])) {
                            // Start new escape sequence
                            escape_seq_idx = 0;
                            escape_seq_buf[escape_seq_idx++] = dtmp[i];
                        } else if (escape_seq_idx > 0) {
                            // We're in the middle of an escape sequence
                            escape_seq_buf[escape_seq_idx++] = dtmp[i];

                            // Check if we have a complete escape sequence
                            if (escape_seq_idx >= 2) {
                                uint8_t keycode = process_escape_sequence(escape_seq_buf, escape_seq_idx);

                                if (keycode != 0) {
                                    // Valid escape sequence found
                                    send_keycode(keycode);
                                    escape_seq_idx = 0; // Reset buffer
                                    memset(escape_seq_buf, 0, sizeof(escape_seq_buf));
                                } else if (escape_seq_idx >= 7) {
                                    // Escape sequence too long, reset
                                    escape_seq_idx = 0;
                                    memset(escape_seq_buf, 0, sizeof(escape_seq_buf));
                                }
                            }
                        } else {
                            // Regular character, send immediately
                            send_key(dtmp[i]);
                        }
                    }
                }
                break;

            case UART_FIFO_OVF:
                ESP_LOGW(TAG, "UART FIFO overflow");
                uart_flush_input(EX_UART_NUM);
                xQueueReset(uart0_queue);
                break;

            case UART_BUFFER_FULL:
                ESP_LOGW(TAG, "UART buffer full");
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
    ESP_LOGI(TAG, "Starting ESP32 USB Keyboard with UART input");

    // Initialize button (optional - can be used for testing)
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

    // Install UART driver
    ESP_ERROR_CHECK(uart_driver_install(EX_UART_NUM, BUF_SIZE * 2, BUF_SIZE * 2, 20, &uart0_queue, 0));
    ESP_ERROR_CHECK(uart_param_config(EX_UART_NUM, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(EX_UART_NUM, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));

    // Create UART event handler task
    xTaskCreate(uart_event_task, "uart_event_task", 4096, NULL, 12, NULL);

    // Initialize USB
    ESP_LOGI(TAG, "USB initialization");
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

    ESP_LOGI(TAG, "ESP32 USB Keyboard ready. Type characters via 'idf.py monitor' to send as keyboard input.");

    // Main loop
    while (1) {
        if (tud_mounted()) {
            // USB is connected and ready
            vTaskDelay(pdMS_TO_TICKS(1000));
        } else {
            // USB not connected, wait
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
}
