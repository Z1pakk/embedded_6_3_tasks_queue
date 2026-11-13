# 6_3 Tasks with Queue

Homework 6.3: a FreeRTOS-based telemetry system using the **Producer-Consumer pattern** with an inter-task message queue.
Runs on an **ESP32-S3-DevKitC-1** with **ESP-IDF 6.1** (PlatformIO).

---

## Overview

The system collects simulated telemetry from two independent sensor producer tasks and safely transfers data over a FreeRTOS queue (`xSensorsQueue`) to a dedicated consumer task that formats and outputs the telemetry to the serial monitor.

```
+------------------------------------+
| Producer 1: Temperature Sensor    | ---\
| (Drift model, period: 500 ms)      |     \   xQueueSend()
+------------------------------------+      \      +-----------------------+   xQueueReceive()   +----------------------+
                                             +---> |  FreeRTOS Queue       | ------------------> |  Consumer Task       | ---> Serial Log
+------------------------------------+      /      |  (Capacity: 5 items)  |                     |  (Format & Display)  |      (115200 baud)
| Producer 2: Light / ADC Sensor     | ---/        +-----------------------+                     +----------------------+
| (Ambient + noise, period: 500 ms)  |
+------------------------------------+
```

---

## Tasks & Priorities

| Task | Priority | Period / Timing | Description |
|---|---|---|---|
| **Producer 1 (Temperature)** | Normal (4) | 500 ms (`vTaskDelayUntil`) | Simulates realistic ambient temperature with random walk/drift (18.0°C – 32.0°C in fixed-point tenths). Dispatches `SensorMessage_t` with `SOURCE_SENSOR_1`. |
| **Producer 2 (Light / ADC)** | Normal (4) | 500 ms (`vTaskDelayUntil`) | Simulates 12-bit ADC light intensity (1500 counts ambient base + 0–199 counts jitter). Dispatches `SensorMessage_t` with `SOURCE_SENSOR_2`. |
| **Consumer Task** | High (5) | Event-driven (1000 ms timeout) | Blocks on `xQueueReceive()`. When a message arrives, unpacks payload by sensor source and logs formatted telemetry to the UART console. |

---

## Key Design Principles & Features

1. **Thread-Safe Data Transfer via Queue**:
   - `xSensorsQueue` holds up to 5 elements (`Q_SIZE = 5`).
   - Data is transferred by value using `SensorMessage_t` (8 bytes), ensuring zero pointer aliasing and thread isolation without manual mutex locking.

2. **Priority Hierarchy for Maximum Throughput**:
   - Consumer priority (5) is higher than producer priority (4). As soon as telemetry is posted to the queue, the consumer immediately preempts producers and drains the queue, minimizing latency and preventing queue saturation.

3. **Deterministic Periodic Execution**:
   - Producers use `vTaskDelayUntil()` instead of `vTaskDelay()` to avoid cumulative timing jitter and clock drift during sampling cycles.

4. **Hardware Entropy**:
   - Uses ESP32 hardware True Random Number Generator (`esp_random()`) to generate realistic non-repeating sensor data across system boots.

5. **Fixed-Point Efficiency**:
   - Temperature readings are encoded in tenths of a degree (e.g. `225` = `22.5°C`), eliminating floating-point math overhead on embedded cores.

6. **Compile-Time Safety (`static_assert`)**:
   - Asserts priority boundaries against `configMAX_PRIORITIES`.
   - Asserts priority relationship (`PRIORITY_PRODUCER_TASK < PRIORITY_CONSUMER_TASK`).
   - Asserts `sizeof(SensorMessage_t) <= 16` to prevent oversized by-value copying.
   - Asserts valid queue capacity (`Q_SIZE > 0`) and minimum stack allocation (`TASK_STACK_SIZE >= configMINIMAL_STACK_SIZE`).

---

## Build, Flash & Monitor

```sh
pio run                  # Compile firmware
pio run -t upload        # Flash to ESP32-S3
pio device monitor       # Open serial log at 115200 baud
```

### Expected Output Example:
```text
I (320) 6_3: Start the application.
I (325) 6_3: Producer sensor 1 (Temperature) started
I (330) 6_3: Producer sensor 2 (Light/ADC) started
I (335) 6_3: Consumer task started
I (340) 6_3: Finished initialization successfully
I (830) 6_3: [TEMP SENSOR] Temperature: 22.4 °C
I (835) 6_3: [LIGHT SENSOR] Raw Intensity: 1584 ADC counts
I (1330) 6_3: [TEMP SENSOR] Temperature: 22.6 °C
I (1335) 6_3: [LIGHT SENSOR] Raw Intensity: 1612 ADC counts
```

---

## File Structure

```
6_3_tasks_with_queue/
├── CMakeLists.txt              # ESP-IDF project CMake configuration
├── platformio.ini              # PlatformIO environment (esp32-s3-devkitc-1, espidf, 115200 baud)
├── README.md                   # Project documentation and architectural overview
└── src/
    ├── CMakeLists.txt          # Source component registration
    └── main.c                  # FreeRTOS tasks, queue management, and entry point
```
