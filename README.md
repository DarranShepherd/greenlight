# Greenlight

Greenlight is a glanceable kitchen display for Octopus Agile electricity prices, built on ESP-IDF 6, LVGL 9, and the 2.8" Cheap Yellow Display (ESP32-2432S028R class hardware).

The device is intended to answer one question instantly: is now a good time to use electricity, and if not, when does it get better?

## Product intent

Greenlight should make half-hourly time-of-use pricing understandable at a glance from across the room.

The primary screen prioritizes the current price band, how long that band remains active, and the next three grouped periods so a user can quickly decide whether to run appliances now or wait.

Secondary screens provide:

- a more detailed day view with histograms and summary statistics
- an on-device settings flow for Wi-Fi, tariff region, and brightness

## Scope for v1

Version 1 will support:

- on-device Wi-Fi onboarding only
- WPA PSK entry on the touchscreen
- storing Wi-Fi credentials in NVS for automatic reconnect after reboot
- region selection for Octopus Agile using region code plus region name
- time synchronization using NTP with timezone set to `Europe/London`
- Octopus Agile tariff download using raw VAT-inclusive unit rates
- negative prices treated as `Super Cheap`
- startup tariff fetch with tariff data held in RAM only
- an offline screen when tariff data cannot be loaded
- automatic polling for tomorrow's prices every 5 minutes after 16:00 until available
- adjustable display brightness from settings

Version 1 will not yet support:

- Octopus account authentication
- API key or OAuth onboarding
- per-user tariff auto-discovery
- remote configuration outside the device UI

## UI concept

The app uses three horizontal swipeable screens.

### 1. Primary screen

This follows the top-left mockup.

It shows:

- current unit price in p/kWh
- current price band label
- how long until the current band ends
- the clock time when the next band starts
- the next three grouped future blocks

Grouped blocks are formed by merging adjacent 30-minute tariff slots that fall into the same band.

Banding rules:

- `Super Cheap`: `<= 5p`, including negative prices
- `Cheap`: `> 5p` and `<= 15p`
- `Normal`: `> 15p` and `<= 25p`
- `Expensive`: `> 25p` and `<= 40p`
- `Very Expensive`: `> 40p`

Visual direction:

- `Cheap`, `Normal`, and `Expensive` use green, amber, and red background coding
- `Super Cheap` and `Very Expensive` should be more visually forceful on the current block than the other states
- for those extreme bands, the current block can increase foreground contrast and animate the status icon subtly

### 2. Detail screen

This follows the bottom-middle mockup.

It shows:

- today's histogram of 30-minute prices
- min, average, and max price for the visible day
- tomorrow's histogram and summary when tomorrow data becomes available

Behavior:

- before tomorrow's prices are published, show a single-day layout
- once tomorrow's prices are available, switch automatically to a two-day side-by-side layout

### 3. Settings screen

This screen contains:

- Wi-Fi SSID selection from scan results
- PSK entry using an on-screen keyboard
- connection status and retry feedback
- tariff region dropdown showing code and region name
- brightness control
- refresh or sync status details if space allows

## Data source

Greenlight will consume the public Octopus Energy API.

For v1, the tariff is determined by the selected region rather than account lookup. The implementation should use the Agile tariff endpoint that returns half-hourly unit rates for the selected product and region, using the VAT-inclusive field for all user-facing calculations.

Initial target assumptions:

- product family: Octopus Agile public tariff data
- region: `B`
- timezone handling: local display in `Europe/London`

The code should be structured so that product code selection can later be moved behind a configuration layer or account-derived tariff lookup.

## Functional requirements

### Startup and connectivity

1. Initialize display, touch, LVGL, and non-volatile storage.
2. Load persisted settings from NVS.
3. If Wi-Fi credentials are available, attempt automatic reconnect.
4. If Wi-Fi is not configured or connection fails, show the settings screen and prompt for onboarding.
5. After Wi-Fi connection, synchronize time with NTP.
6. Do not enter the main tariff UI until local time is valid.
7. Fetch tariff data during startup before entering the main tariff UI.
8. If startup tariff fetch fails, show an offline screen rather than stale tariff data.

### Tariff refresh behavior

1. Fetch today's Agile prices after time sync.
2. Determine the current slot from local time.
3. If tomorrow's data is already available, fetch and merge it into the working set.
4. After 16:00 local time, if tomorrow is not yet available, poll every 5 minutes until it is.
5. Hold the active tariff dataset in RAM only.
6. If refresh fails before any successful load, show an offline screen.
7. If a later refresh fails after a successful load, keep showing the in-memory dataset and indicate that live refresh is unavailable.

### Price grouping behavior

