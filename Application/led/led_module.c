/**
  ******************************************************************************
  * @file    led_module.c
  * @brief   Time-triggered LED state machine. The driver consumes a fixed
  *          tick period (typically 10 ms) and emits a blink pattern that
  *          depends on the selected operating mode and reported device state.
  ******************************************************************************
  */

#include "led_module.h"

#include "main.h"
#include "settings.h"
#include "stm32f4xx_hal.h"

/* ---------------------------------------------------------------------------
 * Pattern timings, milliseconds
 * ------------------------------------------------------------------------- */
#define LED_PULSE_ON_MS         50u
#define LED_PULSE_GAP_MS        150u    /* off-time between pulses inside one burst */

#define LED_NOPOLL_PERIOD_MS    3000u
#define LED_POLLING_PERIOD_MS   1500u
#define LED_FRESET_PERIOD_MS    250u    /* purely cosmetic for reset burst */

#define LED_NOPOLL_PULSES       1u
#define LED_POLLING_PULSES      2u
#define LED_FRESET_PULSES       10u

/* ---------------------------------------------------------------------------
 * Internal state
 * ------------------------------------------------------------------------- */
typedef struct {
    uint16_t pulses;            /* pulses in the current burst         */
    uint16_t period_ms;         /* total period including idle gap     */
    uint16_t pulse_idx;         /* currently emitted pulse, 0-based    */
    uint16_t timer_ms;          /* time elapsed in current sub-state   */
    uint8_t  on_phase;          /* 1 = ON, 0 = OFF                     */
    uint8_t  finished_burst;    /* when set, we are in the trailing gap*/
    uint8_t  one_shot;          /* set during factory-reset burst      */
} led_pattern_t;

static uint8_t        s_mode = LED_MODE_STATE_MACHINE;
static led_state_t    s_state = LED_STATE_NO_POLLING;
static led_state_t    s_pending_state_after_oneshot = LED_STATE_NO_POLLING;
static led_pattern_t  s_pattern;

/* ---------------------------------------------------------------------------
 * Helpers
 * ------------------------------------------------------------------------- */
static inline void led_set(bool on)
{
    HAL_GPIO_WritePin(STAT_LED_GPIO_Port, STAT_LED_Pin,
                      on ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

static void led_load_pattern(led_state_t st, uint8_t one_shot)
{
    s_pattern.pulse_idx      = 0u;
    s_pattern.timer_ms       = 0u;
    s_pattern.on_phase       = 0u;
    s_pattern.finished_burst = 0u;
    s_pattern.one_shot       = one_shot;

    switch (st) {
    case LED_STATE_POLLING:
        s_pattern.pulses    = LED_POLLING_PULSES;
        s_pattern.period_ms = LED_POLLING_PERIOD_MS;
        break;
    case LED_STATE_FACTORY_RESET:
        s_pattern.pulses    = LED_FRESET_PULSES;
        s_pattern.period_ms = LED_FRESET_PERIOD_MS;
        break;
    case LED_STATE_NO_POLLING:
    default:
        s_pattern.pulses    = LED_NOPOLL_PULSES;
        s_pattern.period_ms = LED_NOPOLL_PERIOD_MS;
        break;
    }

    led_set(false);
}

/* ---------------------------------------------------------------------------
 * API
 * ------------------------------------------------------------------------- */
void led_module_init(uint8_t mode)
{
    s_mode = mode;
    s_state = LED_STATE_NO_POLLING;
    led_load_pattern(s_state, 0u);

    if (mode == LED_MODE_ALW_ON) {
        led_set(true);
    } else {
        led_set(false);
    }
}

void led_module_set_mode(uint8_t mode)
{
    if (mode == s_mode) {
        return;
    }
    s_mode = mode;
    if (mode == LED_MODE_ALW_ON) {
        led_set(true);
    } else if (mode == LED_MODE_ALW_OFF) {
        led_set(false);
    } else {
        led_load_pattern(s_state, 0u);
    }
}

uint8_t led_module_get_mode(void)
{
    return s_mode;
}

void led_module_set_state(led_state_t state)
{
    if (s_pattern.one_shot) {
        /* defer the change until the burst finishes */
        s_pending_state_after_oneshot = state;
        return;
    }
    if (state == s_state) {
        return;
    }
    s_state = state;
    if (s_mode == LED_MODE_STATE_MACHINE) {
        led_load_pattern(s_state, 0u);
    }
}

void led_module_signal_factory_reset(void)
{
    s_pending_state_after_oneshot = s_state;
    led_load_pattern(LED_STATE_FACTORY_RESET, 1u);
}

/* ---------------------------------------------------------------------------
 * Tick
 * ------------------------------------------------------------------------- */
void led_module_tick(uint16_t tick_ms)
{
    if (s_mode == LED_MODE_ALW_ON) {
        led_set(true);
        return;
    }
    if (s_mode == LED_MODE_ALW_OFF && !s_pattern.one_shot) {
        led_set(false);
        return;
    }

    s_pattern.timer_ms = (uint16_t)(s_pattern.timer_ms + tick_ms);

    if (!s_pattern.finished_burst) {
        if (s_pattern.on_phase) {
            if (s_pattern.timer_ms >= LED_PULSE_ON_MS) {
                /* end of ON phase -> start gap */
                led_set(false);
                s_pattern.on_phase = 0u;
                s_pattern.timer_ms = 0u;
                s_pattern.pulse_idx++;
                if (s_pattern.pulse_idx >= s_pattern.pulses) {
                    s_pattern.finished_burst = 1u;
                }
            }
        } else {
            if (s_pattern.timer_ms >= LED_PULSE_GAP_MS) {
                /* start next ON pulse */
                led_set(true);
                s_pattern.on_phase = 1u;
                s_pattern.timer_ms = 0u;
            }
        }
        return;
    }

    /* In the trailing idle period, until period_ms has elapsed in total. */
    const uint32_t burst_duration =
        (uint32_t)s_pattern.pulses * (LED_PULSE_ON_MS + LED_PULSE_GAP_MS);
    const uint32_t elapsed_total =
        burst_duration + (uint32_t)s_pattern.timer_ms;

    if (elapsed_total >= s_pattern.period_ms) {
        if (s_pattern.one_shot) {
            led_load_pattern(s_pending_state_after_oneshot, 0u);
            s_state = s_pending_state_after_oneshot;
            return;
        }
        /* Restart the same burst. */
        led_load_pattern(s_state, 0u);
    }
}
