#include "ble_mouse.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"
#include "esp_hid_common.h"
#include "esp_hidd.h"
#include "esp_hidd_gatts.h"
#include "esp_log.h"

#define HID_REPORT_PERIOD_MS 20
#define HID_GYRO_DEAD_ZONE_RPS 0.035f
#define HID_DEFAULT_HORIZONTAL_GAIN 22.0f
#define HID_DEFAULT_VERTICAL_GAIN 28.0f
#define HID_MIN_GAIN 1.0f
#define HID_MAX_GAIN 60.0f

static const char *TAG = "ble-mouse";

typedef struct {
    float gyro[3];
    bool clutch;
} mouse_sample_t;

static const uint8_t mouse_report_map[] = {
    0x05, 0x01,  // Usage Page (Generic Desktop)
    0x09, 0x02,  // Usage (Mouse)
    0xa1, 0x01,  // Collection (Application)
    0x09, 0x01,  //   Usage (Pointer)
    0xa1, 0x00,  //   Collection (Physical)
    0x05, 0x09,  //     Usage Page (Button)
    0x19, 0x01,  //     Usage Minimum (Button 1)
    0x29, 0x03,  //     Usage Maximum (Button 3)
    0x15, 0x00,  //     Logical Minimum (0)
    0x25, 0x01,  //     Logical Maximum (1)
    0x95, 0x03,  //     Report Count (3)
    0x75, 0x01,  //     Report Size (1)
    0x81, 0x02,  //     Input (Data, Variable, Absolute)
    0x95, 0x01,  //     Report Count (1)
    0x75, 0x05,  //     Report Size (5)
    0x81, 0x03,  //     Input (Constant)
    0x05, 0x01,  //     Usage Page (Generic Desktop)
    0x09, 0x30,  //     Usage (X)
    0x09, 0x31,  //     Usage (Y)
    0x09, 0x38,  //     Usage (Wheel)
    0x15, 0x81,  //     Logical Minimum (-127)
    0x25, 0x7f,  //     Logical Maximum (127)
    0x75, 0x08,  //     Report Size (8)
    0x95, 0x03,  //     Report Count (3)
    0x81, 0x06,  //     Input (Data, Variable, Relative)
    0xc0,        //   End Collection
    0xc0,        // End Collection
};

static esp_hid_raw_report_map_t report_maps[] = {
    {
        .data = mouse_report_map,
        .len = sizeof(mouse_report_map),
    },
};

static esp_hid_device_config_t hid_config = {
    .vendor_id = 0x16c0,
    .product_id = 0x05df,
    .version = 0x0100,
    .manufacturer_name = "Tactile",
    .report_maps = report_maps,
    .report_maps_len = 1,
};

static esp_hidd_dev_t *hid_device;
static QueueHandle_t sample_queue;
static QueueHandle_t click_queue;
static tactile_ble_mouse_status_callback_t mouse_status_callback;
static volatile bool mouse_ready;
static volatile bool mouse_connected;
static volatile uint32_t reports_sent;
static volatile uint32_t clicks_sent;
static volatile char vertical_axis = 'y';
static volatile bool vertical_inverted;
static volatile float horizontal_gain = HID_DEFAULT_HORIZONTAL_GAIN;
static volatile float vertical_gain = HID_DEFAULT_VERTICAL_GAIN;
static bool advertising_data_ready;
static bool scan_response_ready;
static bool hid_service_ready;
static char device_name[32];
static char serial_number[16];

static void publish_status(void) {
    if (mouse_status_callback) {
        mouse_status_callback(mouse_ready, mouse_connected);
    }
}

static void start_advertising_if_ready(void) {
    if (!advertising_data_ready || !scan_response_ready ||
        !hid_service_ready || mouse_connected) {
        return;
    }
    static esp_ble_adv_params_t parameters = {
        .adv_int_min = 0x20,
        .adv_int_max = 0x30,
        .adv_type = ADV_TYPE_IND,
        .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
        .channel_map = ADV_CHNL_ALL,
        .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
    };
    esp_err_t error = esp_ble_gap_start_advertising(&parameters);
    if (error != ESP_OK && error != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "Could not start advertising: %s",
                 esp_err_to_name(error));
    }
}

