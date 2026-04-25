# Changelog

All notable changes to the COLYFLOR Weather Station project will be documented in this file.

## [2.0] - 2026-04-25
### Security
- **Secure OTA Validation:** Replaced `HTTPUpdate` with a custom stream loop to incrementally compute the SHA-256 hash during download. The update is now aborted before the boot partition switches if the binary's hash does not strictly match the ECDSA-verified manifest, preventing MITM firmware injection.
- **Manifest Hash Validation:** Added an early abort sequence if the OTA manifest is missing a valid 64-character SHA-256 hash, preventing unnecessary downloads and confusing mismatch logs.

### Fixed
- **Version Comparison:** Replaced string-based version comparison with semantic version parsing (`isVersionNewer`) to correctly evaluate multi-digit minor versions (e.g., preventing 1.10 from sorting before 1.9).
- **OTA Timeout:** Added a 300-second timeout to the OTA download stream to prevent the device from hanging indefinitely if the server stalls but keeps the TCP socket open.
- **OTA Connection Leak:** Separated the manifest check and binary download into distinct HTTPClient and WiFiClientSecure scopes to prevent TLS state corruption and potential hardware panics.
- **OTA Partition Swap:** Explicitly passed `true` to `Update.end()` to ensure the ESP32-C6 reliably commits the partition swap across different Arduino core versions.

## [1.91] - 2026-04-21
### Fixed
- **Alerts timer:** Change alerts time from 10 seconds to 5 seconds as per user's request.
- **OTA version check:** Separate major and minor version, this was signed as 1.91 so the code would go over OTA

## [1.9] - 2026-04-11

### Fixed
- **OpenWeather API IP logs:** Change OWM variables and logs medsages to make it clerar is about OpenWeather API IP address not OWN ip

## [1.8] - 2026-04-09

### Added
- **Manual OWN IP Override:** Added the `setowmip [IP]` command to manually bypass DNS and set the OpenWeatherMap API IPv4 address in NVRAM. Include format validation.
- **WiFi Network Discovery:** The Captive Portal now scans for available WiFi networks and populates them into a dropdown datalist. This simplifies setup while maintaining support for manual typing of hidden SSIDs.

### Fixed
- **Log Date Formatting:** Corrected a typo in the timestamp of serial and web console logs to properly use hyphens for dates instead of colons (e.g., `2026-04-09` instead of `2026:04:09`).

## [1.7] - 2026-03-30

### Added
- **WiFi Profile Management:** Implemented the `rmwifi [slot]` console command to allow selective deletion of saved WiFi profiles from NVRAM without needing a full system reset.
- **Framework Optimizations:** Internal stability improvements and structural groundwork for the v1.8 captive portal and IP override features.

## [1.6] - 2026-03-20

