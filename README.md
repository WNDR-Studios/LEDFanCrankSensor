# Hand-Crank Fan Installation

An interactive art installation: a heavy hand crank that visitors spin to "power" a
separate LED fan installation. The crank doesn't generate real electricity — an Arduino
detects when the crank has been spun up to speed and switches on AC power to the fans
via a relay. Slow down or stop, and the fans switch off again after a short delay. A
decorative chain/belt runs from the crank up to the fans to sell the illusion that
cranking physically drives them.

This README covers everything needed to build the electronics from scratch: parts,
wiring, software setup, configuration, and the calibration procedure. For the full
project background and mechanical design notes, see [CLAUDE.md](CLAUDE.md). For
detailed wiring diagrams, see [docs/WIRING.md](docs/WIRING.md).

## How it works

```
[Hand Crank + Flywheel]
        |
        | (magnet on shaft passes sensor once per revolution)
        v
[A3144 Hall Sensor] --> [Arduino Nano] --> [5V Relay Module] --> [AC power to fan PSU]
```

1. A magnet on the crank shaft passes an A3144 Hall-effect sensor once per revolution.
2. The Arduino measures RPM from the time between pulses.
3. A hysteresis state machine (IDLE / RUNNING / COOLDOWN) decides when to energize the
   relay, with minimum on/off times to prevent rapid relay cycling.
4. The relay switches AC mains power to the fan installation's own power supply.

## Bill of materials

