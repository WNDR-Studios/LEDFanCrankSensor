# Wiring Plan — Hand-Crank Fan Installation

This document describes how to wire the Arduino Nano, A3144 Hall-effect sensor, and
5V relay module for the hand-crank fan installation. See `CLAUDE.md` for the overall
project concept and `LEDFanCrankSensor/LEDFanCrankSensor.ino` for the controller sketch.

```
[Hand Crank + Flywheel]
        |
        | (magnet on shaft passes sensor once per revolution)
        v
[A3144 Hall Sensor] --> [Arduino Nano] --> [5V Relay Module] --> [AC power to fan PSU]
```

The Nano is powered via USB (wall adapter or power bank). The relay module's logic
side (VCC/GND) is powered from the Nano's 5V/GND pins — the relay coil draws roughly
70-80mA, comfortably within what a USB-powered Nano's 5V rail can supply.

## 1. Component list

| Component | Notes |
|---|---|
| Arduino Nano (or compatible clone) | Main controller |
| A3144 Hall-effect sensor | Digital, open-collector output |
| Neodymium disc magnet | Epoxied to crank shaft (not hot-glued) |
| 5V relay module (SRD-05VDC-SL-C, optocoupler) | Switches AC to fan PSU |
| LED fan installation + its own PSU | The switched load |
| USB cable + wall adapter or power bank | Powers the Nano (and relay module via 5V) |
| Optional: 10k ohm resistor | External pull-up for A3144 OUT if using long wire runs (sketch defaults to `INPUT_PULLUP`, so this is not required for short runs) |
| Optional: 0.1uF ceramic capacitor | Across A3144 OUT-GND, only if signal noise is observed on the bench |
| Electrical enclosure, strain relief, fuse/breaker | For the AC mains side — see Section 6 |

## 2. Pin connection table

| From | To | Notes |
|---|---|---|
| A3144 VCC | Nano 5V | |
| A3144 GND | Nano GND | |
| A3144 OUT | Nano D2 (INT0) | Sketch uses `INPUT_PULLUP`; add an external 10k pull-up to 5V only if using a long wire run and seeing unreliable triggering |
| Relay module VCC | Nano 5V | Coil draws ~70-80mA — fine for a USB-powered Nano |
| Relay module GND | Nano GND | |
| Relay module IN | Nano D7 | Active-LOW assumed (`RELAY_ACTIVE_LOW = true` in the sketch) — **verify with the test in Section 5 before connecting AC** |
| Relay COM | AC Hot (line) — incoming | |
| Relay NO | AC Hot (line) — to fan installation PSU | Closes COM→NO when energized |
| Relay NC | unused | |
| AC Neutral | Straight through, supply → fan PSU | Not switched — standard practice, switch only the hot/line conductor |
| AC Ground/Earth | Straight through, supply → fan PSU chassis ground | Not switched |
| Nano D13 (onboard LED) | — | No external wiring; used for debug status (off=IDLE, solid=RUNNING, fast blink=COOLDOWN) |

> **A3144 pinout varies by supplier/breakout board.** The common order on a 3-pin TO-92
> package (facing the flat/branded side, leads down) is VCC - GND - OUT, but some
> breakout boards differ. Confirm with a multimeter or the datasheet for your specific
> part before wiring.

## 3. ASCII wiring diagram

### Low-voltage control side

```
        A3144 Hall Sensor                    Arduino Nano
       +-----------------+                  +----------------+
       |  VCC  OUT  GND  |                  |                |
       +---+----+----+---+                  |                |
           |    |    |                      |                |
           |    |    +----------------------+ GND            |
           |    +---------------------------+ D2 (INT0)      |
           +--------------------------------+ 5V             |
                                             |                |
                                             |            5V Relay Module
                                             |           +------------------+
                                             |           | VCC  IN     GND  |
                            5V --------------+-----------+ VCC              |
                            D7 --------------+-----------+  IN              |
                            GND -------------+-----------+ GND              |
                                             |           |                  |
                                             |           |  COM   NO   NC   |
                              USB cable -----+           +---+-----+----+---+
                              (5V power in)                  |     |    |
                                                              |     |    |
                                              == continues to AC side below ==
```

### AC mains side

```
   ************************************************************
   *  DANGER - AC MAINS (120V). Switch the HOT/LINE wire ONLY. *
   *  Have an electrician verify this wiring (Milestone 5)     *
   *  before energizing. Use a proper enclosure + strain       *
   *  relief + upstream fuse/breaker.                          *
   ************************************************************

   AC supply HOT  ----------------------------> Relay COM
   Relay NO       ----------------------------> Fan installation PSU (HOT in)
   Relay NC       ----------------------------> (unused)

   AC supply NEUTRAL -------------------------------------------> Fan PSU (NEUTRAL in)
                          (runs straight through — not switched)

   AC supply GROUND --------------------------------------------> Fan PSU (GROUND)
                          (runs straight through — not switched)
```

## 4. Hall sensor mounting notes

