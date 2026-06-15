/*
  Hand-Crank Fan Installation Controller
  =======================================

  Reads RPM from a Hall-effect sensor (1 pulse per crank revolution) and
  switches a relay (AC power to an LED fan installation) once the crank has
  been spun up to speed, using hysteresis thresholds, minimum run/off times,
  and a sensor-loss fail-safe.

  See docs/WIRING.md for wiring details, the relay polarity test procedure,
  and AC safety notes.

  Development milestones (see CLAUDE.md):
    1. Bench test RPM sensing       - watch instRPM/avgRPM on Serial Monitor
    2. Validate state machine       - watch state transitions on Serial,
                                       relay pin can be left unconnected
    3. Add relay (LED/low-voltage)  - confirm RELAY_ACTIVE_LOW, see WIRING.md
    4. Tune thresholds               - adjust constants below against the
                                       real crank/flywheel
    5. Final AC integration          - electrician-verified mains wiring
*/

#include <Arduino.h>

// =====================================================================
// Pin assignments
// =====================================================================
const uint8_t HALL_SENSOR_PIN = 2;   // D2 - INT0, interrupt-capable
const uint8_t RELAY_PIN       = 7;   // D7 - relay module IN
const uint8_t STATUS_LED_PIN  = 13;  // onboard LED - debug/status

// =====================================================================
// Relay polarity
// =====================================================================
// Most SRD-05VDC-SL-C style opto-isolated relay modules are ACTIVE-LOW:
// driving IN LOW energizes the relay coil. This is the assumed default.
//
// TODO (Milestone 3): confirm on real hardware using the relay polarity
// test procedure in docs/WIRING.md, with an LED/low-voltage load BEFORE
// connecting AC mains. Flip this to `false` if the relay energizes when
// the pin is driven HIGH instead.
const bool RELAY_ACTIVE_LOW = true;

// =====================================================================
// RPM measurement
// =====================================================================
// Number of magnet pulses per crank revolution. A single magnet on the
// shaft gives 1 pulse/rev.
// TODO: update if the magnet count changes (finer low-RPM resolution).
const uint8_t PULSES_PER_REV = 1;

// RPM is derived from the time between consecutive pulses (period-based),
// not from a pulse-counting window - at hand-crank speeds (well under
// 1 pulse/sec at PULSES_PER_REV = 1), a counting window only updates in
// coarse, multi-RPM steps. This interval instead controls how often that
// period-based reading is re-sampled into the smoothing buffer.
const unsigned long RPM_UPDATE_INTERVAL_MS = 100;

// Number of instantaneous RPM samples averaged together to smooth jitter
// from inconsistent hand-cranking speed (RPM_SMOOTHING_SAMPLES *
// RPM_UPDATE_INTERVAL_MS = total smoothing window, e.g. 4 * 100ms = 400ms).
const uint8_t RPM_SMOOTHING_SAMPLES = 4;

// Minimum time between accepted pulses, used inside the ISR to reject
// electrical noise/contact bounce on the Hall sensor output. 5ms allows
// for crank speeds far beyond any realistic hand-crank RPM
// (5ms/pulse = 12,000 RPM at 1 pulse/rev) while filtering true glitches.
// TODO: revisit after Milestone 1 bench testing if noise is observed.
const unsigned long MIN_PULSE_INTERVAL_MS = 5;

// If no pulse has been received for this long, report RPM as 0 instead of
// the period-based estimate asymptotically approaching zero. Must be longer
// than the time-per-revolution at RPM_OFF_THRESHOLD (25 RPM = 2400ms/rev)
// so normal slow cranking near that threshold isn't momentarily zeroed.
const unsigned long RPM_STALL_TIMEOUT_MS = 3000;

// =====================================================================
// Threshold / hysteresis (placeholders - tune in Milestone 4)
// =====================================================================
const float RPM_ON_THRESHOLD  = 35.0;  // RPM required to switch relay ON
const float RPM_OFF_THRESHOLD = 25.0;  // RPM below which relay may switch OFF

