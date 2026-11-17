#include <stdint.h>
#include <stdlib.h>
#include <assert.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_random.h"
#include "esp_log.h"

enum SensorSource {
    SOURCE_SENSOR_1 = 0,
    SOURCE_SENSOR_2 = 1,
};

typedef struct {
    enum SensorSource source;
    uint32_t sensor_data;
} SensorMessage_t ;

typedef struct {
    enum SensorSource source;
    const char *sensor_name;
    TickType_t period_ticks;
} SensorConfig_t;

static const char *LOGS_TAG = "6_3";

#define TASK_STACK_SIZE UINT32_C(3072)

#define PRIORITY_PRODUCER_TASK ((UBaseType_t)4)
#define PRIORITY_CONSUMER_TASK ((UBaseType_t)5)
#define PRIORITY_MAIN ((UBaseType_t)(20))

#define READ_PRODUCER_DELAY pdMS_TO_TICKS(500)
#define Q_SIZE UINT32_C(5)
#define Q_ITEM_SIZE sizeof(SensorMessage_t)
#define Q_PRODUCER_TIMEOUT pdMS_TO_TICKS(500)
#define Q_CONSUMER_TIMEOUT pdMS_TO_TICKS(1000)

#define ASSERT_BELOW(priority, max_priority) static_assert(priority < max_priority, ""#priority" exceeds maximum allowed value " #max_priority"")
#define ASSERT_LOWER_PRIORITY(priority, higher_priority) static_assert(priority < higher_priority, ""#priority" must be lower than " #higher_priority"")

ASSERT_BELOW(PRIORITY_PRODUCER_TASK, configMAX_PRIORITIES);
ASSERT_BELOW(PRIORITY_CONSUMER_TASK, configMAX_PRIORITIES);

ASSERT_LOWER_PRIORITY(PRIORITY_PRODUCER_TASK, PRIORITY_CONSUMER_TASK);

static_assert(sizeof(SensorMessage_t) <= 16, "SensorMessage_t is too large for efficient by-value queue copying");
static_assert(Q_SIZE > 0, "Queue capacity must be greater than zero");
static_assert(TASK_STACK_SIZE >= configMINIMAL_STACK_SIZE, "Task stack size must meet or exceed FreeRTOS minimum requirements");

// This func stops the program if xTaskCreate returns error
static inline void check_task_created(BaseType_t result, const char *name) {
    if (result != pdPASS) {
        ESP_LOGE(LOGS_TAG, "Task was not created (not enough heap?)", name);
        abort();
    }
}

static QueueHandle_t xSensorsQueue;

static void producer_sensor_1_task(void *pvParameters) {
    ESP_LOGI(LOGS_TAG, "Producer sensor 1 (Temperature) started");
    SensorMessage_t sensor_message;
    sensor_message.source = SOURCE_SENSOR_1;

    // Initial baseline temperature: 22.5°C (represented in tenths: 225)
    int32_t current_temp_x10 = 225;

    TickType_t xLastWakeTime = xTaskGetTickCount();

    for (;;) {
        // Apply small random drift (-3 to +3 tenths, i.e., -0.3°C to +0.3°C)
        int32_t step = ((int32_t)(esp_random() % 7)) - 3;
        current_temp_x10 += step;
        // Clamp between 18.0°C (180) and 32.0°C (320)
        if (current_temp_x10 < 180) current_temp_x10 = 180;
        if (current_temp_x10 > 320) current_temp_x10 = 320;
        sensor_message.sensor_data = (uint32_t)current_temp_x10;

        if (xQueueSend(xSensorsQueue, &sensor_message, Q_PRODUCER_TIMEOUT) != pdPASS) {
            ESP_LOGW(LOGS_TAG, "Queue full, sensor 1 frame dropped");
        }

        vTaskDelayUntil(&xLastWakeTime, READ_PRODUCER_DELAY);
    }

    ESP_LOGI(LOGS_TAG, "Producer sensor 1 task finished");
}

static void producer_sensor_2_task(void *pvParameters) {
    (void) pvParameters;

    ESP_LOGI(LOGS_TAG, "Producer sensor 2 (Light/ADC) started");

    SensorMessage_t sensor_message;
    sensor_message.source = SOURCE_SENSOR_2;

    TickType_t xLastWakeTime = xTaskGetTickCount();

    for (;;) {
        // Simulate a 12-bit ADC reading (0 to 4095) with ambient base + noise
        uint32_t base_ambient = 1500;
        uint32_t noise = esp_random() % 200; // 0 to 199 counts of jitter

        sensor_message.sensor_data = base_ambient + noise;
        if (xQueueSend(xSensorsQueue, &sensor_message, Q_PRODUCER_TIMEOUT) != pdPASS) {
            ESP_LOGW(LOGS_TAG, "Queue full, sensor 2 frame dropped");
        }

        vTaskDelayUntil(&xLastWakeTime, READ_PRODUCER_DELAY);
    }

    ESP_LOGI(LOGS_TAG, "Producer sensor 2 task finished");
}

static void consume_task(void *pvParameters) {
    (void) pvParameters;

    ESP_LOGI(LOGS_TAG, "Consumer task started");

    SensorMessage_t sensor_message;

    for (;;) {
        if (xQueueReceive(xSensorsQueue, &sensor_message, Q_CONSUMER_TIMEOUT) == pdPASS) {
            switch (sensor_message.source) {
                case SOURCE_SENSOR_1:
                    ESP_LOGI(LOGS_TAG, "[TEMP SENSOR] Temperature: %lu.%lu °C",
                             sensor_message.sensor_data / 10,
                             sensor_message.sensor_data % 10);
                    break;
                case SOURCE_SENSOR_2:
                    ESP_LOGI(LOGS_TAG, "[LIGHT SENSOR] Raw Intensity: %lu ADC counts",
                             sensor_message.sensor_data);
                    break;
            }

        } else {
            // can't receive any value for 500ms log a warning
            ESP_LOGW(LOGS_TAG, "Timeout - no data for 500ms");
        }
    }

    ESP_LOGI(LOGS_TAG, "Consumer task finished");
}


void app_main() {
    ESP_LOGI(LOGS_TAG, "Start the application.");

    // set the main task priority to reduce switching during creating tasks
    const UBaseType_t main_prio = uxTaskPriorityGet(NULL);
    vTaskPrioritySet(NULL, PRIORITY_MAIN);

    xSensorsQueue = xQueueCreate(Q_SIZE, Q_ITEM_SIZE);
    if (xSensorsQueue == NULL) {
        ESP_LOGE(LOGS_TAG, "Queue creation failed (out of memory?)");
        abort();
    }

    BaseType_t isOk = pdPASS;

    check_task_created(xTaskCreate(producer_sensor_1_task, "producer_sensor_1_task", TASK_STACK_SIZE, NULL, PRIORITY_PRODUCER_TASK, NULL), "Producer 1");
    check_task_created(xTaskCreate(producer_sensor_2_task, "producer_sensor_2_task", TASK_STACK_SIZE, NULL, PRIORITY_PRODUCER_TASK, NULL), "Producer 2");
    check_task_created(xTaskCreate(consume_task, "consume_task", TASK_STACK_SIZE, NULL, PRIORITY_CONSUMER_TASK, NULL), "Consumer");

    ESP_LOGI(LOGS_TAG, "Finished initialization successfully");

    // set the main task priority back to the original value
    vTaskPrioritySet(NULL, main_prio);
}
