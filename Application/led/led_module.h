/**
  ******************************************************************************
  * @file    led_module.h
  * @brief   STAT_LED state machine driver.
  *
  *  Modes:
  *    - LED_MODE_ALW_OFF:        LED is permanently off.
  *    - LED_MODE_ALW_ON:         LED is permanently on.
  *    - LED_MODE_STATE_MACHINE:  LED follows the device state.
  *
  *  States in LED_MODE_STATE_MACHINE:
  *    - LED_STATE_NO_POLLING:     1 short blink every 3 s.
  *    - LED_STATE_POLLING:        2 short blinks every 1.5 s.
  *    - LED_STATE_FACTORY_RESET:  10 short blinks (one-shot, then revert).
  ******************************************************************************
  */
#ifndef APPLICATION_LED_MODULE_H
#define APPLICATION_LED_MODULE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    LED_STATE_NO_POLLING     = 0,
    LED_STATE_POLLING        = 1,
    LED_STATE_FACTORY_RESET  = 2,
} led_state_t;

/** Initialise the LED module with a given mode (see settings.h). */
void led_module_init(uint8_t mode);

/** Change the operating mode at runtime (see led_mode_t in settings.h). */
void led_module_set_mode(uint8_t mode);

/** Get the currently active operating mode. */
uint8_t led_module_get_mode(void);

/** Set the device state used by the state-machine mode. */
void led_module_set_state(led_state_t state);

/** Trigger the factory-reset blink burst (one-shot). */
void led_module_signal_factory_reset(void);

/**
 * Periodic tick — call from a single task with a fixed period. The default
 * tick used in the application is 10 ms; the module timing is expressed in
 * ticks so other periods are usable as long as `tick_ms` is passed.
 */
void led_module_tick(uint16_t tick_ms);

#ifdef __cplusplus
}
#endif

#endif /* APPLICATION_LED_MODULE_H */