### Added
- **Multi-Alert & Pagination Engine:** The alert display system now fully supports cycling through multiple active alerts. Long alert descriptions are automatically word-wrapped and paginated, with on-screen indicators for the current alert and page number.
- **Manual Region Command:** Introduced the `setregion [region]` command to manually override the display region without a full geocode refresh.
- **System Uptime Telemetry:** The `heartbeat` command now calculates and displays hardware uptime in Days, Hours, and Minutes.
- **Password Validation:** Enforced WiFi password input validation in both the Captive Portal and CLI to strictly prevent the use of injection-sensitive characters (`<`, `>`, `"`, `'`, `\`).
- **Proactive Maintenance:** Implemented an automatic weekly hardware reboot (every 7 days) to flush heap fragmentation and ensure long-term network stack stability.
- **Dynamic Timezone System:** The timezone is now managed by storing the user's IANA timezone (e.g., `America/New_York`). On boot, the device queries the `worldtimeapi.org` web service to fetch the latest POSIX-compliant timezone string, automatically accounting for DST rule changes. The system falls back to a locally-stored value if the web service is unreachable. A new `setiana` command has been added for manual configuration.
- **Temperature Unit Selection:** Added support for Celsius and Fahrenheit. The unit can be selected in the Captive Portal and changed via the `setunit [C|F]` command. API calls and the display are updated accordingly.

### Fixed
- **UTF-8 Web Console:** Added explicit UTF-8 encoding to the Web Console HTML head, form submission, and HTTP headers to properly display and process special characters (e.g., degree symbols, accents).
- **Alert UI State:** Fixed a bug where the display could get stuck on the red alert screen after an alert expires or `testalert off` is executed.
- **LCD Flicker:** Optimized the main loop to eliminate a constant 10-second screen refresh, preventing a noticeable flicker when no alerts are active.
- **Web Console Stability:** Refactored the web console handler to use chunked transfer encoding, resolving memory-related crashes when the serial log becomes large.
- **Geocoding Encoding:** Resolved an issue where UTF-8 characters were improperly converted, breaking OpenWeather API requests for international cities.
- **Setup Mode Commands:** Fixed an issue where `reboot` and `reset` commands were ignored while the device was in the Captive Portal state.
- **Setup Diagnostics:** The device now utilizes `WIFI_AP_STA` mode if an existing connection is present during setup, preserving web console access for debugging.
- **Intelligent Probing:** Refined network diagnostics to prevent false recovery loops when non-critical endpoints (e.g., Time API) are blocked by local DNS sinkholes like Pi-hole.
- **Log formatting:** Corrected serial logging to accurately reflect the user-selected temperature unit (C or F).

## [1.5] - 2026-03-15

### Added
- **Manual Coordinate Command:** Introduced the `setcoords [lat] [lon]` command to allow direct setting of latitude and longitude, bypassing the need for city-based geocoding.
- **Enhanced Logging:** 
    - Geocoding success messages now include the fetched region (e.g., state and country).
    - Weather updates now log the current temperature, sky condition, and the OpenWeatherMap API version being used.

### Changed
- **Web Console Logo:** The logo in the web console is now generated from the embedded `logo.h` data and served as a Base64-encoded BMP data URI, removing the external dependency on `logo.png`.

### Fixed
- **Web Console UI:** Corrected the help text for console commands to use `[]` instead of `<>` for arguments, preventing HTML rendering issues.

## [1.4] - 2026-03-16

### Added
- **API 3.0 Integration:** Implemented OpenWeatherMap One Call API 3.0 support with automatic HTTP 401/404 downgrade fallback to API 2.5.
- **Alert UI Paging:** Added time-multiplexed rendering state machine to alternate between forecast data and government alerts at 10-second intervals. Includes custom word-wrapping algorithm for alert descriptions.
- **Deferred Execution Machine:** Implemented non-blocking state flags for disruptive commands (`reboot`, `reset`, `recover`) to guarantee HTTP 303 redirects execute prior to network stack termination.
- **Targeted WiFi Scanner:** Replaced `WiFiMulti` with an explicit `connectTargetedWiFi` function mapping local APs to NVS slots.
- **Linux-Style Console:** Inverted string concatenation and injected JavaScript to auto-scroll web logs bottom-to-top.
- **Dual DNS Probing:** Expanded `runDeepProbe` to sequentially resolve both OpenWeatherMap and primary manifest domain endpoints prior to TCP routing checks.
- **Console Commands:** Added `clear` (ANSI escape/RAM wipe), `version`, `addwifi`, `testalert`, and `setapiver`.

### Changed
- **Heartbeat Telemetry:** Appended CPU die temperature, minutes to next scheduled API fetch, and minutes to next OTA check.
- **Status Telemetry:** Added NVS capacity utilization percentage and synchronous `runDeepProbe` pass/fail output.
- **Network Telemetry:** Added active DNS server IP output to the `net` command.
- **NVRAM Dump:** Modified `nvram` to output the full 32-character API key and all populated SSID slots. Password output is strictly suppressed.

### Fixed
- **Command Feedback:** Restored explicit `logMsg` execution confirmations for `weather`, `setcity`, `settz`, and `setapi`.

---

## [1.3] - 2026-03-12

### Added
- **Binary Integrity Pre-Check:** The OTA engine now retrieves the expected binary size from the manifest and compares it against `ESP.getFreeSketchSpace()` before initiating the download. This prevents "out-of-space" crashes during flash.
- **Extended Security Telemetry:** Added detailed logging prefixes (`Security ->`, `Integrity ->`, `Network ->`) to the Serial and Web Console to better distinguish between connection issues and cryptographic failures.
- **Enhanced Signing Utility:** Updated `sign.py` to automatically calculate and inject the binary file size into the manifest.
- **Intelligent Version Comparison:** Implemented `compareVersion` logic to prevent accidental firmware downgrades.
- **OTA Telemetry Suite:** Added high-granularity logging for manifest URL, HTTP response codes, and local vs. remote version strings.
- **Reboot Notification:** Added clear final log message prior to software restart.

### Fixed
- **Download Lifecycle Logging:** Resolved an issue where a failed download might not report the specific HTTP or Flash error code to the console.
- **NVS Entry Mapping:** Optimized NVS entry retrieval to ensure consistent behavior across different ESP32 partition schemes.
- **Downgrade Logic:** Prevented older firmware versions from being installed automatically.
- **NTP Initialization:** Fixed LCD clock showing `--:--` by ensuring time refresh occurs before UI rendering.

---

## [1.2] - 2026-03-09

### Added
- **Manual Time Synchronization:** Integrated the `sync` command.
- **POST-Redirect-GET Pattern:** Implemented 303 redirects on the `/cmd` endpoint to prevent command re-submission.
- **Root URL Redirect:** Configured a 301 redirect from `/` to `/console`.
- **LCD Console Banner:** Restored the "Console: http://IP" telemetry message for 5 seconds during boot.

---

## [1.1] - 2026-03-03

### Added
- **Captive Portal Provisioning:** Web-based setup via `COLYFLOR_SETUP` AP.
- **NVRAM Storage:** Migration of settings to flash memory via `Preferences.h`.
- **Multi-AP Support:** Automatic evaluation of up to 5 WiFi networks.
- **Input Sanitization:** Stripping of escape sequences and HTML tags from user inputs.

---

## [1.0] - Initial Release
- Hardware integration for ST7789 TFT.
- OpenWeatherMap API parsing.
- ECDSA-secured OTA pipeline with signature verification.
- Night mode backlight PWM dimming.