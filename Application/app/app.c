/**
  ******************************************************************************
  * @file    app.c
  * @brief   Application orchestrator. Wires the modular drivers together,
  *          handles factory-reset, brings up the network interface in the
  *          configured mode and runs the housekeeping loop (LED ticks,
  *          watchdog, deferred Modbus actions).
  ******************************************************************************
  */

#include "app.h"

#include "cmsis_os.h"
#include "iwdg.h"
#include "lwip/api.h"
#include "lwip/dhcp.h"
#include "lwip/netif.h"
#include "main.h"
#include "stm32f4xx_hal.h"

#include "button_module.h"
#include "di_module.h"
#include "ksz8863.h"
#include "led_module.h"
#include "modbus_app.h"
#include "modbus_tcp_server.h"
#include "settings.h"

/* The LwIP MX_LWIP_Init() exposes its struct netif so that we can override
 * the addressing after MX_LWIP_Init() has run. */
extern struct netif gnetif;

/* Module accessors implemented in modbus_app.c. */
uint8_t modbus_app_take_pending_save(void);
uint8_t modbus_app_take_pending_reboot(void);
uint8_t modbus_app_take_pending_factory_reset(void);
uint32_t modbus_app_last_request_tick(void);

/* Latest result of the boot-time KSZ8863 SMI self-test. Exposed as a flag
 * for any module that wants to react to the switch being unreachable
 * (e.g. an LED pattern, future Modbus diagnostic registers). */
static bool     s_ksz8863_present = false;
static uint16_t s_ksz8863_id1     = 0u;
static uint16_t s_ksz8863_id2     = 0u;

/* ---------------------------------------------------------------------------
 * Sub-tasks
 * ------------------------------------------------------------------------- */

/* DI sampling task: 1 ms period, drives the anti-bounce filter. */
static void di_task(void* arg)
{
    (void)arg;
    uint32_t tick = osKernelGetTickCount();
    for (;;) {
        di_module_tick();
        tick += 1u;
        osDelayUntil(tick);
    }
}

/* LED state machine: 10 ms tick. */
static void led_task(void* arg)
{
    (void)arg;
    const uint16_t period_ms = 10u;
    uint32_t tick = osKernelGetTickCount();
    for (;;) {
        led_module_tick(period_ms);
        tick += period_ms;
        osDelayUntil(tick);
    }
}

/* ---------------------------------------------------------------------------
 * Factory reset routine
 * ------------------------------------------------------------------------- */
static void perform_factory_reset(void)
{
    led_module_signal_factory_reset();
    settings_reset_to_defaults();
    (void)settings_save();

    /* Give the LED time to play the burst (10 short pulses) before reboot. */
    const uint32_t deadline = HAL_GetTick() + 3500u;
    while (HAL_GetTick() < deadline) {
        HAL_IWDG_Refresh(&hiwdg);
        osDelay(50);
    }
    NVIC_SystemReset();
}

/* ---------------------------------------------------------------------------
 * Network bring-up
 * ------------------------------------------------------------------------- */
static void apply_network_config(void)
{
    const settings_t* s = settings_get();

    if (s->use_dhcp) {
        /* MX_LWIP_Init() already started DHCP. Nothing more to do. */
        return;
    }

    /* Static configuration: stop DHCP if it happens to be running and load
     * the static address. */
    dhcp_release_and_stop(&gnetif);

    ip4_addr_t ip, mask, gw;
    IP4_ADDR(&ip,   s->ip[0],      s->ip[1],      s->ip[2],      s->ip[3]);
    IP4_ADDR(&mask, s->netmask[0], s->netmask[1], s->netmask[2], s->netmask[3]);
    IP4_ADDR(&gw,   s->gateway[0], s->gateway[1], s->gateway[2], s->gateway[3]);

    netif_set_addr(&gnetif, &ip, &mask, &gw);
}

/* ---------------------------------------------------------------------------
 * Housekeeping
 * ------------------------------------------------------------------------- */
static void update_led_state_from_traffic(void)
{
    const uint32_t now      = HAL_GetTick();
    const uint32_t last_req = modbus_app_last_request_tick();

    /* "Polling" if a Modbus client is currently connected AND we have seen
     * a request in the last 5 s. Otherwise "no polling". */
    extern bool modbus_tcp_server_has_client(void);
    const bool has_client    = modbus_tcp_server_has_client();
    const bool recent_traffic = (last_req != 0u) && ((now - last_req) <= 5000u);

    led_module_set_state((has_client && recent_traffic)
                         ? LED_STATE_POLLING
                         : LED_STATE_NO_POLLING);
}

/* ---------------------------------------------------------------------------
 * Public entry point
 * ------------------------------------------------------------------------- */
void app_run(void)
{
    /* Load settings (or defaults) before anything that consumes them. */
    (void)settings_init();
    settings_t* s = settings_get();

    /* If the FACT_RES button is held at startup, blank-load defaults. We do
     * this synchronously here so that the rest of the boot uses defaults. */
    if (button_wait_held(BUTTON_HOLD_FOR_FACTORY_RESET_MS)) {
        /* Initialise the LED first so that the burst is visible. */
        led_module_init((uint8_t)s->led_mode);
        perform_factory_reset();
        /* Not reached. */
    }

    /* Initialise hardware drivers. */
    di_module_init(s->di_filter_ms);
    led_module_init((uint8_t)s->led_mode);
    modbus_app_init();

    /* Apply network configuration (static or DHCP). */
    apply_network_config();

    /* HAL_ETH_Init() has already run by the time we get here (LwIP init
     * called it from low_level_init()), so SMI/MIIM access is available.
     * Probe the switch once: if it answers with a Micrel OUI we know the
     * RMII/MDIO bus is healthy. The result is kept in static state for
     * other modules to consult. */
    s_ksz8863_present = ksz8863_self_test(&s_ksz8863_id1, &s_ksz8863_id2);

    /* Spawn periodic tasks. */
    const osThreadAttr_t di_attr  = {
        .name = "DI", .stack_size = 384, .priority = osPriorityHigh
    };
    osThreadNew(di_task, NULL, &di_attr);

    const osThreadAttr_t led_attr = {
        .name = "LED", .stack_size = 256, .priority = osPriorityLow
    };
    osThreadNew(led_task, NULL, &led_attr);

    /* Spawn Modbus TCP server. */
    modbus_tcp_server_start();

    /* Housekeeping loop: feeds the watchdog, processes deferred actions and
     * keeps the LED state in sync with the Modbus traffic. */
    uint32_t tick = osKernelGetTickCount();
    for (;;) {
        HAL_IWDG_Refresh(&hiwdg);
        update_led_state_from_traffic();

        if (modbus_app_take_pending_save()) {
            (void)settings_save();
        }
        if (modbus_app_take_pending_factory_reset()) {
            perform_factory_reset();
            /* Not reached. */
        }
        if (modbus_app_take_pending_reboot()) {
            const uint32_t deadline = HAL_GetTick() + 200u;
            while (HAL_GetTick() < deadline) {
                HAL_IWDG_Refresh(&hiwdg);
                osDelay(20);
            }
            NVIC_SystemReset();
        }

        tick += 100u;
        osDelayUntil(tick);
    }
}
