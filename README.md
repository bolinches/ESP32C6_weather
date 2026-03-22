## ESP32-C6 Weather Station

An autonomous, secure weather monitoring station powered by an ESP32-C6. Features a "Feels Like" temperature display, 4-line hourly forecast, API 3.0 alert paging, captive portal configuration, and an encrypted OTA update system.

## 🚀 Serial & Web Interactive Dashboard

The firmware includes a non-blocking command processor accessible via the Arduino Serial Monitor (**115200 Baud**, Newline termination) or the local Web Console (`http://<DEVICE_IP>/console`).

| Command | Action |
| --- | --- |
| `help` | Shows the command list |
| `status` | Probes network routing, DNS, and NVS capacity |
| `version` | Prints firmware version |
| `heartbeat` | Outputs RSSI, CPU temp, heap, scheduling metrics, and uptime |
| `recover` | Forces tiered network recovery sequence |
| `ota [force]` | Checks and applies OTA (`force` bypasses version check) |
| `weather` | Forces immediate API fetch and UI refresh |
| `net` | Shows SSID, RSSI, IP, GW, and DNS server |
| `heap` | Displays free heap memory |
| `nvram` | Dumps stored preferences (excludes passwords) |
| `addwifi` | Syntax: `addwifi [ssid] [pass]` (Adds/updates WiFi slot) |
| `reset` | Wipes NVRAM and executes hardware reboot |
| `dim` | Overrides backlight to night threshold |
| `bright` | Overrides backlight to day threshold |
| `auto` | Restores schedule-based backlight logic |
| `reboot` | Executes software restart |
| `clear` | Clears terminal history and RAM buffer |
| `setapiver`| Syntax: `setapiver [2.5 / 3.0]` (Sets target OpenWeather API) |
| `testalert`| Syntax: `testalert [on / off]` (Forces alert state UI) |
| `setcity` | Updates city string and executes geocoding |
| `setregion`| Syntax: `setregion [region]` (Manually sets region string) |
| `setcoords` | Syntax: `setcoords [lat] [lon]` (Manually sets coordinates) |
| `settz` | Updates POSIX timezone and forces NTP sync |
| `setunit` | Syntax: `setunit [C/F]` (Sets temperature unit) |
| `setapi` | Updates OpenWeather API key |

---

## 🛰 System Features

* **API 3.0 & Alert Paging:** Supports OpenWeather One Call 3.0 with automatic API 2.5 downgrade fallback. Utilizes a time-multiplexed state machine to render text-wrapped government weather alerts at 10-second intervals.
* **Captive Portal Configuration:** Hardcoded credentials removed. Device broadcasts `COLYFLOR_SETUP` AP on initial boot or network failure for web-based provisioning.
* **Dynamic Geocoding & NVRAM:** Translates user-input city to Lat/Lon coordinates via OpenWeather API and stores preferences in non-volatile flash memory.
* **Multi-AP Target Scanning:** Stores up to 5 WiFi networks in NVS. Executes explicit environment scans and sequential targeted connections to matched SSIDs.
* **Intelligent Network Probing:** Deep diagnostic probes enforce strict validation on OpenWeatherMap and primary TCP routing, while executing passive diagnostic-only checks on secondary OTA and Time services to prevent false recovery loops.
* **Deferred Command Execution:** Hardware-disruptive commands (reboot, reset) utilize a delayed state machine to guarantee HTTP 303 redirects execute prior to TCP stack termination.
* **Semantic OTA Versioning:** Parses manifest versions to prevent accidental downgrades unless explicitly forced. Includes binary size integrity pre-checks.
* **Input Sanitization:** Web and Serial inputs are stripped of escape characters and HTML tags to prevent injection. Web logs utilize bottom-to-top auto-scrolling.
* **4-Line Forecast:** Displays +3H, +6H, +9H, and +24H forecast data.
* **Smart Polling:** Randomized update intervals (15–45 min) to optimize API usage and mitigate rate limiting.
* **Self-Healing:** Monitors weather fetch streaks; executes tiered recovery (Targeted Rescan -> Radio Toggle -> Reboot).
* **Secure Pull OTA:** ECDSA (NIST P-256) signature verification for firmware binaries prior to installation.
* **Adaptive LCD Backlight:** Configurable PWM duty cycles mapped to local POSIX time thresholds (`config.h`).

---

## ⚠️ Known Limitations

* **WiFi Network Compatibility:** The device **cannot** connect to public WiFi networks that require a web-based Captive Portal login (e.g., hotels, airports, cafes). It requires standard WPA/WPA2/WPA3 Personal authentication.
* **SSID & Password Characters:** Due to strict input sanitization to prevent web injection, your WiFi SSID and Password **cannot** contain the following characters: `<`, `>`, `"`, `'`, `\`. Using these characters will cause connection failures or configuration errors.

---

## 🛠 Hardware Specifications

* **Microcontroller:** [ESP32-C6 1.47" LCD Development Board](https://www.amazon.com/ESP32-C6-Development-Single-Core-Processor-Frequency/dp/B0DK5J6LX3/)
* **Display:** 1.47-inch IPS LCD (ST7789 Driver, 172x320 Resolution)
* **Pinout:**
  * **Backlight:** 22 (PWM)
  * **SPI:** SCK (7), MOSI (6), CS (14), DC (15), RST (21)
* **Partition scheme:** Minimal SPIFFS 19MB APP with OTA
* **Enclosure:** 3D Printed (OpenSCAD designed Case + Asymmetrical Bezel)
* **Better:** ESP32-C6-Display Case from Daniel https://makerworld.com/en/models/1628925-esp32-c6-display-case
* **Best:** ESP32 C6 with LCD Screen Enclosure Case from Adrian https://makerworld.com/en/models/2121443-esp32-c6-with-lcd-screen-enclosure-case

---

## 🔐 Security & OTA (Secure Pull)

The station performs a secure check for firmware updates via HTTPS. Authenticity and integrity are verified using **NIST P-256 ECDSA** signatures.

### 1. The Signing Process

To ensure the device only accepts valid firmware, we use a "Handshake" signing method. Instead of signing the entire binary, we sign a string containing the version and the binary's SHA-256 hash.

**Handshake Format:** `VERSION|SHA256_HASH` (e.g., `1.9|37a748...`)

#### Requirements:

* Python 3.x
* `cryptography` library (`pip install cryptography`)
* `private.pem` (Your NIST P-256 private key)

#### Signing with `sign.py`:

Run the provided signing script against your compiled `.bin` file:

```bash
python3 sign.py ~/path/to/weather.ino.bin
