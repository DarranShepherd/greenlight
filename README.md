# Greenlight

Greenlight is a glanceable kitchen display for Octopus Agile electricity prices, built on ESP-IDF 6, LVGL 9, and the 2.8" Cheap Yellow Display (ESP32-2432S028R class hardware).

The device is intended to answer one question instantly: is now a good time to use electricity, and if not, when does it get better?

## Current status

Phase 7 of the delivery plan is complete in firmware and has been hardware-tested on the target CYD board.

Implemented through phase 7:

- the demo UI has been replaced with a three-screen horizontal router
- startup now initializes NVS and loads persisted app settings
- brightness is applied on boot and can be changed from the settings screen
- brightness changes are persisted in the `settings` NVS namespace
- the settings screen can scan nearby Wi-Fi networks and accept credentials on-device
- successful Wi-Fi joins are persisted in NVS for reconnect on reboot
- startup now attempts Wi-Fi reconnect automatically when saved credentials exist
- SNTP time sync now runs after Wi-Fi connection and presents London local time on-device
- touch calibration now runs on-device, stores an affine calibration in NVS, and is applied on boot
- Wi-Fi join failures now report the underlying disconnect reason instead of collapsing everything into a generic timeout
- startup now fetches Octopus Agile tariff data after time sync and keeps the active dataset in RAM only
- tariff prices are classified into `Super Cheap`, `Cheap`, `Normal`, `Expensive`, and `Very Expensive` bands
- adjacent slots in the same band are grouped into contiguous display blocks for the current and upcoming periods
- startup tariff failures now surface an offline state instead of pretending stale data is valid
- later refresh failures keep the last in-memory dataset and mark live refresh as unavailable
- the firmware now discovers the currently active public Agile product code instead of depending on an expired hardcoded tariff identifier
- the primary screen now renders a glanceable current-band card with live price, time remaining, next change time, and the next three grouped future blocks
- `Super Cheap` and `Very Expensive` current periods now receive stronger visual treatment than the other bands, including a subtle animated indicator
- the phase 4 primary screen has been iterated on-device to tighten the hero layout, center the status indicator, improve top-bar alignment, and make the preview cards denser and easier to read from a distance
- the detail screen now renders per-day tariff histograms with min, average, and max summaries for today and, when published, tomorrow
- the detail screen now matches the primary route styling with a black background, compact top bar, and tariff-band colors reused in the histogram bars and summary values
- the today histogram now includes a vertical current-time marker
- when tomorrow prices are only partially published by Octopus, the chart now preserves the full `0-24` axis and leaves the unpublished trailing hours empty rather than stretching the returned bars to fill the width
- the settings screen now matches the routed UI styling with a black background, compact top bar, and disconnected Wi-Fi strike-through treatment
- the tariff region control is now first-class on-device, shows code plus region name, persists immediately, and triggers a fresh Agile reload for the selected region
- touch calibration now lives in its own dedicated settings card separate from the Wi-Fi onboarding controls
- the heavier phase 5 LVGL scene required increasing `CONFIG_ESP_MAIN_TASK_STACK_SIZE` and `CONFIG_MAIN_TASK_STACK_SIZE` to `8192`
- boot now holds on a branded splash screen until startup is resolved instead of dropping straight into the routed UI
- the Ampernomics logo is now rendered on-device during boot from the repository SVG via a generated LVGL image asset
- startup status text now reports Wi-Fi reconnect, London time sync, and tariff download progress before route handoff
- the splash layout now reserves a fixed two-line status area so long SSIDs or wrapped startup messages do not shift the logo vertically

Remaining work before the routed shell is functionally complete for v1:

- the primary route exposes the intended glanceable layout for current and upcoming grouped periods
- the detail route exposes the intended histogram view for today and tomorrow, including single-day fallback before tomorrow is published

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

Current firmware treatment:

- a compact top bar with clock, centered title, and Wi-Fi status
- a primary hero card with large current price, centered band indicator, and remaining-time summary
- three dense preview cards that prioritize upcoming block times and representative prices

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
- if Octopus has only published part of tomorrow, keep the chart scaled to the full day and leave the unpublished hours empty on the right-hand side

### 3. Settings screen

This screen contains:

- brightness control
- tariff region selector showing code and region name
- Wi-Fi SSID selection from scan results
- PSK entry using an on-screen keyboard
- connection status and retry feedback
- time sync status details

Target layout for the final settings pass:

- black background with the same top bar treatment as the other routes
- current time at top left, centered title, and Wi-Fi status icon at top right
- the Wi-Fi icon should show a strike-through treatment when disconnected
- remove explanatory filler copy and keep only the controls and concise state text
- order the sections as brightness, region, Wi-Fi, then time sync information

Current firmware treatment:

- a compact top bar with clock, centered title, and Wi-Fi status icon matching the other routes
- brightness, region, Wi-Fi, touch calibration, and time sync each live in compact dark cards
- the region control cycles through Octopus region codes and names and triggers an immediate tariff refresh for the newly selected region
- touch calibration status and action are separated from Wi-Fi onboarding so calibration can be revisited without scrolling through network controls

## Data source

Greenlight will consume the public Octopus Energy API.

For v1, the tariff is determined by the selected region rather than account lookup. The implementation uses the Agile tariff endpoint that returns half-hourly unit rates for the active public import product and selected region, using the VAT-inclusive field for all user-facing calculations.

The public API does not always return a full 48 half-hour slots for tomorrow as soon as the next day appears. In hardware testing on 2026-05-13, the latest published slot ended at `2026-05-14 23:00`, so the firmware now treats missing late-evening slots as unpublished data rather than compressing the histogram.

