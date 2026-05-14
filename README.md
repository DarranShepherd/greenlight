# Greenlight by Ampernomics

![Greenlight primary screen during a normal-price period](docs/img/primary-normal.png)

Greenlight is a companion project from [Ampernomics](https://ampernomics.com), built around the same goal: helping people understand whether Agile pricing is actually working in their favour.

## What Greenlight Does

- Shows the current Agile unit price in p/kWh.
- Classifies the current slot as Super Cheap, Cheap, Normal, Expensive, or Very Expensive.
- Highlights how long the current period lasts and when the next change happens.
- Previews the next grouped pricing blocks so you can decide whether to run appliances now or later.
- Displays day-level price histograms for today and, when published, tomorrow.
- Supports on-device Wi-Fi setup, brightness control, region selection, and touch calibration.

## Screens

### Primary view

The main screen is optimized for glanceability. It emphasizes the current tariff band, the live unit rate, and the next few grouped periods.

![Primary screen during a super-cheap period](docs/img/primary-super-cheap.png)

![Primary screen during a very expensive period](docs/img/primary-very-expensive.png)

### Detail view

The detail screen shows the full daily shape of Agile pricing so it is easy to spot the best and worst parts of the day.

![Daily price histogram view](docs/img/detail-daily-prices.png)

## Hardware

Greenlight is built and tested on the common Cheap Yellow Display class board, typically sold as ESP32-2432S028R hardware.

Tested target characteristics:

- ESP32-based board
- 2.8 inch 240x320 ILI9341 TFT
- XPT2046 resistive touch controller
- Integrated Wi-Fi
- USB-powered standalone device format

The current firmware configuration matches the common CYD pinout defined in [main/hardware.h](main/hardware.h).

## Software Stack

- ESP-IDF 6
- LVGL 9
- `esp_lvgl_port`
- `esp_lcd_ili9341`
- `esp_lcd_touch_xpt2046`
- Public Octopus Energy API

The UI is written as a purpose-built embedded interface rather than a mirrored mobile app. Boot, Wi-Fi onboarding, time sync, tariff refresh, and rendering all happen on-device.

## How It Works

On startup, Greenlight:

1. Initializes display, touch, and persistent settings.
2. Reconnects to saved Wi-Fi if credentials are already stored.
3. Synchronizes local time for the `Europe/London` timezone.
4. Downloads the active public Octopus Agile tariff for the selected region.
5. Renders a primary summary screen, a detailed chart screen, and a settings screen.

If pricing cannot be loaded on startup, the device surfaces an offline state instead of pretending stale data is current.

## Current Feature Set

- On-device Wi-Fi scanning and password entry
- Saved Wi-Fi credentials for reconnect after reboot
- Touchscreen calibration stored in NVS
- Adjustable display brightness
- Region selection for Octopus Agile pricing
- Startup splash screen with live status text
- Current and upcoming tariff block summaries
- Today and tomorrow histogram views when data is available
- Graceful handling for partial tomorrow publication from Octopus

## Data Source

Greenlight uses the public Octopus Energy API and currently targets public Agile import pricing by region. It does not require Octopus account authentication for the current firmware flow.

User-facing prices are based on VAT-inclusive unit rates returned by the public tariff endpoints.

## Repository Layout

- [main](main): ESP-IDF application code
- [docs](docs): setup notes and project documentation
- [docs/img](docs/img): README screenshots and documentation images
- [tools](tools): validation, asset generation, and screenshot capture helpers

## Getting Started

For local setup and validation details, see [docs/getting-started.md](docs/getting-started.md).

Minimal firmware build flow:

```sh
idf.py set-target esp32
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

The project is developed against ESP-IDF 6.0. The checked-in dev container is the easiest way to get a matching environment.

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) for contribution and validation expectations.

## Notes

Greenlight is an independent project and is not affiliated with Octopus Energy. Octopus Agile and Octopus Energy are trademarks of their respective owners.

Greenlight is maintained by [Ampernomics](https://ampernomics.com).
