# Station Clock (Pebble Watchface)

Station Clock is a Pebble watchface for Pebble Time 2 (`emery`) with a split-panel retro station-display layout.

## Features

- Main screen:
  - Large digital time (12h/24h based on system setting)
  - Weather icon + temperature
  - Weekday + date
- Tracking screen:
  - Steps
  - Distance walked
  - Active minutes
  - Calories
- Optional battery indicator: thin colored line along the top edge of the screen
  - Width shrinks as battery discharges
  - Color changes by charge level: green (100–75%), yellow (75–50%), orange (50–25%), red (25–0%)
- Tap anywhere to switch between screens
  - Includes tap debounce (2 seconds) to prevent accidental rapid toggles
- Optional auto-return timeout from tracking screen back to main screen
- Persistent settings:
  - Temperature unit: Fahrenheit or Celsius
  - Distance unit: Miles or Kilometers
  - Tracking timeout (seconds, `0` disables auto-return)

## Weather Behavior

- Weather source: Open-Meteo current conditions API
- Uses phone geolocation
- Fetches:
  - On JS startup (`ready`)
  - Every 30 minutes (minute `00` and `30`)
  - On explicit watch request message
- Weather code mapping covers standard Open-Meteo WMO codes (clear, cloudy, fog, rain, freezing precipitation, snow, storm)

## Settings

Settings are provided via Clay (`src/pkjs/config.json`) and are persisted on-watch.

- `Use Fahrenheit` (`TEMP_UNIT_IS_F`)
- `Use Miles` (`DIST_UNIT_IS_MI`)
- `Show Battery` (`SHOW_BATTERY`, off by default)
- `Tracking Timeout` (`HEALTH_DISPLAY_TIMEOUT`, 0-300 seconds)

## Build and Run

Prerequisites:

- Pebble SDK 3.x
- Node.js
- Pebble CLI available in `PATH`

Install dependencies:

```bash
npm install
```

Build:

```bash
pebble build
```

Install to emulator:

```bash
pebble install --emulator emery
```

Take screenshot (optional):

```bash
pebble screenshot --no-open --emulator emery /tmp/station_clock.png
```

## Project Structure

- `src/c/main.c`: Watchface UI, rendering, health data, view switching, AppMessage handling
- `src/pkjs/index.js`: Weather fetch + messaging bridge
- `src/pkjs/config.json`: Clay settings UI
- `resources/images/glyphs/`: Bitmap glyphs used for all text rendering
- `resources/images/Weather/`: Weather icons
- `resources/images/Tracking/`: Tracking screen icons

## Notes

- Target platform is currently `emery` only (Pebble Time 2).
- Text rendering is bitmap-glyph based; no runtime TTF font usage.