// =====================================================================
// Timing / cooldown (placeholders - tune in Milestone 4)
// =====================================================================
// Once the relay turns ON, it stays ON for at least this long regardless
// of RPM, to avoid rapid on/off cycling that could damage the fan PSUs.
const unsigned long MIN_RUN_TIME_MS = 8000;

// Once the relay turns OFF, it must stay OFF for at least this long before
// it can turn back ON, to prevent immediate re-triggering from a quick
// re-crank.
const unsigned long MIN_OFF_TIME_MS = 3000;

// =====================================================================
// Fail-safe
// =====================================================================
// If the relay is energized and no Hall sensor pulses are received for
// this long, force the relay OFF immediately (bypassing MIN_RUN_TIME_MS).
// Must be longer than the time-per-revolution at RPM_OFF_THRESHOLD
// (25 RPM = 2.4s/rev) so normal slow cranking doesn't trip it, but short
// enough to be a meaningful safety cutoff if the sensor/magnet fails.
// TODO: revisit if thresholds or PULSES_PER_REV change.
const unsigned long SENSOR_TIMEOUT_MS = 4000;

// =====================================================================
// Serial debug
// =====================================================================
const unsigned long SERIAL_BAUD = 115200;
const unsigned long SERIAL_PRINT_INTERVAL_MS = 250;

// =====================================================================
// Status LED (onboard D13)
// =====================================================================
const unsigned long LED_COOLDOWN_BLINK_MS = 150; // fast blink while in COOLDOWN

// =====================================================================
// System state
// =====================================================================
enum SystemState { IDLE, RUNNING, COOLDOWN };

volatile unsigned long lastPulseMicros      = 0; // last ISR call, for debounce
volatile unsigned long lastValidPulseMicros = 0; // last accepted pulse, for period calc
volatile unsigned long pulseIntervalMicros  = 0; // time between last two accepted pulses
volatile unsigned long lastPulseMillis      = 0; // last accepted pulse, for stall/fail-safe checks

unsigned long lastRpmCalcTime = 0;
float instantRpm = 0.0;
float rpmSamples[RPM_SMOOTHING_SAMPLES] = {0};
uint8_t rpmSampleIndex = 0;
float smoothedRpm = 0.0;

SystemState currentState = IDLE;
unsigned long stateEnteredTime = 0;
unsigned long relayOnSince     = 0;
unsigned long relayLastOffTime = 0;
bool relayEnergized = false;

unsigned long lastLedToggle = 0;
bool ledOn = false;

unsigned long lastSerialPrint = 0;

// =====================================================================
// Interrupt service routine - Hall sensor pulse
// =====================================================================
// The A3144 is open-collector and idles HIGH via INPUT_PULLUP; it pulls
// the line LOW when the magnet passes, so we trigger on FALLING.
void onHallPulse() {
  unsigned long nowMicros = micros();
  if (nowMicros - lastPulseMicros >= MIN_PULSE_INTERVAL_MS * 1000UL) {
    pulseIntervalMicros  = nowMicros - lastValidPulseMicros;
    lastValidPulseMicros = nowMicros;
    lastPulseMillis      = millis();
  }
  lastPulseMicros = nowMicros;
}

// =====================================================================
// Helpers
// =====================================================================
const char* stateName(SystemState s) {
  switch (s) {
    case IDLE:     return "IDLE";
    case RUNNING:  return "RUNNING";
    case COOLDOWN: return "COOLDOWN";
  }
  return "UNKNOWN";
}

// Translates a desired relay energize state into the physical pin level,
// accounting for RELAY_ACTIVE_LOW.
int relayLevelFor(bool energize) {
  if (RELAY_ACTIVE_LOW) {
    return energize ? LOW : HIGH;
  }
  return energize ? HIGH : LOW;
}

