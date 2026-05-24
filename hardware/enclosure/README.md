# Greenlight Enclosure

Greenlight Case is a wall- or desk-friendly 3D printable enclosure for the Greenlight ESP32 touchscreen energy display project. This folder is the canonical home for the editable CAD source, neutral exchange export, and printable mesh files.

The design is also published on MakerWorld:

- <https://makerworld.com/en/models/2843371-greenlight-case>

## Overview

The enclosure is intended to turn a bare ESP32 touchscreen board into a tidier standalone device with:

- a framed front opening for the display
- a main body for the electronics
- a separate stand for desk use
- a separate wall-mount accessory for fixed installation

The repository is the best place for source and editable files. MakerWorld is the best place for a ready-to-print presentation and print profile.

## Layout

The enclosure assets are organized like this:

```text
hardware/
  enclosure/
    README.md
    source/
    exchange/
    print/
```

Folder purpose:

- `source/`: native CAD files such as Fusion 360 `.f3z`
- `exchange/`: neutral editable exports such as `.step`
- `print/`: final printable `.stl` files for each body or accessory

## File Inventory

Committed enclosure assets:

- `source/Greenlight Case.f3z`: Fusion 360 source archive
- `exchange/Greenlight Case.step`: neutral CAD exchange export
- `print/Greenlight Case Body.stl`: main enclosure body
- `print/Greenlight Case Front.stl`: front bezel or cover piece
- `print/Greenlight Case Stand.stl`: freestanding support piece
- `print/Greenlight Wall Mount.stl`: wall-mount accessory

## Compatibility

This enclosure is intended for the Greenlight hardware format shown on the MakerWorld listing: a CYD-style ESP32 board with a 240x320 resistive touchscreen.

Before printing for a different board family, check the screen opening, PCB outline, port locations, and mounting clearances against your hardware. If you later publish board-specific variants, list them here explicitly.

## Print Notes

The MakerWorld print profile is currently published with:

- `0.2 mm` layer height
- `2` walls
- `15%` infill

For local printing from the STL files:

- print the body and front as the primary enclosure parts
- print the stand for desk use
- print the wall mount only if you want fixed installation
- use the MakerWorld profile if you want the same baseline settings as the public release

If you test improved settings, materials, or orientations, document them here so the repo stays useful outside MakerWorld.

## Assembly Notes

Typical build flow:

- print the enclosure parts you need
- fit the Greenlight hardware into the main body
- attach the front piece
- choose either the desk stand or the wall mount accessory
