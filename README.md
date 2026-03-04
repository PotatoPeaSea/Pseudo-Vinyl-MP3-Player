# Pseudo Vinly MP3 Player

A portable, battery-powered MP3 player built on the **ESP32-S3** that streams audio wirelessly to Bluetooth earbuds and features a **vinyl-style spinning album art** animation on a circular display.

![Status](https://img.shields.io/badge/status-in%20development-yellow)

## Features

- **Bluetooth Audio** — Streams to wireless earbuds/speakers via A2DP (primary output)
- **Wired Output** — Secondary 3.5mm headphone jack via PCM5102 I2S DAC
- **Circular Display** — 1.28" GC9A01 240×240 IPS round screen
- **Vinyl Spin Animation** — Album art spins like a vinyl record during playback
- **Physical Controls** — 3 buttons (play/pause, next, previous) + rotary encoder (volume/scroll)
- **Shuffle & Repeat** — Cycle modes via encoder push button
- **SD Card Storage** — Reads MP3 files from a FAT32 SD card
- **ID3 Tag Support** — Displays song title, artist, and album art from metadata
- **Battery Powered** — LiPo battery with TP4056 USB-C charging

## Hardware

| Component | Model |
|---|---|
| MCU | ESP32-S3-WROOM-1 (N8R8) |
| DAC | PCM5102 (I2S) |
| Display | GC9A01 1.28" Round IPS |
| Storage | SPI SD Card Reader |
| Encoder | KY-040 Rotary Encoder |
| Buttons | 3× Tactile Switches |
| Battery | 3.7V LiPo + TP4056 Charger |

## Project Structure

```
Pseudo-Vinyl-MP3-Player/
├── README.md
├── docs/
│   └── PRD.md              # Product Requirements Document
├── firmware/                # ESP32-S3 firmware (coming soon)
└── tools/                   # Desktop utilities
    └── prescale_art/        # Album art pre-scaler tool (coming soon)
```

## Documentation

- [Product Requirements Document](docs/PRD.md) — Full specifications, architecture, and UI/UX design

## License

TBD
