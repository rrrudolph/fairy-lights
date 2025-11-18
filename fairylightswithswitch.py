# Optimized Digital Crossfade: SPDT Switch + Pot Dim (No Debounce, Max Speed)
# Pins: D5=IN1 (warm), D6=IN2 (cool), D7=SPDT (HIGH=0 crossfade, LOW=1 all on), A0=Pot
# Modes: Direct poll (no debounce for latching SPDT)
# Starts in crossfade; switch flips instant
# Speed: 1ms updates, minimal ops, 1ms yield—~1000Hz loop for silky LEDs

import machine
import utime
import math
from machine import Pin, ADC

# Pin dict
pinmap = {
    'D5': 14,  # Warm
    'D6': 12,  # Cool
    'D7': 13,  # SPDT
    'A0': 0,   # Pot
}

# Pins
IN1 = Pin(pinmap['D5'], Pin.OUT)
IN2 = Pin(pinmap['D6'], Pin.OUT)
SWITCH_PIN = Pin(pinmap['D7'], Pin.IN, Pin.PULL_UP)  # HIGH=0, LOW=1
POT_PIN = ADC(pinmap['A0'])

# Config (tuned for speed/granularity)
crossfade_speed = 5000  # ms cycle
ac_freq = 500  # Hz blend
cool_brightness_factor = 0.45  # Cool dim
min_r = 0.05  # Min ratio
max_r = 0.95  # Max range
gamma = 1.5  # Contrast
current_mode = 0  # Default crossfade
phase = 0.0
last_step = 0
half_p_us = 500000 // ac_freq  # Precompute ~1000us

# Sine lookup table for speed: 3600 entries (0.1° resolution, ~0.03% granularity)
NUM_STEPS = 3600
sine_table = [math.sin(2 * math.pi * i / NUM_STEPS) for i in range(NUM_STEPS)]
phase_to_step = lambda p: int((p % (2 * math.pi)) * NUM_STEPS / (2 * math.pi)) % NUM_STEPS

def both_off():
    IN1.off()
    IN2.off()

def crossfade_update():
    global phase, last_step, pot_multiplier
    now = utime.ticks_ms()
    delta_ms = utime.ticks_diff(now, last_step)
    if delta_ms >= 5:  # Granular 5ms updates
        phase += (2 * math.pi * delta_ms) / crossfade_speed
        if phase >= 2 * math.pi:
            phase -= 2 * math.pi
        # Read pot fresh
        pot_raw = POT_PIN.read()
        pot_multiplier = pot_raw / 1023.0
        # Fast table lookup instead of sin
        step = phase_to_step(phase)
        sin_val = sine_table[step]
        half_period_us = 500000 // ac_freq
        # Warm with gamma
        warm_linear = min_r + max_r * (1.0 + sin_val) / 2
        warm_ratio = pow(warm_linear, gamma)
        warm_on_us = int(half_period_us * warm_ratio * pot_multiplier)
        # Cool with gamma/cool_factor
        cool_linear = min_r + max_r * (1.0 - sin_val) / 2
        cool_ratio = pow(cool_linear, gamma) * cool_brightness_factor
        cool_on_us = int(half_period_us * cool_ratio * pot_multiplier)
        alternate_polarity(warm_on_us, cool_on_us)
        last_step = now

def all_on_update():
    global last_step, pot_mult
    now = utime.ticks_ms()
    delta = utime.ticks_diff(now, last_step)
    if delta >= 1:
        pot_mult = POT_PIN.read() / 1023.0
        sin_v = 0.0  # 50/50
        # Warm
        w_lin = min_r + max_r * (1.0 + sin_v) / 2
        w_r = pow(w_lin, gamma)
        w_on = int(half_p_us * w_r * pot_mult)
        # Cool
        c_lin = min_r + max_r * (1.0 - sin_v) / 2
        c_r = pow(c_lin, gamma) * cool_factor
        c_on = int(half_p_us * c_r * pot_mult)
        alternate_polarity(w_on, c_on)
        last_step = now

def alternate_polarity(w_on, c_on):
    # Warm half
    IN1.on(); IN2.off()
    utime.sleep_us(w_on)
    IN1.off()
    utime.sleep_us(half_p_us - w_on)
    # Cool half
    IN1.off(); IN2.on()
    utime.sleep_us(c_on)
    IN2.off()
    utime.sleep_us(half_p_us - c_on)

# Main loop: Max speed
while True:
    now = utime.ticks_ms()
    # Direct SPDT poll (no debounce)
    new_mode = 0 if SWITCH_PIN.value() == 1 else 1
    if new_mode != current_mode:
        current_mode = new_mode
        phase = 0.0
        last_step = now
        both_off()
        print(f"Mode: {current_mode}")

    if current_mode == 0:
        crossfade_update()
    elif current_mode == 1:
        all_on_update()

    utime.sleep_ms(1)  # Minimal yield for speed