# ESP32 Wi-Fi Temperature Logger

An **ESP-IDF firmware in C** for the **Wemos D1 R32 (ESP32)** board: a self-contained Wi-Fi temperature logger and thermostat built around the analog **LMT85LPG** sensor.

## Features

- **Periodic sampling**: an `esp_timer` interrupt reads the LMT85LPG every 500 ms via **ADC1**, converts the raw voltage to °C using the datasheet transfer formula.
- **History storage**: every sample is stored in **NVS** (non-volatile memory) together with an **SNTP-synced RTC** timestamp (pool.ntp.org, GMT+2), so history survives reboots.
- **Web UI** (`esp_http_server`): current temperature page (auto-refreshes), full history table, clear-history button, and a threshold editor.
- **Thermostat output**: LED on `GPIO2` switches at the configured temperature threshold with **hysteresis** (default 3 °C, settable in menuconfig).
- **Wi-Fi**: connects at boot (SSID/password in `idf.py menuconfig`); the assigned IP address is printed over UART.

## Wiring

- LMT85LPG: `3.3V` + `GND`, analog output → `IO35`.
- LED (with resistor) → `IO2` + `GND`.

Schematics: `docs/img/board.png`, `docs/img/schematic.png`.

## Build & flash

```sh
idf.py menuconfig     # Wi-Fi credentials, hysteresis
idf.py flash monitor  # build, flash, watch UART (connection state, IP, readings)
```

More in `docs/doc.md` and `docs/dokumentace.pdf`.
