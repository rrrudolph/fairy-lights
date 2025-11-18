// D1 Mini ESP8266 Crossfade: High Freq Digital Blend (Cool Dimmed 70%)
// Pins: D5=14 (warm), D6=12 (cool), D7=13 (SPDT HIGH=0 crossfade/LOW=1 all on), A0=Pot
// Modes: Poll switch; 6s cycle, 500Hz blend
// Speed: ~5kHz updates, <1μs jitter with table

#include <math.h>  // For pow (if needed; table for sin)

const int IN1_PIN = 14;  // Warm
const int IN2_PIN = 12;  // Cool
const int SWITCH_PIN = 13;  // SPDT (HIGH=0, LOW=1)
const int POT_PIN = A0;  // Pot

// Config
const unsigned long CROSSFADE_SPEED = 5000;  // ms cycle
const int AC_FREQ = 500;  // Hz blend
const float COOL_FACTOR = 0.65;  // Cool dim
const float MIN_RATIO = 0.05;  // 5% min
const float MAX_RATIO = 0.99;  // Max range
const float GAMMA = 1.5;  // Contrast
int current_mode = 0;  // 0=crossfade, 1=all on
float phase = 0.0;
unsigned long last_step = 0;
unsigned long last_status = 0;
float pot_mult = 1.0;
const unsigned long HALF_P_US = 500000UL / AC_FREQ;  // ~1000us

// Sine table: 3600 entries (0.1° res, fast lookup)
const int NUM_STEPS = 3600;
float sine_table[NUM_STEPS];  // Precompute in setup

void setup() {
  pinMode(IN1_PIN, OUTPUT);
  pinMode(IN2_PIN, OUTPUT);
  pinMode(SWITCH_PIN, INPUT_PULLUP);
  // Precompute sine table
  for (int i = 0; i < NUM_STEPS; i++) {
    sine_table[i] = sin(2 * PI * i / NUM_STEPS);
  }

  Serial.begin(9600);
  Serial.println("Crossfade started");
}

void loop() {
  unsigned long now = millis();

  // Poll SPDT (no debounce for latching)
  int switch_state = digitalRead(SWITCH_PIN);
  int new_mode = (switch_state == HIGH) ? 0 : 1;
  if (new_mode != current_mode) {
    current_mode = new_mode;
    phase = 0.0;
    last_step = now;
    both_off();
  }

  crossfade_update();

  delayMicroseconds(500);  // ~2kHz loop (0.5ms yield)
}

void both_off() {
  digitalWrite(IN1_PIN, LOW);
  digitalWrite(IN2_PIN, LOW);
}

void crossfade_update() {
  unsigned long now = micros();
  unsigned long delta_us = now - last_step;
  if (delta_us >= 5000) {  // 5ms updates (~200Hz granularity)
    // Time-based phase (keeps 6s cycle)
    phase += (2 * PI * delta_us / 1000.0) / CROSSFADE_SPEED;
    if (phase >= 2 * PI) phase -= 2 * PI;
    // Pot read
    int pot_raw = analogRead(POT_PIN);
    pot_mult = pot_raw / 1023.0;
    // Fast table lookup
    int step = (int)(phase * NUM_STEPS / (2 * PI)) % NUM_STEPS;
    float sin_v = sine_table[step];
    // Warm
    float w_lin = MIN_RATIO + MAX_RATIO * (1.0 + sin_v) / 2;
    float w_r = pow(w_lin, GAMMA);
    unsigned long w_on = (unsigned long)(HALF_P_US * w_r * pot_mult);
    // Cool
    float c_lin = MIN_RATIO + MAX_RATIO * (1.0 - sin_v) / 2;
    float c_r = pow(c_lin, GAMMA) * COOL_FACTOR;
    unsigned long c_on = (unsigned long)(HALF_P_US * c_r * pot_mult);
    alternate_polarity(w_on, c_on);
    last_step = now;
  }
}

void all_on_update() {
  unsigned long now = micros();
  unsigned long delta_us = now - last_step;
  if (delta_us >= 5000) {
    int pot_raw = analogRead(POT_PIN);
    pot_mult = pot_raw / 1023.0;
    float sin_v = 0.0;  // 50/50
    float w_lin = MIN_RATIO + MAX_RATIO * (1.0 + sin_v) / 2;
    float w_r = pow(w_lin, GAMMA);
    unsigned long w_on = (unsigned long)(HALF_P_US * w_r * pot_mult);
    float c_lin = MIN_RATIO + MAX_RATIO * (1.0 - sin_v) / 2;
    float c_r = pow(c_lin, GAMMA) * COOL_FACTOR;
    unsigned long c_on = (unsigned long)(HALF_P_US * c_r * pot_mult);
    alternate_polarity(w_on, c_on);
    last_step = now;
  }
}

void alternate_polarity(unsigned long w_on, unsigned long c_on) {
  // Warm half
  digitalWrite(IN1_PIN, HIGH);
  digitalWrite(IN2_PIN, LOW);
  delayMicroseconds(w_on);
  digitalWrite(IN1_PIN, LOW);
  delayMicroseconds(HALF_P_US - w_on);
  // Cool half
  digitalWrite(IN1_PIN, LOW);
  digitalWrite(IN2_PIN, HIGH);
  delayMicroseconds(c_on);
  digitalWrite(IN2_PIN, LOW);
  delayMicroseconds(HALF_P_US - c_on);
}