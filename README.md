# DeskBot (DeskBuddy) 🤖

An interactive ESP32-powered desktop companion featuring an OLED display, tactile navigation buttons, and acoustic buzzer feedback.

---

## 1. Hardware Pinout Reference (Dual OLED + 3 Buttons)

### Dual OLED Displays (Option A: Dual Hardware I2C - No Soldering)

| Component | ESP32 Pin | I2C Bus | Notes |
| :--- | :--- | :--- | :--- |
| **Left OLED (SDA)** | **GPIO 21** | `Wire` (Bus 0) | Dedicated Left Eye (Address `0x3C`) |
| **Left OLED (SCL)** | **GPIO 22** | `Wire` (Bus 0) | Default Hardware I2C |
| **Right OLED (SDA)** | **GPIO 18** | `Wire1` (Bus 1) | Dedicated Right Eye / Menu (Address `0x3C`) |
| **Right OLED (SCL)** | **GPIO 19** | `Wire1` (Bus 1) | Secondary Hardware I2C |

### Tactile Navigation Buttons & Audio

| Component | ESP32 Pin | Function | Notes |
| :--- | :--- | :--- | :--- |
| **Button 1 (Menu / Next)** | **GPIO 32** | Digital Input | Internal pull-up (`INPUT_PULLUP`) |
| **Button 2 (Select / OK)** | **GPIO 33** | Digital Input | Internal pull-up (`INPUT_PULLUP`) |
| **Button 3 (Back / Mood)** | **GPIO 27** | Digital Input | Internal pull-up (`INPUT_PULLUP`) |
| **Buzzer** | **GPIO 26** | PWM / Digital Out | Clicks, chimes, alarms, melodies |

---

## 2. Project Directory Structure

```text
deskbuddy/
├── README.md               # Hardware specs, wiring & setup notes
├── deskbuddy.ino           # Main Arduino / ESP32 firmware sketch
└── ollie_ble_reference/
    └── ollie_ble_reference.ino # Standalone Sphero Ollie BLE connection sketch
```

---

## 3. Planned Features & Roadmap

- [x] **Base UI & Menu Navigation** (3-button navigation, non-blocking audio)
- [x] **Companion Mode & Animated Expressions** (Pixel eyes, idle blinks, mood shifts, interactive poke)
- [x] **Pomodoro Focus Timer** (15/25/45/5m presets, real-time MM:SS countdown, progress bar, alarm)
- [x] **Sphero Ollie BLE Battery Monitor** (Live battery % meter, connection state, RGB LED color controller)
- [x] **Reflex Speed Minigame** (Screen flash, random delay, millisecond reaction timer & rankings)
- [x] **Spotify Now Playing Controller** (Track & artist display, dynamic progress bar, play/pause & skip)
- [x] **Clean System Telemetry** (Clean, undistorted uptime counter, free heap RAM monitor, CPU freq)
- [ ] **Live Clock & NTP Sync** (Wi-Fi time sync, custom digital watchfaces)

---

## 4. Companion Modes & Button Guide

### 🤖 Companion Face (Dual Eyes Mode)
- **Idle Behavior:** Natural blinking (every 3–5 seconds) and glance shifts (looking left, right, happy).
- **Button 1 (GPIO 32 - Menu):** Opens the Main Menu drawer!
- **Button 2 (GPIO 33 - Select):** Also opens the Main Menu drawer!
- **Button 3 (GPIO 27 - Back/Mood):** Cycles expressions & moods (*Neutral*, *Glance Left*, *Glance Right*, *Happy `^ ^`*, *Wink*, *Sleepy `zZ`*).

### 🍅 Focus Pomodoro Timer
- **Button 1:** Cycle presets (`25m` -> `45m` -> `5m` break -> `15m`).
- **Button 2:** Start / Pause the timer.
- **Button 3:** Back to menu.
- Rings a dual-tone alarm when the timer reaches zero.

### 🔋 Sphero Ollie Battery & Controller (`2B-4FCD`)
- Displays live battery percentage meter, connection indicator, and LED color state.
- **Button 1:** Cycle Ollie RGB LED colors (*Cyan*, *Purple*, *Green*, *Red*, *Blue*).
- **Button 2:** Ping / Wake Ollie.
- **Button 3:** Back to menu.

### 🎧 Spotify Now Playing Controller
- Displays currently playing track name, artist, and active progress bar.
- **Button 1:** Skip to Next Track (`>>`).
- **Button 2:** Play / Pause toggle (`▶ / ⏸`).
- **Button 3:** Back to menu (`<`).

### ⚡ Reflex Speed Minigame
- **Button 2:** Press to start the test.
- The screen prompts *"Ready... Steady... Wait for beep!"*.
- After a randomized delay (1.8s - 4.2s), the OLED flashes inverted with an audio beep: **"PRESS [B2]!"**.
- Measures your reaction time in milliseconds with rankings:
  - `< 220 ms`: **GODLIKE!**
  - `< 320 ms`: **FAST!**
  - `> 320 ms`: **DECENT**
- Pressing too early triggers a **TOO EARLY!** false-start penalty.

### 🎵 8-Bit Jukebox
- Select and play classic sound effects: *Coin Chime*, *Level Up Fanfare*, *Alert Alarm*, *Powerup Chirp*.

---

## 5. Optional Add-On: Sphero Ollie BLE Integration

If you want to connect your ESP32 directly to your Ollie via native BLE:

* **Device Name:** `2B-4FCD`
* **MAC Address:** `ea:a4:5f:42:4f:cd`
* **Sphero Service UUID:** `22bb746f-2ba0-7554-2d6f-726568705327`

A ready-to-test standalone connection sketch is saved in [ollie_ble_reference.ino](file:///Users/shivankvibes/Projects/deskbuddy/ollie_ble_reference.ino).