void setRelay(bool energize) {
  digitalWrite(RELAY_PIN, relayLevelFor(energize));
  relayEnergized = energize;

  if (energize) {
    relayOnSince = millis();
    Serial.println(F(">>> RELAY ON"));
  } else {
    Serial.println(F("<<< RELAY OFF"));
  }
}

void transitionTo(SystemState newState) {
  Serial.print(F("State change: "));
  Serial.print(stateName(currentState));
  Serial.print(F(" -> "));
  Serial.print(stateName(newState));
  Serial.print(F(" | avgRPM="));
  Serial.print(smoothedRpm);
  Serial.print(F(" | t="));
  Serial.println(millis());

  currentState = newState;
  stateEnteredTime = millis();
}

// =====================================================================
// Setup
// =====================================================================
void setup() {
  // Drive the relay pin to its "de-energized" level BEFORE switching it to
  // OUTPUT. This avoids a brief glitch pulse on power-up/reset, keeping the
  // relay safely OFF by default.
  digitalWrite(RELAY_PIN, relayLevelFor(false));
  pinMode(RELAY_PIN, OUTPUT);

  pinMode(HALL_SENSOR_PIN, INPUT_PULLUP);

  pinMode(STATUS_LED_PIN, OUTPUT);
  digitalWrite(STATUS_LED_PIN, LOW);

  Serial.begin(SERIAL_BAUD);
  Serial.println();
  Serial.println(F("=== Hand-Crank Fan Installation Controller ==="));
  Serial.print(F("RELAY_ACTIVE_LOW     = "));
  Serial.println(RELAY_ACTIVE_LOW ? F("true (active-low)") : F("false (active-high)"));
  Serial.print(F("PULSES_PER_REV       = ")); Serial.println(PULSES_PER_REV);
  Serial.print(F("RPM_ON_THRESHOLD     = ")); Serial.println(RPM_ON_THRESHOLD);
  Serial.print(F("RPM_OFF_THRESHOLD    = ")); Serial.println(RPM_OFF_THRESHOLD);
  Serial.print(F("MIN_RUN_TIME_MS      = ")); Serial.println(MIN_RUN_TIME_MS);
  Serial.print(F("MIN_OFF_TIME_MS      = ")); Serial.println(MIN_OFF_TIME_MS);
  Serial.print(F("SENSOR_TIMEOUT_MS    = ")); Serial.println(SENSOR_TIMEOUT_MS);
  Serial.print(F("RPM_UPDATE_INTERVAL_MS = ")); Serial.println(RPM_UPDATE_INTERVAL_MS);
  Serial.print(F("RPM_STALL_TIMEOUT_MS   = ")); Serial.println(RPM_STALL_TIMEOUT_MS);
  Serial.println(F("==============================================="));

  unsigned long now = millis();
  lastRpmCalcTime      = now;
  stateEnteredTime     = now;
  lastPulseMillis      = now;
  lastValidPulseMicros = micros();

  // Allow the relay to switch ON immediately at startup if RPM is already
  // above threshold, rather than waiting out MIN_OFF_TIME_MS.
  relayLastOffTime = now - MIN_OFF_TIME_MS;

  attachInterrupt(digitalPinToInterrupt(HALL_SENSOR_PIN), onHallPulse, FALLING);
}