static void gap_event(esp_gap_ble_cb_event_t event,
                      esp_ble_gap_cb_param_t *parameters) {
    switch (event) {
        case ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT:
            advertising_data_ready =
                parameters->adv_data_cmpl.status == ESP_BT_STATUS_SUCCESS;
            start_advertising_if_ready();
            break;
        case ESP_GAP_BLE_SCAN_RSP_DATA_SET_COMPLETE_EVT:
            scan_response_ready =
                parameters->scan_rsp_data_cmpl.status ==
                ESP_BT_STATUS_SUCCESS;
            start_advertising_if_ready();
            break;
        case ESP_GAP_BLE_SEC_REQ_EVT:
            esp_ble_gap_security_rsp(
                parameters->ble_security.ble_req.bd_addr, true);
            break;
        case ESP_GAP_BLE_AUTH_CMPL_EVT:
            if (parameters->ble_security.auth_cmpl.success) {
                ESP_LOGI(TAG, "BLE bond established");
            } else {
                ESP_LOGW(TAG, "BLE authentication failed: 0x%x",
                         parameters->ble_security.auth_cmpl.fail_reason);
            }
            break;
        default:
            break;
    }
}

static void hid_event(void *handler_args, esp_event_base_t base, int32_t id,
                      void *event_data) {
    (void)handler_args;
    (void)base;
    esp_hidd_event_data_t *parameters = event_data;
    switch ((esp_hidd_event_t)id) {
        case ESP_HIDD_START_EVENT:
            hid_service_ready = true;
            mouse_ready = true;
            ESP_LOGI(TAG, "BLE HID ready as \"%s\"", device_name);
            publish_status();
            start_advertising_if_ready();
            break;
        case ESP_HIDD_CONNECT_EVENT:
            hid_device = parameters->connect.dev;
            mouse_connected = true;
            ESP_LOGI(TAG, "BLE mouse connected");
            publish_status();
            break;
        case ESP_HIDD_DISCONNECT_EVENT:
            mouse_connected = false;
            ESP_LOGI(TAG, "BLE mouse disconnected");
            publish_status();
            start_advertising_if_ready();
            break;
        case ESP_HIDD_STOP_EVENT:
            hid_service_ready = false;
            mouse_ready = false;
            mouse_connected = false;
            publish_status();
            break;
        default:
            break;
    }
}

static int8_t report_delta(float value, float *remainder) {
    float accumulated = value + *remainder;
    int delta = (int)truncf(accumulated);
    if (delta > 127) {
        delta = 127;
    } else if (delta < -127) {
        delta = -127;
    }
    *remainder = accumulated - (float)delta;
    return (int8_t)delta;
}

static bool send_mouse_report(uint8_t buttons, int8_t dx, int8_t dy) {
    uint8_t report[4] = {buttons, (uint8_t)dx, (uint8_t)dy, 0};
    esp_err_t error =
        esp_hidd_dev_input_set(hid_device, 0, 0, report, sizeof(report));
    if (error != ESP_OK) {
        ESP_LOGD(TAG, "Mouse report failed: %s", esp_err_to_name(error));
        return false;
    }
    ++reports_sent;
    return true;
}