| Part | Notes |
|---|---|
| Arduino Nano (or compatible clone) | Main controller |
| A3144 Hall-effect sensor | Digital, open-collector output |
| Neodymium disc magnet | Epoxied to the crank shaft |
| 5V relay module (SRD-05VDC-SL-C, opto-isolated) | Switches AC to the fan PSU |
| LED fan installation + its own PSU | The switched load |
| USB cable + 5V power source | Powers the Nano (and relay coil via the Nano's 5V rail) |
| Hand crank + flywheel + decorative chain/belt | See [CLAUDE.md](CLAUDE.md) for the mechanical build |
| Electrical enclosure, strain relief, fuse/breaker | For the AC mains side |
| Optional: 10k ohm resistor | External pull-up for the A3144, only needed for long wire runs |
| Optional: 0.1uF ceramic capacitor | Noise filter across A3144 OUT-GND, only if needed |

## 1. Software setup

### Option A: Arduino IDE

1. Install [Arduino IDE 2.x](https://www.arduino.cc/en/software).
2. Open **Boards Manager** (left sidebar) and install the **Arduino AVR Boards** package.
3. Open `LEDFanCrankSensor/LEDFanCrankSensor.ino` in the IDE.
4. Select **Tools > Board > Arduino AVR Boards > Arduino Nano**.
5. Select **Tools > Processor**. Most clones need **ATmega328P (Old Bootloader)** — if
   upload fails with the default option, try the old bootloader variant.
6. Select the correct **Port** (COM port the Nano enumerates as — check Device Manager
   if it's not obvious; clone boards often use a CH340 USB-serial chip).
7. Click **Upload**.

### Option B: arduino-cli

```sh
arduino-cli core install arduino:avr
arduino-cli compile --fqbn arduino:avr:nano LEDFanCrankSensor
arduino-cli upload -p <COM_PORT> --fqbn arduino:avr:nano:cpu=atmega328old LEDFanCrankSensor
```

(Drop `:cpu=atmega328old` if your board uses the newer bootloader.)

## 2. Wiring

| From | To | Notes |
|---|---|---|
| A3144 VCC | Nano 5V | |
| A3144 GND | Nano GND | |
| A3144 OUT | Nano D2 (INT0) | Sketch uses `INPUT_PULLUP` |
| Relay module VCC | Nano 5V | Coil draws ~70-80mA, fine on a USB-powered Nano |
| Relay module GND | Nano GND | |
| Relay module IN | Nano D7 | Active-LOW assumed — verify before wiring AC, see below |
| Relay COM | AC Hot (line) in | |
| Relay NO | AC Hot to fan installation PSU | Switched conductor |
| Relay NC | unused | |
| AC Neutral / Ground | Straight through to fan PSU | Not switched |

See [docs/WIRING.md](docs/WIRING.md) for full ASCII wiring diagrams, A3144 mounting and
magnet orientation notes, the relay polarity test procedure, and AC mains safety notes.

**Do not connect AC mains until the relay polarity test (Step 4 below) has passed.**

## 3. Configuration reference

All tunable constants live at the top of `LEDFanCrankSensor.ino`. Defaults are
placeholders — see the calibration steps below for tuning them on real hardware.

| Constant | Default | Meaning |
|---|---|---|
| `HALL_SENSOR_PIN` | `2` | Hall sensor signal, must be interrupt-capable (D2/D3 on Nano) |
| `RELAY_PIN` | `7` | Relay module IN pin |
| `STATUS_LED_PIN` | `13` | Onboard LED, used for debug status |
| `RELAY_ACTIVE_LOW` | `true` | `true` if driving the relay pin LOW energizes the coil |
| `PULSES_PER_REV` | `1` | Magnet pulses per crank revolution |
| `RPM_UPDATE_INTERVAL_MS` | `100` | How often the RPM reading is recomputed/smoothed |
| `RPM_SMOOTHING_SAMPLES` | `4` | Number of samples averaged together (smoothing window = samples x update interval) |
| `MIN_PULSE_INTERVAL_MS` | `5` | Debounce: ignore pulses closer together than this |
| `RPM_STALL_TIMEOUT_MS` | `3000` | No pulses for this long -> RPM reads 0 |
| `RPM_ON_THRESHOLD` | `35.0` | RPM required to turn the relay ON |
| `RPM_OFF_THRESHOLD` | `25.0` | RPM below which the relay may turn OFF |
| `MIN_RUN_TIME_MS` | `8000` | Minimum time the relay stays ON once energized |
| `MIN_OFF_TIME_MS` | `3000` | Minimum time the relay stays OFF before it can re-energize |
| `SENSOR_TIMEOUT_MS` | `4000` | No pulses for this long while relay is ON -> fail-safe shutoff |
| `SERIAL_BAUD` | `115200` | Serial Monitor baud rate |
| `SERIAL_PRINT_INTERVAL_MS` | `250` | Debug print rate |
| `LED_COOLDOWN_BLINK_MS` | `150` | Onboard LED blink rate while in COOLDOWN |

## 4. Build & calibration steps

1. **Bench test RPM sensing.** Wire only the A3144 (Section 2). Upload the sketch, open
   the Serial Monitor at 115200 baud, and hand-spin the flywheel. Confirm `instRPM` and
   `avgRPM` track your cranking speed smoothly at both low and high speeds. The relay
   pin can be left unconnected for this step.

2. **Validate the state machine.** Still without the relay connected, watch the Serial
   Monitor for `State change: ...` messages as you cross `RPM_ON_THRESHOLD` and
   `RPM_OFF_THRESHOLD`. Confirm IDLE -> RUNNING -> COOLDOWN -> IDLE transitions happen
   as expected, with the minimum run/off times being respected.

3. **Wire the relay (no AC yet).** Connect the relay module per Section 2, but leave
   COM/NO/NC disconnected from AC. Use an LED or low-voltage load across COM/NO to test.

4. **Confirm relay polarity.** Follow the test procedure in
   [docs/WIRING.md](docs/WIRING.md) Section 5. If the relay behaves backwards (load is
   on when the log says `RELAY OFF`), set `RELAY_ACTIVE_LOW = false` and re-upload.

5. **Tune thresholds.** Once the physical crank/flywheel assembly is built, adjust
   `RPM_ON_THRESHOLD`, `RPM_OFF_THRESHOLD`, `MIN_RUN_TIME_MS`, and `MIN_OFF_TIME_MS`
   based on how it actually feels to crank.

6. **Final AC integration.** Wire the relay's COM/NO/NC to AC mains inside a proper
   electrical enclosure with strain relief and an upstream fuse/breaker. **Have an
   electrician verify the mains wiring before energizing the installation.**

## Repository layout

| Path | Description |
|---|---|
| `LEDFanCrankSensor/LEDFanCrankSensor.ino` | The Arduino sketch |
| `docs/WIRING.md` | Detailed wiring diagrams, mounting notes, polarity test, AC safety |
| `CLAUDE.md` | Full project brief — mechanical design, requirements, development plan |
