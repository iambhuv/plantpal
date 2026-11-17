# PlantPal

Self-watering plant controller on a NodeMCU ESP8266. The firmware uses the
native **ESP8266 RTOS SDK in C (no Arduino)**. The control panel is a small
Svelte app that is built into the firmware and served by the board.

What it does:

- Reads soil moisture (A0) every 2 s and air temperature/humidity (DHT11) every 10 s.
- Runs the pump through a relay when the soil drops below a threshold, or when
  the button or the panel's **Water now** is pressed.
- Joins the strongest saved Wi-Fi network and answers at `http://plantpal.local`.
- Keeps its own fallback network, **PlantPal**, so you can always reach it.

## Hardware

Full parts list, wiring table, build order and gotchas:
[docs/circuit/README.md](docs/circuit/README.md)
(schematic: [docs/circuit/plantpal-schematic.png](docs/circuit/plantpal-schematic.png)).

### Pin map

| Function         | NodeMCU pin | GPIO   | Notes                                  |
|------------------|-------------|--------|----------------------------------------|
| Relay IN (pump)  | D1          | GPIO5  | HIGH = pump on; stays LOW during boot  |
| DHT11 DATA       | D5          | GPIO14 | VCC 3V3, 10 kΩ pull-up to 3V3          |
| Moisture (analog)| A0          | ADC0   | supply 3V3                             |
| Button           | D0          | GPIO16 | to GND, 10 kΩ pull-up to 3V3           |
| Green LED        | D2          | GPIO4  | active-low, 330 Ω                      |
| Red LED          | D6          | GPIO12 | active-low, 330 Ω                      |
| Blue LED (pump)  | D7          | GPIO13 | active-low, 330 Ω                      |

LEDs are active-low: anode through 330 Ω to 3V3, cathode to the pin, so the
pin goes LOW to light them. Green means the soil is at or above the
threshold, red means it is below, blue means the pump is running.

### Pump and relay

- The relay module (JQC3F-05VDC-C) is active-high: D1 HIGH closes the contact.
- 9 V supply + goes to relay **COM**; relay **NO** goes to pump +; pump − goes to GND.
- 1N4007 flyback diode across the pump: **band to pump +**, other end to pump −.

### Power (safety-critical)

- One 9 V DC supply feeds the pump (through the relay) and an **L7805**
  regulator. The L7805's 5 V output feeds the NodeMCU **VIN** and the relay **VCC**.
- **VIN needs 5 V.** Do not feed it 3.3 V (for example from the old LM2596 module).
- All parts share one common ground. A missing ground is the most common
  reason a relay won't switch.
- Use the relay's **NO** contact, not NC. On NC the pump runs whenever the
  relay is off, including at boot.
- The 9 V supply must handle the pump's current; a PP3 battery sags badly.
- The L7805 gets warm (about 1 W). That is normal; keep it away from plastic.
- Measure the 5 V rail before connecting anything else, and connect the pump
  last, after checking the diode's direction.

## Using it

### Wi-Fi

- **First time (no saved networks):** join the **PlantPal** network
  (password `water1234`) and open `http://192.168.4.1`. Add your networks on
  the **Settings** page.
- **Saved networks:** up to 5, added, edited and removed on the Settings page.
  Passwords are stored on the board and never sent back to the browser.
  Each network shows its state: connected, in range, not in range, or wrong password.
- **2.4 GHz only:** the ESP8266 can't see 5 GHz networks. Phone hotspots
  often default to 5 GHz; set the hotspot's band to 2.4 GHz or it shows as
  "not in range".
- **Choosing a network:** the board scans and joins the strongest saved
  network in range. If a join doesn't finish within about 8 s it tries the
  next one. If none is in range it scans again every 5 s.
- **Failover:** if a working network drops, the board retries it once, then
  scans and joins the best remaining one. A network with a wrong password is
  skipped until you edit it or restart the board.
- **Fallback network:** the **PlantPal** network is on by default and can be
  switched off in Settings. Even when switched off, it comes back whenever no
  saved network has been connected for 30 s, so you can't lock yourself out.
- **Address:** on a saved network, open `http://plantpal.local`. Some phones
  (often Android) don't resolve `.local` names; use the IP address instead.
  It is shown on the Settings page next to the connected network, and printed
  on the serial console when the board joins.

### Panel

Two tabs:

- **Plant:** soil moisture with the threshold line, temperature, humidity,
  time since last watering, **Water now**, the auto-watering switch, and
  recent events.
- **Settings:** Wi-Fi networks, watering threshold, pump run time, wait
  between waterings, the PlantPal network switch, uptime, free memory, and Restart.

Settings and networks are saved to flash and survive a restart. The event
log is kept in RAM only (last 16 events) and is cleared on restart.