static void mouse_report_loop(void *context) {
    (void)context;
    mouse_sample_t sample = {0};
    float horizontal_remainder = 0.0f;
    float vertical_remainder = 0.0f;
    bool release_click = false;
    TickType_t wake = xTaskGetTickCount();
    while (true) {
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(HID_REPORT_PERIOD_MS));
        mouse_sample_t latest;
        if (xQueueReceive(sample_queue, &latest, 0) == pdTRUE) {
            sample = latest;
        }
        if (mouse_connected && hid_device) {
            if (release_click) {
                if (send_mouse_report(0, 0, 0)) {
                    release_click = false;
                }
                continue;
            }
            uint8_t click;
            if (xQueueReceive(click_queue, &click, 0) == pdTRUE) {
                if (send_mouse_report(1, 0, 0)) {
                    ++clicks_sent;
                    release_click = true;
                }
                continue;
            }
        }
        if (!sample.clutch) {
            horizontal_remainder = 0.0f;
            vertical_remainder = 0.0f;
            continue;
        }
        if (!mouse_connected || !hid_device) {
            continue;
        }

        float motion[2];
        tactile_ble_mouse_motion(sample.gyro, motion);
        int8_t dx = report_delta(motion[0], &horizontal_remainder);
        int8_t dy = report_delta(motion[1], &vertical_remainder);
        if (dx == 0 && dy == 0) {
            continue;
        }
        send_mouse_report(0, dx, dy);
    }
}

bool tactile_ble_mouse_init(
    const char *role, const char *device_id,
    tactile_ble_mouse_status_callback_t status_callback) {
    mouse_status_callback = status_callback;
    snprintf(device_name, sizeof(device_name), "Tactile %s", role);
    snprintf(serial_number, sizeof(serial_number), "tactile-%s", device_id);
    hid_config.device_name = device_name;
    hid_config.serial_number = serial_number;

    sample_queue = xQueueCreate(1, sizeof(mouse_sample_t));
    click_queue = xQueueCreate(4, sizeof(uint8_t));
    if (!sample_queue || !click_queue) {
        ESP_LOGE(TAG, "Could not allocate mouse input queues");
        return false;
    }

    esp_err_t error = esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "Could not release Classic Bluetooth memory: %s",
                 esp_err_to_name(error));
        return false;
    }
    esp_bt_controller_config_t controller =
        BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    error = esp_bt_controller_init(&controller);
    if (error == ESP_OK) {
        error = esp_bt_controller_enable(ESP_BT_MODE_BLE);
    }
    if (error == ESP_OK) {
        esp_bluedroid_config_t bluedroid =
            BT_BLUEDROID_INIT_CONFIG_DEFAULT();
        error = esp_bluedroid_init_with_cfg(&bluedroid);
    }
    if (error == ESP_OK) {
        error = esp_bluedroid_enable();
    }
    if (error == ESP_OK) {
        error = esp_ble_gap_register_callback(gap_event);
    }
    if (error == ESP_OK) {
        error = esp_ble_gatts_register_callback(
            esp_hidd_gatts_event_handler);
    }
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "BLE initialization failed: %s",
                 esp_err_to_name(error));
        return false;
    }

    esp_ble_auth_req_t authentication = ESP_LE_AUTH_REQ_SC_BOND;
    esp_ble_io_cap_t input_output = ESP_IO_CAP_NONE;
    uint8_t keys = ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK;
    uint8_t key_size = 16;
    ESP_ERROR_CHECK(esp_ble_gap_set_security_param(
        ESP_BLE_SM_AUTHEN_REQ_MODE, &authentication,
        sizeof(authentication)));
    ESP_ERROR_CHECK(esp_ble_gap_set_security_param(
        ESP_BLE_SM_IOCAP_MODE, &input_output, sizeof(input_output)));
    ESP_ERROR_CHECK(esp_ble_gap_set_security_param(
        ESP_BLE_SM_SET_INIT_KEY, &keys, sizeof(keys)));
    ESP_ERROR_CHECK(esp_ble_gap_set_security_param(
        ESP_BLE_SM_SET_RSP_KEY, &keys, sizeof(keys)));
    ESP_ERROR_CHECK(esp_ble_gap_set_security_param(
        ESP_BLE_SM_MAX_KEY_SIZE, &key_size, sizeof(key_size)));
    ESP_ERROR_CHECK(esp_ble_gap_set_device_name(device_name));

    static const uint8_t hid_service_uuid[] = {
        0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00, 0x00, 0x80,
        0x00, 0x10, 0x00, 0x00, 0x12, 0x18, 0x00, 0x00,
    };
    esp_ble_adv_data_t advertising = {
        .set_scan_rsp = false,
        .include_name = false,
        .include_txpower = false,
        .min_interval = 0x0006,
        .max_interval = 0x0010,
        .appearance = ESP_HID_APPEARANCE_MOUSE,
        .service_uuid_len = sizeof(hid_service_uuid),
        .p_service_uuid = (uint8_t *)hid_service_uuid,
        .flag = ESP_BLE_ADV_FLAG_GEN_DISC |
                ESP_BLE_ADV_FLAG_BREDR_NOT_SPT,
    };
    esp_ble_adv_data_t scan_response = {
        .set_scan_rsp = true,
        .include_name = true,
        .include_txpower = true,
    };
    error = esp_ble_gap_config_adv_data(&advertising);
    if (error == ESP_OK) {
        error = esp_ble_gap_config_adv_data(&scan_response);
    }
    if (error == ESP_OK) {
        error = esp_hidd_dev_init(
            &hid_config, ESP_HID_TRANSPORT_BLE, hid_event, &hid_device);
    }
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "BLE HID setup failed: %s", esp_err_to_name(error));
        return false;
    }
    if (xTaskCreate(mouse_report_loop, "ble-mouse", 4096, NULL, 8, NULL) !=
        pdPASS) {
        ESP_LOGE(TAG, "Could not allocate BLE mouse task");
        return false;
    }
    return true;
}