// =====================================================================
// RPM calculation
// =====================================================================
// At hand-crank speeds (well under 1 pulse/sec at PULSES_PER_REV = 1),
// counting pulses in a window gives very coarse steps. Instead, RPM is
// derived from the period between consecutive pulses, which varies
// continuously. Between pulses, the elapsed time since the last pulse is
// used as a floor so a slowing crank is reflected immediately rather than
// waiting for the next pulse to "confirm" it.
void calculateRpm() {
  unsigned long now = millis();
  if (now - lastRpmCalcTime < RPM_UPDATE_INTERVAL_MS) {
    return;
  }
  lastRpmCalcTime = now;

  noInterrupts();
  unsigned long interval  = pulseIntervalMicros;
  unsigned long lastValid = lastValidPulseMicros;
  unsigned long lastMs    = lastPulseMillis;
  interrupts();

  if (interval == 0 || (now - lastMs) >= RPM_STALL_TIMEOUT_MS) {
    instantRpm = 0.0;
  } else {
    unsigned long elapsedSincePulse = micros() - lastValid;
    if (elapsedSincePulse > interval) {
      interval = elapsedSincePulse;
    }
    instantRpm = 60000000.0 / (interval * (float)PULSES_PER_REV);
  }

  rpmSamples[rpmSampleIndex] = instantRpm;
  rpmSampleIndex = (rpmSampleIndex + 1) % RPM_SMOOTHING_SAMPLES;

  float sum = 0.0;
  for (uint8_t i = 0; i < RPM_SMOOTHING_SAMPLES; i++) {
    sum += rpmSamples[i];
  }
  smoothedRpm = sum / RPM_SMOOTHING_SAMPLES;
}

// =====================================================================
// State machine
// =====================================================================
void updateStateMachine() {
  unsigned long now = millis();

  // --- Fail-safe: relay energized but no sensor pulses for too long ---
  if (relayEnergized && (now - lastPulseMillis > SENSOR_TIMEOUT_MS)) {
    Serial.println(F("FAULT: no Hall sensor pulses received - forcing relay OFF"));
    setRelay(false);
    relayLastOffTime = now;
    transitionTo(IDLE);
    return;
  }

  switch (currentState) {
    case IDLE:
      if (smoothedRpm >= RPM_ON_THRESHOLD &&
          (now - relayLastOffTime) >= MIN_OFF_TIME_MS) {
        setRelay(true);
        transitionTo(RUNNING);
      }
      break;

    case RUNNING:
      if (smoothedRpm < RPM_OFF_THRESHOLD) {
        // Relay stays ON - waiting out MIN_RUN_TIME_MS before allowing OFF.
        transitionTo(COOLDOWN);
      }
      break;

    case COOLDOWN:
      if (smoothedRpm >= RPM_OFF_THRESHOLD) {
        // RPM recovered before MIN_RUN_TIME_MS elapsed.
        transitionTo(RUNNING);
      } else if ((now - relayOnSince) >= MIN_RUN_TIME_MS) {
        setRelay(false);
        relayLastOffTime = now;
        transitionTo(IDLE);
      }
      break;
  }
}

// =====================================================================
// Status LED
// =====================================================================
void updateStatusLed() {
  unsigned long now = millis();

  switch (currentState) {
    case IDLE:
      digitalWrite(STATUS_LED_PIN, LOW);
      break;

    case RUNNING:
      digitalWrite(STATUS_LED_PIN, HIGH);
      break;

    case COOLDOWN:
      if (now - lastLedToggle >= LED_COOLDOWN_BLINK_MS) {
        lastLedToggle = now;
        ledOn = !ledOn;
        digitalWrite(STATUS_LED_PIN, ledOn ? HIGH : LOW);
      }
      break;
  }
}

// =====================================================================
// Debug output
// =====================================================================
void printDebug() {
  unsigned long now = millis();
  if (now - lastSerialPrint < SERIAL_PRINT_INTERVAL_MS) {
    return;
  }
  lastSerialPrint = now;

  Serial.print(F("t="));
  Serial.print(now);
  Serial.print(F(" state="));
  Serial.print(stateName(currentState));
  Serial.print(F(" ("));
  Serial.print(now - stateEnteredTime);
  Serial.print(F("ms) instRPM="));
  Serial.print(instantRpm);
  Serial.print(F(" avgRPM="));
  Serial.print(smoothedRpm);
  Serial.print(F(" relay="));
  Serial.println(relayEnergized ? F("ON") : F("OFF"));
}

// =====================================================================
// Main loop - millis()-based, no delay()
// =====================================================================
void loop() {
  calculateRpm();
  updateStateMachine();
  updateStatusLed();
  printDebug();
}
