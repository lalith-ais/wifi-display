# wifi-display

An ESP-IDF firmware component for ESP32-P4 that brings up Wi-Fi (via the `esp_hosted` / `esp_wifi_remote` stack) and a GT911 capacitive touch controller under a lightweight supervisor that automatically restarts crashed or stuck tasks.

---

## Overview

The project establishes a service-oriented architecture on top of FreeRTOS. A central **supervisor** task acts as an init process: it spawns named services, monitors their liveness with both FreeRTOS task-state polling and an optional heartbeat mechanism, and restarts them (with exponential back-off) if they die or become stuck. Essential services that exhaust their restarts trigger a full system reboot, with the failing service name persisted to NVS so it is visible in the next boot log.

Two services are registered at startup:

| Service | Essential | Restart policy | Heartbeat timeout |
|---------|-----------|----------------|-------------------|
| `wifi`  | yes       | always         | 30 s              |
| `gt911` | no        | always         | 5 s               |

---

## Hardware targets

| Target     | Wi-Fi stack used                                 |
|------------|--------------------------------------------------|
| ESP32-P4   | `esp_hosted` + `esp_wifi_remote` (hosted mode)  |


The `idf_component.yml` applies `esp_hosted` and `esp_wifi_remote` conditionally for those two targets only; other targets use the standard `esp_wifi` driver.

---

## Project structure

```
main/
├── main.c               # app_main: NVS init → supervisor_start()
├── supervisor.c/h       # Init-process supervisor (task watchdog + heartbeat)
├── system.c/h           # Service registry (services[])
├── priorities.h         # Centralised FreeRTOS task priorities
├── wifi_setup.c/h       # Low-level Wi-Fi driver wrapper (esp_wifi / esp_wifi_remote)
├── wifi_service.c/h     # Wi-Fi service task + event queue API
├── gt911_service.c/h    # GT911 touch-controller service task + event queue API
├── CMakeLists.txt
├── idf_component.yml    # Component dependencies
└── Kconfig.projbuild    # Menuconfig: SSID, password, retry count, hosted transport
```

---

## Dependencies

Managed via the ESP-IDF Component Manager (`idf_component.yml`):

- `espressif/esp_lcd_st7701` ≥ 2.0.2
- `espressif/esp_lcd_touch` ≥ 1.1.0
- `espressif/esp_lcd_touch_gt911` ≥ 1.1.0
- `espressif/esp_wifi_remote` ≥ 0.10, < 2.0 *(P4 / H2 only)*
- `espressif/esp_hosted` ~2 *(P4 / H2 only)*

---

## Getting started

### Prerequisites

- ESP-IDF v5.x (v5.3 or later recommended)
- Python ≥ 3.8 with the IDF virtual environment activated

### Configure

```bash
idf.py menuconfig
```

Under **WiFi Configuration** set:

| Option | Default | Description |
|--------|---------|-------------|
| `CONFIG_WIFI_SSID` | `myssid` | Network SSID |
| `CONFIG_WIFI_PASSWORD` | `mypassword` | Network password |
| `CONFIG_WIFI_MAX_RETRY` | `5` | Connection retries before restart |
| `CONFIG_HOSTED_TRANSPORT` | `0` | `0` = SPI, `1` = SDIO, `2` = UART |

### Build and flash

```bash
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

---

## Architecture

### Supervisor

`supervisor_start(services)` spawns a single high-priority task (priority 24) that:

1. Reads any previous crash reason from NVS and logs it at boot.
2. Starts each service defined in the `services[]` array.
3. Polls every 5 seconds (`SUPERVISOR_CHECK_MS`), checking both FreeRTOS task state and heartbeat counters.
4. On death or a missed heartbeat, applies the service's restart policy with exponential back-off (1 s → 2 s → 4 s → 8 s max).
5. If an essential service cannot be recovered, persists its name to NVS and calls `esp_restart()`.

Services call `supervisor_heartbeat(name)` periodically to prove they are not stuck. The call is ISR-safe (atomic increment).

### Priority hierarchy

```
PRIO_SUPERVISOR         24   (init process)
PRIO_WIFI_SUPERVISOR    23
PRIO_WIFI_SERVICE       22
PRIO_GT911_SUPERVISOR   15
PRIO_GT911_SERVICE      14
PRIO_DISPLAY_SUPERVISOR 10   (reserved for future display service)
PRIO_DISPLAY_SERVICE     9
```

All service priorities must be strictly less than `PRIO_SUPERVISOR`; the supervisor enforces this at registration time and rejects any violators.

### Wi-Fi service

`wifi_service.c` wraps `wifi_setup.c` in a FreeRTOS task with a 10-element event queue. Internal callbacks from the `esp_wifi` event loop post `wifi_service_message_t` messages to the queue. Consumers can retrieve the queue handle via `wifi_service_get_queue()` and wait on connection / IP events.

Public query functions (`wifi_service_is_connected()`, `wifi_service_has_ip()`, `wifi_service_get_ip()`, `wifi_service_get_mac()`) are safe to call from any task.

### GT911 touch service

`gt911_service.c` initialises the GT911 over I2C at 400 kHz (SDA = GPIO 7, SCL = GPIO 8, RST = GPIO 35, INT = GPIO 3) and polls at 50 Hz. Touch points are posted to a 20-element queue as `gt911_touch_event_t` structs. Consumers obtain the queue handle via `gt911_service_get_queue()`.

The display resolution is currently fixed at 480 × 800 px (placeholder for the forthcoming display service).

---

## Adding a new service

1. Implement a supervisor entry-point function with signature `void my_service_supervisor(void *arg)`.
2. Add it to the `services[]` array in `system.c`:

```c
{"my-svc", my_service_supervisor, 4096, PRIO_MY_SUPERVISOR, RESTART_ALWAYS, false, NULL, 15},
```

3. Call `supervisor_heartbeat("my-svc")` inside your service loop at least once per `heartbeat_timeout_s` seconds.
4. Add priority constants to `priorities.h`.

---

## Roadmap

- [ ] Display service (ST7701 panel via SPI/RGB)
- [ ] LVGL integration consuming the GT911 touch queue
- [ ] MQTT / HTTP client service
- [ ] OTA update service

---

## Licence