void tactile_ble_mouse_sample(const float angular_velocity_rps[3],
                              bool clutch_pressed) {
    if (!sample_queue) {
        return;
    }
    mouse_sample_t sample = {
        .gyro = {
            angular_velocity_rps[0],
            angular_velocity_rps[1],
            angular_velocity_rps[2],
        },
        .clutch = clutch_pressed,
    };
    xQueueOverwrite(sample_queue, &sample);
}

void tactile_ble_mouse_motion(const float angular_velocity_rps[3],
                              float motion[2]) {
    float horizontal = angular_velocity_rps[2];
    float vertical = angular_velocity_rps[vertical_axis == 'x' ? 0 : 1];
    if (fabsf(horizontal) < HID_GYRO_DEAD_ZONE_RPS) {
        horizontal = 0.0f;
    }
    if (fabsf(vertical) < HID_GYRO_DEAD_ZONE_RPS) {
        vertical = 0.0f;
    }
    motion[0] = -horizontal * horizontal_gain;
    motion[1] = vertical * vertical_gain *
                (vertical_inverted ? -1.0f : 1.0f);
}

bool tactile_ble_mouse_click(void) {
    if (!mouse_connected || !click_queue) {
        return false;
    }
    uint8_t click = 1;
    return xQueueSend(click_queue, &click, 0) == pdTRUE;
}

uint32_t tactile_ble_mouse_clicks_sent(void) {
    return clicks_sent;
}

bool tactile_ble_mouse_set_vertical_axis(char axis) {
    if (axis != 'x' && axis != 'y') {
        return false;
    }
    vertical_axis = axis;
    return true;
}

char tactile_ble_mouse_vertical_axis(void) {
    return vertical_axis;
}

void tactile_ble_mouse_set_vertical_inverted(bool inverted) {
    vertical_inverted = inverted;
}

bool tactile_ble_mouse_vertical_inverted(void) {
    return vertical_inverted;
}

bool tactile_ble_mouse_set_gain(float horizontal, float vertical) {
    if (horizontal < HID_MIN_GAIN || horizontal > HID_MAX_GAIN ||
        vertical < HID_MIN_GAIN || vertical > HID_MAX_GAIN) {
        return false;
    }
    horizontal_gain = horizontal;
    vertical_gain = vertical;
    return true;
}

void tactile_ble_mouse_gain(float *horizontal, float *vertical) {
    if (horizontal) {
        *horizontal = horizontal_gain;
    }
    if (vertical) {
        *vertical = vertical_gain;
    }
}

bool tactile_ble_mouse_ready(void) {
    return mouse_ready;
}

bool tactile_ble_mouse_connected(void) {
    return mouse_connected;
}

uint32_t tactile_ble_mouse_reports_sent(void) {
    return reports_sent;
}
