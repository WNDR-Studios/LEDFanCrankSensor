# Hand-Crank Fan Installation — Project Brief

## Project Overview

This project is an interactive art/installation piece: a heavy, early-industrial-style
hand crank that a user physically spins to "power" a separate installation of LED fans.
The crank itself does not generate real electrical power — instead, a microcontroller
detects when the user has cranked it up to a target speed and switches on AC power to
the fan installation via a relay. When the user slows down or stops, the fans turn off
after a short delay. A decorative chain/belt runs from the crank mechanism up to the fan
installation to visually reinforce the illusion that cranking physically drives the fans.

## Mechanical System (context for the code — not part of the sketch itself)

- **Hand crank**: heavy cast-iron crank (sourced from an antique blacksmith post drill
  or similar), early-industrial aesthetic.
- **Flywheel / resistance**: an air-resistance fan wheel (e.g. salvaged from a Schwinn
  Airdyne or Concept2 rower), providing natural, self-scaling resistance — the faster
  you spin it, the harder it pushes back. No electronic or friction brake needed.
- **Decorative chain/belt**: a roller chain or V-belt running from the crank shaft up to
  the fan installation, purely cosmetic — carries no load, just sells the illusion.
- **Adjustable tension** (mechanical, not electronic): a bracket/cam that can change the
  air gap or shroud position on the flywheel to adjust how hard it is to crank.

## Electronics System (this is what the sketch controls)

```
[Hand Crank + Flywheel]
        |
        | (magnet on shaft passes sensor once per revolution)
        v
[Hall Effect Sensor (A3144)] --> [Arduino Nano] --> [5V Relay Module] --> [AC power to fan installation's power supply]
```

### Components

| Component | Role | Notes |
|---|---|---|
| A3144 Hall effect sensor | RPM detection | Interrupt-driven, one pulse per revolution via magnet on shaft |
| Neodymium disc magnet | Trigger for sensor | Epoxied to crank shaft, NOT hot-glued (vibration/heat) |
| Arduino Nano | Main controller | Reads RPM, runs state machine, drives relay |
| 5V relay module (SRD-05VDC-SL-C, optocoupler) | AC switching | Controls 120V AC line to fan power supply |
| LED fan installation | The "output" | Powered via relay-switched AC, runs its own PSU |

## Core Logic Requirements

1. **RPM Measurement**
   - Use the hall effect sensor on an interrupt pin.
   - Count pulses (1 per revolution) over a rolling time window to compute RPM.
   - Smooth/average RPM over multiple samples to avoid jitter from inconsistent
     hand-cranking speed.

2. **Threshold-Based Relay Control with Hysteresis**
   - Define two thresholds, not one:
     - `RPM_ON_THRESHOLD` — RPM must reach/exceed this to turn relay ON (e.g. 35 RPM)
     - `RPM_OFF_THRESHOLD` — RPM must drop below this to turn relay OFF (e.g. 25 RPM)
   - This hysteresis gap is the core debounce mechanism — prevents relay chatter when
     RPM hovers near a single threshold.

3. **Cooldown / Minimum Run Time**
   - Once the relay turns ON, it must stay ON for a minimum duration (e.g. 5–10 seconds)
     regardless of RPM dropping, to avoid rapid on/off cycling that could damage the fan
     power supplies.
   - Similarly, once turned OFF, enforce a minimum OFF time before it can turn ON again
     (e.g. 3 seconds), to prevent immediate re-triggering from a quick re-crank.

4. **State Machine**
   - Suggested states: `IDLE` (relay off, below threshold), `RUNNING` (relay on, RPM
     sustained or in cooldown), `COOLDOWN` (RPM dropped below off-threshold, waiting out
     minimum run time before allowing OFF).
   - Use `millis()`-based timing throughout — no `delay()` calls, since RPM sensing
     needs to keep running continuously via interrupts.

5. **Relay Safety**
   - Relay should default to OFF on power-up / reset.
   - Consider a "fail-safe" — if RPM sensor stops producing pulses for an extended
     period while relay is ON (e.g. sensor failure), force relay OFF.

## Suggested Pin Layout (Arduino Nano)

- D2 — Hall effect sensor signal (interrupt-capable pin)
- D7 — Relay control signal (active HIGH or LOW depending on module — confirm before wiring)
- Onboard LED (D13) — useful for debug/status indication (blink pattern for state)

## Tunable Constants (should live at top of sketch, clearly labeled)

```cpp
const float RPM_ON_THRESHOLD   = 35.0;
const float RPM_OFF_THRESHOLD  = 25.0;
const unsigned long MIN_RUN_TIME_MS  = 8000;   // minimum time relay stays ON
const unsigned long MIN_OFF_TIME_MS  = 3000;   // minimum time relay stays OFF
const unsigned long RPM_SAMPLE_WINDOW_MS = 1000; // window for RPM calculation
```

## Development Plan / Milestones

1. **Bench test RPM sensing** — hall sensor + magnet on a hand-spun wheel, print RPM to
   Serial Monitor. Verify accuracy and noise behavior at low/high speeds.
2. **Implement state machine on Serial output only** — no relay yet, just print state
   transitions (IDLE / RUNNING / COOLDOWN) and timestamps to validate hysteresis and
   cooldown logic.
3. **Add relay control** — wire relay module, confirm active-HIGH vs active-LOW polarity,
   test with an LED or low-voltage load before connecting to AC.
4. **Tune thresholds** — once the actual flywheel/crank assembly is built, calibrate
   RPM_ON_THRESHOLD and RPM_OFF_THRESHOLD against how it actually feels to crank.
5. **Final AC integration** — relay wired to fan installation's power supply, inside
   proper electrical enclosure. ⚠️ Have an electrician verify mains wiring before
   running it live.

## Open Questions / Things to Decide Before Coding

- [ ] Confirm relay module polarity (active HIGH vs active LOW trigger)
- [ ] Confirm magnet placement — 1 magnet (1 pulse/rev) vs multiple magnets for finer
      resolution at low RPM
- [ ] Decide on actual RPM thresholds once the physical crank/flywheel is in hand —
      these are placeholders until real-world testing
- [ ] Decide whether status LED / indicator is needed for the installation itself (e.g.
      visual feedback to the user beyond the fans turning on)
- [ ] Decide on fail-safe behavior if sensor signal is lost while relay is ON

## Notes for Claude Code

- Target board: Arduino Nano (or compatible clone)
- Use the Arduino IDE / `arduino-cli` conventions — single `.ino` sketch file is fine
  for this scope, but feel free to split into header files if it improves clarity
  (e.g. `rpm_sensor.h`, `relay_control.h`)
- Prioritize readability and clearly commented tunable constants — this will be
  iterated on physically once hardware is built
- Include Serial.print debug output throughout (RPM value, current state, time in
  state) — this will be essential for tuning thresholds on the actual hardware