- Epoxy the magnet to the crank shaft — **not hot glue** (vibration and heat over time
  will loosen hot glue).
- **Orientation**: the A3144 is unipolar — it only responds to one pole (N or S), not
  both. On the TO-92 package, the sensing face is the flat side with the part markings.
  Mount the magnet so one of its flat pole faces points directly at that sensing face,
  field lines perpendicular to it (a disc magnet works well here). If the sensor doesn't
  trigger at all once everything is wired up, **flip the magnet over** — the wrong pole
  produces zero output, and that's the most common cause of "no pulses."
- **Air gap vs. magnet strength**: the A3144 triggers once the field at the sensor face
  exceeds roughly 1.5-3.5 mT, and field strength falls off steeply with distance
  (roughly inverse-cube). As a rough guide:
  - A small 10mm x 3mm N35 disc typically triggers reliably at ~5-10mm air gap.
  - A larger/higher-grade magnet (15-20mm, N42/N52) can extend that to ~15-25mm+.
  A stronger magnet gives more tolerance for mounting imprecision and vibration, but
  diminishing returns apply (doubling magnet strength does not double the gap), and an
  overly strong magnet may also attract nearby steel hardware/fasteners.
- One magnet = one pulse per revolution, matching `PULSES_PER_REV = 1` in the sketch.
  If you later add more magnets for finer low-RPM resolution, update that constant to
  match.
- Mount the sensor securely (zip ties, 3D-printed bracket, etc.) so the air gap stays
  consistent as the flywheel spins.

## 5. Relay polarity test (Milestone 3)

Before connecting AC mains, confirm whether the relay module is active-LOW or
active-HIGH:

1. Wire the relay module's VCC/GND/IN as in the table above (Nano 5V/GND/D7), but leave
   COM/NO/NC disconnected from AC entirely.
2. Connect a low-voltage test load (e.g. an LED with a current-limiting resistor, or a
   multimeter set to continuity) across COM and NO, powered from a separate low-voltage
   source (not AC).
3. Upload the sketch and open the Serial Monitor at 115200 baud.
4. Hand-spin the flywheel (or temporarily lower `RPM_ON_THRESHOLD` for bench testing) to
   trigger the `>>> RELAY ON` log message.
5. Observe the test load:
   - If it activates when the log says `RELAY ON` and deactivates on `RELAY OFF`, the
     module matches `RELAY_ACTIVE_LOW = true` (the current default) — no code change
     needed.
   - If the behavior is inverted (load is on when the log says `RELAY OFF`), set
     `RELAY_ACTIVE_LOW = false` in the sketch and re-upload.
6. Re-run the test to confirm the corrected polarity before moving on to AC wiring.

## 6. AC integration safety notes (Milestone 5)

- Switch **only** the hot/line conductor through the relay's COM/NO contacts. Neutral
  and ground run straight through to the fan installation's power supply, unswitched.
- Confirm the fan PSU's current draw is comfortably within the relay module's rated
  contact rating (commonly 10A @ 250VAC for SRD-05VDC-SL-C boards, but check your
  specific module's silkscreen/datasheet).
- Mount the relay module, terminal connections, and AC wiring inside a proper
  electrical enclosure with strain relief on all cable entries.
- Add an upstream fuse or breaker sized for the fan PSU's load.
- **Have an electrician verify the complete mains wiring before energizing the
  installation**, per the project's development plan.

## 7. Future expansion — visitor-facing status LED

The current sketch only drives the onboard D13 LED for debug status (off = IDLE,
solid = RUNNING, fast blink = COOLDOWN). If you later want a status LED visible to
visitors:

- Pick any spare digital pin (e.g. D8) and wire an LED + ~220-330 ohm current-limiting
  resistor from that pin to GND.
- `updateStatusLed()` in the sketch is already a self-contained function — add a second
  `digitalWrite()`/blink call there for the new pin without touching the RPM, relay, or
  state-machine logic.

## 8. Open items / TODO

- [ ] Confirm relay module polarity using the Section 5 test, and set
      `RELAY_ACTIVE_LOW` accordingly.
- [ ] Confirm A3144 pinout (VCC/OUT/GND order) for your specific breakout with a
      multimeter before wiring.
- [ ] Confirm magnet placement and count (currently assumed 1 magnet = 1 pulse/rev,
      `PULSES_PER_REV = 1`).
- [ ] Once the physical crank/flywheel is built, tune `RPM_ON_THRESHOLD`,
      `RPM_OFF_THRESHOLD`, `MIN_RUN_TIME_MS`, and `MIN_OFF_TIME_MS` against how it
      actually feels to crank (Milestone 4).
- [ ] Revisit `MIN_PULSE_INTERVAL_MS` and `SENSOR_TIMEOUT_MS` if Milestone 1 bench
      testing shows sensor noise or if thresholds change significantly.
- [ ] Add the optional 0.1uF noise-filter capacitor across A3144 OUT-GND only if needed.
