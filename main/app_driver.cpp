/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <esp_log.h>
#include <stdlib.h>
#include <string.h>

#include <esp_matter.h>
#include <esp_matter_event.h>
#include "bsp/esp-bsp.h"
#include <app-common/zap-generated/attributes/Accessors.h>

#include <app_priv.h>
#include "board_config.h"

using namespace chip::app::Clusters;
using namespace esp_matter;

static const char *TAG = "app_driver";
extern uint16_t onoff_endpoint_id;

app_driver_handle_t app_driver_onoff_init() {
    /* Initialize plug */
    ESP_LOGI(TAG, "ONOFF_PLUGIN_GPIO_INIT");

    gpio_reset_pin(CHANNEL_1_PIN);
    gpio_set_direction(CHANNEL_1_PIN, GPIO_MODE_OUTPUT);
    // gpio_set_pull_mode(CHANNEL_1_PIN, GPIO_PULLUP_ONLY);
    gpio_set_level(CHANNEL_1_PIN, DEFAULT_POWER);

    gpio_set_direction(LED_INDICATOR_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(LED_INDICATOR_PIN, DEFAULT_POWER);

    return NULL;
}

/* Do any conversions/remapping for the actual value here */
static esp_err_t app_driver_onoff_set_power(esp_matter_attr_val_t *val)
{
    esp_err_t err = ESP_OK;
    bool new_state = val->val.b;

    // GPIO handling
    err = gpio_set_level(CHANNEL_1_PIN, new_state);
    err |= gpio_set_level(LED_INDICATOR_PIN, new_state);
    
    ESP_LOGE(TAG, "SET CHANNEL 1 to %d", new_state);
    return err;
}

esp_err_t app_driver_attribute_update(app_driver_handle_t driver_handle, uint16_t endpoint_id, uint32_t cluster_id,
                                      uint32_t attribute_id, esp_matter_attr_val_t *val)
{
    esp_err_t err = ESP_OK;
    if (endpoint_id == onoff_endpoint_id) {
        if (cluster_id == OnOff::Id) {
            if (attribute_id == OnOff::Attributes::OnOff::Id) {
                err = app_driver_onoff_set_power(val);
            }
        }
    }
    return err;
}

bool perform_factory_reset = false;
static void onboard_button_factory_reset_pressed_cb(void *arg, void *data)
{
    if (!perform_factory_reset) {
        ESP_LOGI(TAG, "Factory reset triggered. Release the button to start factory reset.");
        perform_factory_reset = true;
    }
}

static void onboard_button_factory_reset_released_cb(void *arg, void *data)
{
    if (perform_factory_reset) {
        ESP_LOGI(TAG, "Starting factory reset");
        factory_reset();
        perform_factory_reset = false;
    }
}
static void onboard_button_single_click_cb(void *arg, void *data)
{
    ESP_LOGI(TAG, "Toggle button pressed");
    uint16_t endpoint_id = onoff_endpoint_id;
    uint32_t cluster_id = OnOff::Id;
    uint32_t attribute_id = OnOff::Attributes::OnOff::Id;

    node_t *node = node::get();
    endpoint_t *endpoint = endpoint::get(node, endpoint_id);
    cluster_t *cluster = cluster::get(endpoint, cluster_id);
    attribute_t *attribute = attribute::get(cluster, attribute_id);

    esp_matter_attr_val_t val = esp_matter_invalid(NULL);
    attribute::get_val(attribute, &val);
    val.val.b = !val.val.b;
    attribute::update(endpoint_id, cluster_id, attribute_id, &val);
}

app_driver_handle_t app_driver_onboard_button_init(){
    button_config_t gpio_btn_cfg = {
        .type = BUTTON_TYPE_GPIO,
        .long_press_time = CONFIG_BUTTON_LONG_PRESS_TIME_MS,
        .short_press_time = CONFIG_BUTTON_SHORT_PRESS_TIME_MS,
        .gpio_button_config = {
            .gpio_num = ONBOARD_BUTTON_PIN,
            .active_level = 0,
        },
    };

    button_handle_t button_handle = iot_button_create(&gpio_btn_cfg);
    iot_button_register_cb(button_handle, BUTTON_LONG_PRESS_HOLD, onboard_button_factory_reset_pressed_cb, NULL);
    iot_button_register_cb(button_handle, BUTTON_PRESS_UP, onboard_button_factory_reset_released_cb, NULL);
    iot_button_register_cb(button_handle, BUTTON_SINGLE_CLICK, onboard_button_single_click_cb, NULL);
    return (app_driver_handle_t)button_handle;
}

// -------------------------------------------
// EXTERNAL BUTTON HANDLING
// -------------------------------------------

static void app_driver_button_initial_pressed(void *arg, void *data)
{
    ESP_LOGI(TAG, "Initial button pressed");
    gpio_button * button = (gpio_button*)data;
    int switch_endpoint_id = (button != NULL) ? get_endpoint(button) : 1;
    // Press moves Position from 0 (idle) to 1 (press)
    uint8_t newPosition = 1;
    lock::chip_stack_lock(portMAX_DELAY);
    chip::app::Clusters::Switch::Attributes::CurrentPosition::Set(switch_endpoint_id, newPosition);
    // InitialPress event takes newPosition as event data
    cluster::switch_cluster::event::send_initial_press(switch_endpoint_id, newPosition);
    lock::chip_stack_unlock();
}

static void app_driver_button_release(void *arg, void *data)
{
    gpio_button *button = (gpio_button *)data;
    int switch_endpoint_id = (button != NULL) ? get_endpoint(button) : 1;
    if(iot_button_get_ticks_time((button_handle_t)arg) < 5000){
    ESP_LOGI(TAG, "Short button release");
    // Release moves Position from 1 (press) to 0 (idle)
    uint8_t previousPosition = 1;
    uint8_t newPosition = 0;
    lock::chip_stack_lock(portMAX_DELAY);
    chip::app::Clusters::Switch::Attributes::CurrentPosition::Set(switch_endpoint_id, newPosition);
    // ShortRelease event takes previousPosition as event data
    cluster::switch_cluster::event::send_short_release(switch_endpoint_id, previousPosition);
    lock::chip_stack_unlock();
    }
    else{
    ESP_LOGI(TAG, "Long button release");
    // Release moves Position from 1 (press) to 0 (idle)
    uint8_t previousPosition = 1;
    uint8_t newPosition = 0;
    lock::chip_stack_lock(portMAX_DELAY);
    chip::app::Clusters::Switch::Attributes::CurrentPosition::Set(switch_endpoint_id, newPosition);
    // LongRelease event takes previousPositionPosition as event data
    cluster::switch_cluster::event::send_long_release(switch_endpoint_id, previousPosition);
    lock::chip_stack_unlock();
    }
}

static void app_driver_button_long_pressed(void *arg, void *data)
{
    ESP_LOGI(TAG, "Long button pressed ");
    gpio_button *button = (gpio_button *)data;
    int switch_endpoint_id = (button != NULL) ? get_endpoint(button) : 1;
    // Press moves Position from 0 (idle) to 1 (press)
    uint8_t newPosition = 1;
    lock::chip_stack_lock(portMAX_DELAY);
    chip::app::Clusters::Switch::Attributes::CurrentPosition::Set(switch_endpoint_id, newPosition);
    // LongPress event takes newPosition as event data
    cluster::switch_cluster::event::send_long_press(switch_endpoint_id, newPosition);
    lock::chip_stack_unlock();
}

static int current_number_of_presses_counted = 1;

static void app_driver_button_multipress_ongoing(void *arg, void *data)
{
    ESP_LOGI(TAG, "Multipress Ongoing");
    gpio_button * button = (gpio_button *)data;
    int switch_endpoint_id = (button != NULL) ? get_endpoint(button) : 1;
    // Press moves Position from 0 (idle) to 1 (press)
    uint8_t newPosition = 1;
    current_number_of_presses_counted++;
    lock::chip_stack_lock(portMAX_DELAY);
    chip::app::Clusters::Switch::Attributes::CurrentPosition::Set(switch_endpoint_id, newPosition);
    // MultiPress Ongoing event takes newPosition and current_number_of_presses_counted as event data
    cluster::switch_cluster::event::send_multi_press_ongoing(switch_endpoint_id, newPosition, current_number_of_presses_counted);
    lock::chip_stack_unlock();
}

static void app_driver_button_multipress_complete(void *arg, void *data)
{
    ESP_LOGI(TAG, "Multipress Complete");
    gpio_button * button = (gpio_button *)data;
    int switch_endpoint_id = (button != NULL) ? get_endpoint(button) : 1;
    // Press moves Position from 0 (idle) to 1 (press)
    uint8_t previousPosition = 1;
    uint8_t newPosition = 0;
    int total_number_of_presses_counted = current_number_of_presses_counted;
    lock::chip_stack_lock(portMAX_DELAY);
    chip::app::Clusters::Switch::Attributes::CurrentPosition::Set(switch_endpoint_id, newPosition);
    // MultiPress Complete event takes previousPosition and total_number_of_presses_counted as event data
    cluster::switch_cluster::event::send_multi_press_complete(switch_endpoint_id, previousPosition, total_number_of_presses_counted);
    // Reset current_number_of_presses_counted to initial value
    current_number_of_presses_counted = 1;
    lock::chip_stack_unlock();
}

app_driver_handle_t app_driver_button_init(gpio_button * button)
{
    button_config_t gpio_btn_cfg = {
        .type = BUTTON_TYPE_GPIO,
        .gpio_button_config = {
            .gpio_num = button->GPIO_PIN_VALUE,
            .active_level = 0,
        },
    };

    button_handle_t button_handle = iot_button_create(&gpio_btn_cfg);
    iot_button_register_cb(button_handle, BUTTON_PRESS_DOWN, app_driver_button_initial_pressed, button);
    iot_button_register_cb(button_handle, BUTTON_PRESS_UP, app_driver_button_release, button);
    iot_button_register_cb(button_handle, BUTTON_LONG_PRESS_START, app_driver_button_long_pressed, button);
    iot_button_register_cb(button_handle, BUTTON_PRESS_REPEAT, app_driver_button_multipress_ongoing, button);
    iot_button_register_cb(button_handle, BUTTON_PRESS_REPEAT_DONE, app_driver_button_multipress_complete, button);
    return (app_driver_handle_t)button_handle;
}