Initial target assumptions:

- product family: Octopus Agile public tariff data
- region: `B`
- timezone handling: local display in `Europe/London`

The code should be structured so that product code selection can later move behind a configuration layer or account-derived tariff lookup, even though the current firmware already discovers the active public Agile product at runtime.

## Functional requirements

### Startup and connectivity

1. Initialize display, touch, LVGL, and non-volatile storage.
2. Load persisted settings from NVS.
3. Show a splash screen on boot with the Ampernomics logo centered and a status line underneath describing startup progress.
4. If Wi-Fi credentials are available, attempt automatic reconnect while the splash screen reports state changes such as connecting to Wi-Fi, synchronizing time, and downloading prices.
5. If Wi-Fi is not configured or connection fails, switch to the settings screen and prompt for onboarding.
6. After Wi-Fi connection, synchronize time with NTP.
7. Do not enter the main tariff UI until local time is valid.
8. Fetch tariff data during startup before entering the main tariff UI.
9. When startup completes successfully, enter the primary UI route.
10. If startup tariff fetch fails, show an offline screen rather than stale tariff data.

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
- `app_settings`: NVS-backed persistence for Wi-Fi credentials, region, and brightness
- `app_state`: runtime UI and application state shared across modules
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
- a small JSON parser suitable for ESP-IDF

## Delivery plan

### Phase 1: foundation

- complete: replace the demo UI with a screen router and application state model
- complete: add NVS initialization and settings persistence
- complete: add brightness persistence and backlight control integration

### Phase 2: onboarding and time

- complete: implement Wi-Fi scan and join UI
- complete: add on-screen keyboard flow for PSK entry
- complete: persist credentials and reconnect automatically on boot
- complete: add SNTP sync and London timezone handling
- complete: add persisted touch calibration flow for the resistive panel

### Phase 3: Octopus data pipeline

- complete: fetch and parse Agile tariff data for the selected region
- complete: keep the active tariff dataset in RAM only
- complete: classify prices into bands and group them into contiguous display blocks
- complete: add startup offline handling and live refresh failure handling

### Phase 4: primary screen

- complete: build the main glanceable card based on the grouped current block
- complete: show remaining time in current band and next three blocks
- complete: add prominent treatment for `Super Cheap` and `Very Expensive`

### Phase 5: detail screen

- complete: build the histogram screen for today and tomorrow
- complete: add min, avg, and max summaries
- complete: wire horizontal swipe navigation across all three screens

### Phase 6: settings UI refresh

- complete: restyle the settings screen to match the black-background routed UI
- complete: use a compact top bar with time at left, title centered, and Wi-Fi status at right
- complete: show the Wi-Fi icon with a strike-through treatment when disconnected
- complete: remove extraneous explanatory text and keep the screen focused on controls and concise status
- complete: reorder settings content as brightness, region, Wi-Fi, then time sync information
- complete: split touch calibration into its own card while keeping it inside the settings route

### Phase 7: boot splash and startup flow

- complete: show the Ampernomics logo from `logo.svg` centered during boot
- complete: display live startup status text below the logo for steps such as Wi-Fi connect, time sync, and tariff download
- complete: reserve two lines for startup status text so wrapped messages do not move the splash lockup
- complete: transition to the primary UI route when startup completes successfully
- complete: fall back to the settings route if Wi-Fi connection fails or onboarding is required

## Implementation notes and learnings

- The CYD is happy with the LVGL tileview-based horizontal router for full-screen navigation.
- Persisted user settings are now stored in the `settings` NVS namespace, with brightness already wired end to end.
- Touch calibration has to be solved in the same pre-rotation coordinate space where the XPT2046 driver callback applies it; solving in rotated LVGL display space produces skewed taps.
- The current firmware no longer fits the default small app slot and now depends on `CONFIG_PARTITION_TABLE_SINGLE_APP_LARGE`.
- On this hardware, nested flex layouts plus scrollable content inside the settings tile were less predictable than expected once rotation was applied.
- For phase 1, the settings controls render more reliably as a compact, explicit layout rather than depending on deeper nested auto-layout and long copy.
- If a screen must be scrollable, its parent container height still needs to fully account for any absolutely positioned children or the lower content can become unreachable.
- Public Octopus Agile product codes do roll forward over time, so the firmware now discovers the current active import product instead of treating a historical product code as stable.

### Phase 8: polish

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

Generated assets:

```bash
python3 tools/generate_assets.py
```

This regenerates the embedded splash asset and the custom LVGL subset fonts. CI runs this step before the firmware build and fails if the committed generated files are stale.

Local debug build with assertions, perf monitor, and runtime GDB stub enabled:

```bash
SDKCONFIG=sdkconfig.debug \
SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.debug.defaults" \
idf.py build
```

Documentation screenshot build:

```bash
idf.py -B build-docs \
	-DSDKCONFIG=sdkconfig.docs-screenshots \
	-DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.docs-screenshots.defaults" \
	build flash

python3 -m pip install pyserial
python3 tools/capture_doc_screenshots.py --port /dev/ttyUSB0 --reset-on-connect
```

This profile keeps the normal firmware path untouched. It uses an isolated `build-docs/` directory and generates a local `sdkconfig.docs-screenshots` file from the checked-in `sdkconfig.defaults` and `sdkconfig.docs-screenshots.defaults` inputs, boots into a deterministic documentation-only mode, renders curated primary and detail tariff examples, and streams four LVGL RGB565 snapshots over UART as framed `GLSHOT` records. The host script can reset the board after opening the port, reassembles the streamed slices, and writes PNG files to `docs/generated/screenshots/` by default.

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
