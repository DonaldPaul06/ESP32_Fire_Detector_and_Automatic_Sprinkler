# ESP32 Fire Detector + Automatic Sprinkler

An ESP32-based fire monitor that watches temperature, smoke/air quality, and (optionally) a flame sensor. When a fire is detected it switches on a relay-driven sprinkler pump, sounds a buzzer, and exposes a small web dashboard for live status, testing, and resetting.

Detection and sprinkler control run independently of WiFi, so the system keeps protecting even if the network is down.

> **Safety notice:** This is a hobby/demo project. It is **not** a certified fire-safety device and must not be relied on as your only protection. Always keep proper smoke alarms and extinguishers, and be careful when combining mains-powered pumps with water.

## Features

- **Multi-sensor detection:** high temperature, smoke/air-quality reading, or flame sensor triggers the alarm
- **Smoke debouncing:** smoke must exceed the limit for several consecutive readings before it counts, which avoids false triggers
- **Automatic sprinkler** with a minimum run time after the last detection and a hard maximum run time to protect the tank and pump
- **Latched alarm and buzzer:** stays active until manually reset
- **Web dashboard** with auto-refresh, a sprinkler test button, and a reset button
- **mDNS:** reachable at `http://firemonitor.local`
- **Non-blocking WiFi reconnect:** retries every 10 seconds without interrupting detection
- **Safe boot state:** the sprinkler is OFF at startup

## Hardware

| Component | Purpose | ESP32 Pin |
|---|---|---|
| DHT22 | Temperature and humidity | GPIO 4 |
| MQ135 | Smoke / air quality (analog) | GPIO 34 |
| Relay module (active-LOW) | Switches sprinkler pump | GPIO 26 |
| Active buzzer | Audible alarm | GPIO 25 |
| Flame sensor (optional) | Flame detection (digital out) | GPIO 27 |

Also needed: a water pump suited to your relay's rating, a water tank, and a suitable power supply for the pump.

### Wiring notes

- **Relay is active-LOW:** the pin is driven `LOW` to turn the sprinkler ON and `HIGH` to turn it OFF.
- **MQ135 output voltage:** the MQ135 is typically powered from 5 V, but the ESP32 ADC tolerates only 3.3 V. Use a voltage divider on the analog output.
- **MQ135 warm-up:** let the sensor warm up (several minutes, ideally longer on first use) before calibrating or trusting readings.
- **Flame sensor:** most modules output `LOW` when a flame is detected. Enable it by setting `USE_FLAME` to `1`.
- **Pump power:** power the pump from its own supply through the relay. Do not power it from the ESP32.
- **Mains safety:** if switching mains voltage, use properly rated and enclosed components, or use a low-voltage DC pump.

## Software setup

1. Install the [Arduino IDE](https://www.arduino.cc/en/software) and add ESP32 board support (Espressif's `esp32` core).
2. Install these libraries via Library Manager:
   - **DHT sensor library** by Adafruit
   - **Adafruit Unified Sensor** (dependency of the DHT library)
3. `WiFi`, `WebServer`, and `ESPmDNS` are included with the ESP32 core.
4. Open the sketch and edit your WiFi credentials:

   ```cpp
   const char* WIFI_NAME = "Your_WiFi_Name";
   const char* WIFI_PASS = "Your_WiFi_Password";
   ```

5. Select your ESP32 board and port, then upload.
6. Open the Serial Monitor at **115200 baud** to see the IP address.

## Configuration

All tunable values are `#define`s near the top of the sketch.

| Setting | Default | Description |
|---|---|---|
| `SMOKE_LIMIT` | `1800` | MQ135 raw reading that counts as smoke. **Set this above your clean-air reading.** |
| `SMOKE_CONFIRM` | `3` | Consecutive 1-second readings above the limit required to trigger |
| `TEMP_LIMIT` | `55.0` | Temperature in °C that triggers the alarm |
| `MIN_ON_MS` | `20000` | Sprinkler keeps running this long (ms) after the last detection |
| `MAX_ON_MS` | `180000` | Hard stop (ms) for sprinkler run time; triggers lockout |
| `TEST_MS` | `10000` | Duration (ms) of a manual test run |
| `USE_FLAME` | `0` | Set to `1` if a flame sensor is connected |

If you use a DHT11 instead of a DHT22, change `DHT dht(DHT_PIN, DHT22);` to `DHT11`.

### Calibrating the smoke threshold

1. Let the MQ135 warm up in clean air.
2. Open the dashboard (or print `smoke` to Serial) and note the typical reading.
3. Set `SMOKE_LIMIT` comfortably above that value, then test with a safe smoke source (for example, a snuffed-out match) held near the sensor.

## How it works

1. **Sensing:** smoke is sampled once per second (averaged over 20 ADC readings); temperature and humidity are read every 2 seconds.
2. **Fire condition:** fire is declared if any of these is true:
   - flame sensor active (when enabled)
   - temperature above `TEMP_LIMIT`
   - smoke above `SMOKE_LIMIT` for `SMOKE_CONFIRM` consecutive readings
3. **Response:** on fire, the alarm latches, the buzzer sounds, and the sprinkler turns on. The sprinkler stays on for at least `MIN_ON_MS` after the last detection.
4. **Protection:** if the sprinkler has been running longer than `MAX_ON_MS`, it is shut off and a lockout is set until the fire condition clears (or you press reset).
5. **Sensor fault:** if the DHT22 returns an error, the temperature check is skipped and the dashboard shows "DHT22 error". Smoke and flame detection still work.

## Web interface

Once connected, open the IP address printed in the Serial Monitor, or `http://firemonitor.local`.

| Route | Action |
|---|---|
| `/` | Dashboard (auto-refreshes every 3 seconds) |
| `/test` | Runs the sprinkler for `TEST_MS` |
| `/reset` | Stops the sprinkler, clears the latched alarm and lockout, silences the buzzer |

Dashboard status banners:

- 🟥 **FIRE DETECTED:** active detection
- 🟧 **ALARM (cleared, press RESET):** fire no longer detected but alarm is still latched
- 🟩 **All clear**

## Limitations and notes

- **No authentication:** anyone on your network can open `/test` or `/reset`. Keep the device on a trusted network, or add authentication before exposing it more widely.
- **Plain HTTP:** the dashboard is not encrypted.
- **MQ135 is not a dedicated smoke detector:** it measures general air quality and drifts with temperature, humidity, and age. Expect to recalibrate.
- **Hardcoded credentials:** avoid committing real WiFi credentials to a public repository.
- **Local only:** there is no remote or push notification support. Consider adding MQTT, Telegram, or similar if you need alerts when you are away.

## Troubleshooting

| Problem | Likely cause / fix |
|---|---|
| Sprinkler is ON at boot or inverted | Check whether your relay module is active-LOW or active-HIGH and adjust `setSprinkler()` |
| Constant false smoke alarms | `SMOKE_LIMIT` is too low; sensor not warmed up; recalibrate in clean air |
| "DHT22 error" on dashboard | Check wiring, pull-up resistor, and that the correct sensor type is selected |
| Can't reach `firemonitor.local` | mDNS may be unsupported on your OS or network; use the IP from the Serial Monitor |
| Sprinkler stops with "Max run time reached" | `MAX_ON_MS` hit; check the tank and press RESET |

## License

Add your preferred license here (for example, MIT).