The panel updates live through Server-Sent Events. If the connection drops it
shows “Reconnecting…” and resumes on its own.

### Watering rules

- Auto mode waters when moisture is below the threshold (default 40 %).
- Each run lasts `run_s` seconds (default 3, range 1–10). Every run, including
  manual ones, is hard-capped at 10 s.
- After a run, auto mode waits `wait_s` seconds (default 60, range 10–1800)
  before it can water again, so the water has time to soak in.
- Manual watering (button or **Water now**) ignores the threshold and `wait_s` but not the cap.
  A press while the pump is already running is ignored.
- Watering runs in the main loop and never waits on Wi-Fi or HTTP.

## HTTP API

All responses are JSON. Errors look like `{"error":"..."}` with a matching
HTTP status.

| Method | Path               | Purpose |
|--------|--------------------|---------|
| GET    | `/`                | The panel (gzipped, cached with an ETag) |
| GET    | `/api/status`      | Status snapshot: sensors, pump, policy, network, uptime, free heap, recent events |
| GET    | `/api/events`      | Server-Sent Events stream of the same snapshot, sent on change or every 10 s |
| POST   | `/api/water`       | Start one manual watering cycle → `{"ok":true}` |
| POST   | `/api/settings`    | Any of `auto`, `threshold` (0–100), `run_s` (1–10), `wait_s` (10–1800), `ap_enabled` → status snapshot |
| GET    | `/api/wifi`        | Saved networks `[{ssid, state, rssi}]`, no passwords |
| POST   | `/api/wifi`        | Add or edit `{"ssid", "pass"}`; omit `pass` to keep the saved one → list |
| POST   | `/api/wifi/delete` | Remove `{"ssid"}` → list |
| POST   | `/api/restart`     | `{"ok":true}`, then reboots about 0.5 s later |

Rules:

- **Every POST must send `Content-Type: application/json`**, or it is
  rejected with 415. This includes `/api/water` and `/api/restart`.
- Request bodies are limited to 512 bytes.
- Numbers outside a range are clamped. A request with any invalid field
  (wrong type) is rejected with 400 and changes nothing.
- SSID is 1–32 characters; password is empty (open network) or 8–64 characters.
  Adding a sixth network returns 409. `/api/wifi` POSTs return 503 if the Wi-Fi task is busy; retry.
- At most 4 event streams at once; a fifth gets 503. Close a tab to free a slot.

Examples (replace `plantpal.local` with the IP if needed):

```sh
curl http://plantpal.local/api/status
curl -N http://plantpal.local/api/events
curl -X POST -H 'Content-Type: application/json' -d '{}' http://plantpal.local/api/water
curl -X POST -H 'Content-Type: application/json' -d '{"threshold":45,"run_s":4}' http://plantpal.local/api/settings
curl -X POST -H 'Content-Type: application/json' -d '{"ssid":"My hotspot","pass":"secret123"}' http://plantpal.local/api/wifi
curl -X POST -H 'Content-Type: application/json' -d '{"ssid":"My hotspot"}' http://plantpal.local/api/wifi/delete
curl -X POST -H 'Content-Type: application/json' -d '{}' http://plantpal.local/api/restart
```

## Building

### Firmware (macOS)

The SDK lives at `~/esp/ESP8266_RTOS_SDK`. `env.sh` sets `IDF_PATH`, the
`xtensa-lx106-elf` toolchain path and the Python environment.

```sh
source env.sh
make defconfig                                   # first time only (creates sdkconfig)
make -j all                                      # build
make flash monitor ESPPORT='/dev/cu.usbserial-*' # flash, then open the serial console
```

USB-serial adapters (FT232R, CH340) show up on macOS as
`/dev/cu.usbserial-…`; run `ls /dev/cu.usbserial-*` to find yours. The
serial console runs at 74880 baud. Exit the monitor with `Ctrl+]`.

### Web panel

The panel source is in `web/` (Svelte 5 + Vite, Bun as the package manager).
`bun run build` produces one self-contained HTML file and writes it gzipped
to `main/www/index.html.gz`, which the firmware embeds.

```sh
cd web && bun install && bun run build
```

You normally don't need to run this by hand: `make` rebuilds the panel when
files in `web/` are newer than the `.gz` and `bun` is installed. Without
`bun`, or if the web build fails, `make` embeds the committed
`main/www/index.html.gz` as it is.

For working on the panel with live reload:

```sh
cd web
DEVICE=mock bun run dev                  # fake API, no board needed
DEVICE=http://192.168.1.50 bun run dev   # proxy /api to a board at that IP
bun run dev                              # proxy /api to http://plantpal.local
```

### Host tests

Pure-C logic (network selection, event log, settings clamping, JSON nesting
guard) is tested on the computer:

```sh
sh test/host/run.sh
```

It prints `PASS` when all tests pass.