1. Convert half-hour slots into display blocks by collapsing adjacent slots in the same band.
2. Identify the block containing the current local time.
3. Show the next three blocks following the current one.
4. Calculate the remaining time in the current block and display it in human-readable form.

## Proposed software architecture

The current repository is still a hardware bring-up scaffold. The application should be split into small modules with clear ownership rather than keeping behavior in `main.c`.

Suggested modules:

- `app_main`: startup orchestration and task launch
- `settings_store`: NVS-backed persistence for Wi-Fi credentials, region, and brightness
- `wifi_manager`: scanning, connect or reconnect flow, event handling, and status reporting
- `time_manager`: SNTP setup, timezone configuration, and time-valid checks
- `octopus_client`: HTTPS requests, JSON parsing, and normalized tariff-slot output
- `tariff_model`: price-band classification, contiguous-block grouping, summary stats, and current/next block derivation
- `ui_router`: swipe navigation between primary, detail, and settings screens
- `ui_primary`: glanceable current-price layout
- `ui_detail`: histogram and min or avg or max presentation
- `ui_settings`: Wi-Fi onboarding, region picker, and brightness controls
- `sync_controller`: periodic refresh scheduling, tomorrow polling after 16:00, and offline-state transitions

## Data model

Suggested internal model shape:

- `tariff_slot`
	- start time UTC
	- end time UTC
	- local start and end display times
	- price including VAT in p/kWh
	- derived band enum
- `tariff_block`
	- start time
	- end time
	- band enum
	- min and max price within the block
	- representative display price for the current block and previews
- `tariff_day_summary`
	- date
	- slot count
	- min
	- avg
	- max
- `runtime_tariff_state`
	- today's slots
	- tomorrow's slots when available
	- fetch status
	- last successful refresh timestamp in RAM
- `app_settings`
	- Wi-Fi SSID
	- Wi-Fi PSK
	- selected region code
	- brightness percent

## Persistence plan

To minimize flash wear on cheap hardware, only durable user settings should be stored in NVS.

Recommended storage group:

- `settings`
	- SSID
	- PSK
	- region code
	- brightness

Tariff payloads and refresh metadata should remain in RAM only. If the device boots without network access or cannot load tariffs successfully, it should show an offline screen instead of displaying persisted tariff data.

## Networking and API notes

The implementation will need:

- Wi-Fi station mode with active scan support
- TLS-capable HTTP client for Octopus API access
- JSON parsing for tariff payloads
- SNTP for time synchronization

Likely ESP-IDF facilities:

- `esp_wifi`
- `esp_netif`
- `esp_event`
- `esp_http_client`
- `esp_tls`
- `esp_sntp`
- `nvs_flash`
- `cJSON` or another small JSON parser already suitable for ESP-IDF

## Delivery plan

### Phase 1: foundation

- replace the demo UI with a screen router and application state model
- add NVS initialization and settings persistence
- add brightness persistence and backlight control integration

### Phase 2: onboarding and time

- implement Wi-Fi scan and join UI
- add on-screen keyboard flow for PSK entry
- persist credentials and reconnect automatically on boot
- add SNTP sync and London timezone handling

### Phase 3: Octopus data pipeline

- fetch and parse Agile tariff data for the selected region
- keep the active tariff dataset in RAM only
- classify prices into bands and group them into contiguous display blocks
- add startup offline handling and live refresh failure handling

### Phase 4: primary screen

- build the main glanceable card based on the grouped current block
- show remaining time in current band and next three blocks
- add prominent treatment for `Super Cheap` and `Very Expensive`

### Phase 5: detail and settings screens

- build the histogram screen for today and tomorrow
- add min, avg, and max summaries
- complete the settings screen with region dropdown and brightness control
- wire horizontal swipe navigation across all three screens

### Phase 6: polish

- animate only where it improves legibility
- tune refresh timing, retry behavior, and offline or live-refresh indicators
- test edge cases around DST transitions, missing tomorrow data, and negative prices

## Development environment

Open this folder in the provided dev container. The container is based on `espressif/idf:v6.0`, so `idf.py` is available immediately.

Build:

```bash
idf.py set-target esp32
idf.py build
```

Flash:

```bash
ls /dev/ttyUSB* /dev/ttyACM* 2>/dev/null
idf.py -p /dev/ttyUSB0 flash monitor
```

## Current hardware assumption

This repository is initialized for the common CYD pinout used by the ESP32-2432S028R with:

- 240x320 ILI9341 TFT
- XPT2046 resistive touch controller
- LVGL 9 via `esp_lvgl_port`

If your board variant differs, update `main/hardware.h` and the panel or touch setup accordingly.
