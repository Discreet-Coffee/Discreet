# Discreet ☕

An open-source ESP32-based espresso machine controller with PID temperature control, pressure profiling, pre-infusion, and a web UI served directly from an SD card. Designed on the Gaggia Classic Pro but compatible with any single boiler machine.

Join the discord - discreetcoffee.co.uk

---

## Features

- **PID temperature control** — tunable Kp, Ki, Kd with SSR output
- **Pressure profiling** — closed-loop pump control via a dimmer module, targeting user-defined bar setpoints
- **Pre-infusion** — configurable pre-infusion time at a lower pressure before ramping to full pressure
- **Bloom time** — configurable bloom/pause phase
- **Web UI** — served from SD card, accessible via browser on your local network at `http://discreet.local`
- **Live data** — real-time temperature, pressure, shot timer, and pump power
- **Theme support** — swap CSS themes from the UI without reflashing
- **OTA updates** — update firmware over Wi-Fi via Arduino OTA
- **Buzzer feedback** — audible alerts on boot and error states

---

## Hardware

| Component | Details |
|---|---|
| Microcontroller | ESP32 |
| Temperature sensor | MAX6675 thermocouple |
| Pressure sensor | Analog (0–12 bar), GPIO 34 |
| Heater control | SSR (Solid State Relay), GPIO 13 |
| Pump control | Dimmable light / TRIAC dimmer, GPIO 18/19 |
| Storage | SD card via SPI |
| Buzzer | GPIO 4 |

### Pin Reference

| Function | GPIO |
|---|---|
| SD CS | 32 |
| SD MOSI | 25 |
| SD MISO | 26 |
| SD SCK | 33 |
| SSR (heater) | 13 |
| Dimmer sync | 19 |
| Dimmer output | 18 |
| Thermocouple CS | 22 |
| Thermocouple CLK | 23 |
| Thermocouple DO | 21 |
| Pressure sensor | 34 |
| Buzzer | 4 |

---

## Software Dependencies

- [Arduino ESP32](https://github.com/espressif/arduino-esp32)
- [dimmable_light](https://github.com/fabianoriccardi/dimmable-light)
- [MAX6675 library](https://github.com/adafruit/MAX6675-library)
- [PID_v1](https://github.com/br3ttb/Arduino-PID-Library)
- [ArduinoJson](https://arduinojson.org/)
- [ArduinoOTA](https://github.com/jandrassy/ArduinoOTA)
- ESPmDNS (bundled with ESP32 Arduino core)

---

## Getting Started

### 1. SD Card Setup

Format the SD card as FAT32 and create a `config.json` file in the root:

```json
{
  "ssid": "your_wifi_name",
  "password": "your_wifi_password"
}
```

Place your web UI files (`index.html`, `global.css`, JS files etc.) in the root of the SD card.

### 2. Flash the Firmware

Open the project in Arduino IDE or PlatformIO, select your ESP32 board, and flash.

### 3. Access the UI

Once booted and connected to Wi-Fi, open a browser and navigate to:

```
http://discreet.local
```

Or use the IP address printed to the serial monitor.

---

## Configuration

Key variables at the top of the sketch you may want to adjust:

| Variable | Default | Description |
|---|---|---|
| `setpoint` | `102` | Target boiler temp (°C). Includes offset. |
| `offset` | `9` | Probe offset. If you ask for 93°C, set offset so `setpoint - offset = 93`. |
| `pressuresetpoint` | `9` | Target brew pressure (bar) |
| `PrePressureSetpoint` | `3` | Pre-infusion pressure (bar) |
| `preinftime` | `8000` | Pre-infusion duration (ms) |
| `Kp / Ki / Kd` | `48 / 8 / 50` | PID tuning values |

### Temperature Offset

The probe sits outside the boiler, so there's a difference between boiler temp and actual output temp. Tune the `offset` variable to compensate. If you request 93°C and your puck temp is off, adjust accordingly.

---

## OTA Updates

Firmware can be updated over Wi-Fi using Arduino OTA:

- **Hostname:** `Discreet`
- **Password:** `Discreet`

---

## API Endpoints

The ESP32 runs a local web server with the following endpoints:

| Endpoint | Method | Description |
|---|---|---|
| `/getValues` | GET | Returns JSON of all live values |
| `/adjust` | GET | Adjust a variable: `?var=setpoint&val=1` |
| `/pressure` | GET | Returns current pressure as plain text |
| `/listFiles` | GET | Lists files on SD card |
| `/upload` | POST | Upload a file to SD card |
| `/delete` | GET | Delete a file: `?name=filename` |
| `/applyTheme` | GET | Apply a theme: `?name=theme.css` |
| `/getCurrentTheme` | GET | Returns the currently active theme name |

### Adjustable Variables via `/adjust`

| `var` | `val` | Description |
|---|---|---|
| `setpoint` | ±int | Nudge target temperature |
| `pressuresetpoint` | ±int | Nudge target pressure |
| `preinftime` | ±int | Nudge pre-infusion time (ms) |
| `bloomtime` | ±int | Nudge bloom time (ms) |
| `steam` | — | Switch to steam mode (150°C) |
| `stopsteam` | — | Return to brew mode (102°C) |
| `Pause` | — | Cut pump power |
| `Resume` | — | Restore pump power |

---

## Notes

- The SD card and WiFi share SPI and conflict on init. The sketch intentionally initialises the SD card twice — once before WiFi to read `config.json`, and again after WiFi connects. This is a known ESP32 SPI/WiFi interaction and is handled in the setup routine.
- Pressure is only read during an active shot (AC detected). At idle, pressure reads as 0.
- The pressure sensor reads 0 for the first 500ms of a shot to allow initial pressure transients to settle.

---

## License

This project is licensed under the [GNU General Public License v3.0](LICENSE).

You are free to use, modify, and distribute this project, but any derivative work must also be released under GPL v3.
