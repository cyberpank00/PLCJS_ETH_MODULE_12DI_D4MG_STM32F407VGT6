# PLCJS_ETH_MODULE_12DI_D4MG_STM32F407VGT6

Прошивка модуля **12 дискретных входов** с интерфейсом Ethernet.
Сбор сигналов с настраиваемой анти-дребезговой фильтрацией, выдача состояний и приём настроек по **Modbus TCP**, конфигурация во внутреннем Flash, кнопка `FACT_RES` для возврата к заводским установкам, светодиод `STAT_LED` как индикатор состояния опроса.

---

## 1. Состав железа

| Узел                  | Чип / Сигнал           | Заметки                                                   |
|-----------------------|------------------------|-----------------------------------------------------------|
| MCU                   | STM32F407VGT6          | Cortex-M4F, 168 МГц, 1 МБ Flash, 192 КБ SRAM (128 + 64 CCM)|
| Ethernet PHY          | KSZ8863MLLI            | Интерфейс RMII; работает как unmanaged switch             |
| Тактирование PHY      | 50 МГц от REFCLKO_3 → REFCLKI_3 | внутренний генератор PHY                          |
| Управление PHY        | `ETHRST` (PD11), `ETHINT` (PB1) | Reset / прерывание                                |
| Дискретные входы      | 12 шт., active-low (на землю), pull-up внутри MCU | см. таблицу пинов ниже              |
| Кнопка `FACT_RES`     | PC8, active-low        | Заводской сброс по удержанию при старте                   |
| Светодиод `STAT_LED`  | PC6, active-high       | Индикация состояния модуля                                |
| Watchdog              | IWDG                   | Кикается из главного цикла приложения                     |

### Распиновка дискретных входов

| Канал (Modbus reg.) | Силкскрин | Пин MCU |
|---------------------|-----------|---------|
| 0                   | DI1       | PB3     |
| 1                   | DI2       | PD7     |
| 2                   | DI3       | PD6     |
| 3                   | DI4       | PD5     |
| 4                   | DI5       | PD4     |
| 5                   | DI6       | PD3     |
| 6                   | DI7       | PD2     |
| 7                   | DI8       | PD1     |
| 8                   | DI9       | PD0     |
| 9                   | DI10      | PC12    |
| 10                  | DI11      | PC11    |
| 11                  | DI12      | PC10    |

> Логика инвертирована: на входе появилась 1 → пин притянут к земле → бит DI = 1.

Схема платы — `DOC/Schematic_ETH_12DI_STM32F407.pdf`.

---

## 2. Сборка и прошивка

Проект сгенерирован STM32CubeMX, сборка через CMake + Ninja с тулчейном **STARM Clang** из STM32CubeCLT.

### Зависимости (Windows)

* [STM32CubeCLT 1.18+](https://www.st.com/en/development-tools/stm32cubeclt.html) — даёт `starm-clang`, `STM32_Programmer_CLI`, GDB-сервер, CMake и Ninja.
* [Windsurf](https://codeium.com/windsurf) (или VS Code) с расширением **CMake Tools**.
* (опционально) **STM32 VS Code Extension** от STMicroelectronics — даёт кнопку Flash и интеграцию с ST-Link.

После установки CubeCLT убедитесь, что в `PATH` есть `starm-clang`, `ninja`, `cmake`:
```powershell
where starm-clang
where ninja
where cmake
```

### Сборка

```powershell
cmake -S . -B build/Debug -G Ninja `
  -DCMAKE_BUILD_TYPE=Debug `
  -DCMAKE_TOOLCHAIN_FILE=cmake/starm-clang.cmake
cmake --build build/Debug
```

Или просто **Configure → Build (F7)** в Windsurf.

Результат — `build/Debug/PLCJS_ETH_MODULE_12DI_D4MG_STM32F407VGT6.elf`.

### Прошивка через ST-Link

```powershell
STM32_Programmer_CLI -c port=SWD -w build/Debug/PLCJS_ETH_MODULE_12DI_D4MG_STM32F407VGT6.elf -v -rst
```

---

## 3. Алгоритм работы

### 3.1. Запуск

```
Reset_Handler
  └ SystemInit → main()
        ├ HAL_Init        — SysTick 1 кГц
        ├ SystemClock_Config (HSE+PLL → 168 МГц)
        ├ MX_GPIO_Init    — все пины из .ioc (DI input+pull-up, FACT_RES, STAT_LED, RMII)
        ├ MX_DMA / MX_ETH / MX_USART / MX_RTC / MX_FATFS / MX_IWDG
        ├ MX_FREERTOS_Init  (создаёт defaultTask)
        └ osKernelStart   ──►  планировщик FreeRTOS
                                  │
                                  ▼
                            StartDefaultTask  (Core/Src/freertos.c)
                                  ├ MX_LWIP_Init   (поднимает TCP/IP стек)
                                  └ app_run()      (Application/app/app.c)
```

### 3.2. `app_run()` — оркестратор

1. `settings_init()` — читает settings из Flash sector 11 (`0x080E0000`), проверяет magic + version + CRC32. Если невалидно — заполняет дефолтами.
2. `led_module_init(settings.led_mode)` — STAT_LED стартует в состоянии `LED_STATE_NO_POLLING` (1 импульс / 3 с).
3. `button_wait_held(BUTTON_HOLD_FOR_FACTORY_RESET_MS)` — если `FACT_RES` держали ≥ 2000 мс при старте, выполняется `LED_STATE_FACTORY_RESET` (10 миганий) → `settings_reset_to_defaults()` → `settings_save()` → `NVIC_SystemReset()`.
4. `di_module_init(settings.di_filter_ms)` — таблица 12 пинов, обнуление счётчиков.
5. `modbus_app_init()` + `modbus_tcp_server_start(...)` — запускает задачу Modbus TCP-сервера на сконфигурированном порту.
6. **Главный цикл** (внутри той же defaultTask):
   * каждый 1 мс — `di_module_tick()` (счётчики стабильности, обновление маски)
   * каждые 10 мс — `led_module_tick(10)` (state-machine паттернов)
   * `HAL_IWDG_Refresh()`
   * Обработка отложенных команд от Modbus:
     * `app_request_save` → `settings_save()`
     * `app_request_reboot` → `osDelay(100)` → `NVIC_SystemReset()`
     * `app_request_factory_reset` → 10 миганий → `settings_reset_to_defaults()` → `save` → `reset`

### 3.3. Параллельные задачи

| Задача             | Создаёт              | Что делает                                                              |
|--------------------|----------------------|-------------------------------------------------------------------------|
| `defaultTask`      | CubeMX               | Хост приложения: тики DI/LED, IWDG, deferred-команды                    |
| `tcpip_thread`     | LwIP (`MX_LWIP_Init`)| Обработка TCP/IP / ARP / DHCP                                           |
| `EthIf` thread     | LwIP                 | Чтение пакетов из DMA-буфера ETH                                        |
| `mb_tcp_server`    | `modbus_tcp_server_start` | listen/accept, recv → `nmbs_server_poll` → callback'и → write       |

---

## 4. Карта регистров Modbus

Slave id по умолчанию = **1** (для TCP несущественен; задаётся через регистр HR 102).
Порт по умолчанию = **502** (HR 103).

### Discrete Inputs (FC02) — read-only

| Адрес     | Что    | Описание                           |
|-----------|--------|------------------------------------|
| 0…11      | DI1…DI12 | Битовое состояние входа после фильтра |

### Input Registers (FC04) — read-only

| Адрес | Что                | Описание                                                     |
|-------|--------------------|--------------------------------------------------------------|
| 0…11  | DI1…DI12           | 0 / 1 — отфильтрованное состояние входа                      |
| 120   | FW version major   |                                                              |
| 121   | FW version minor   |                                                              |
| 122   | uptime[low]        | uptime, секунды, младшее слово                               |
| 123   | uptime[high]       | uptime, секунды, старшее слово                               |
| 124   | DI mask            | 12-битная маска состояний DI1…DI12                           |

### Holding Registers (FC03 / FC06 / FC10) — read/write

| Адрес  | Имя              | Тип   | Диапазон / значения              | Применяется |
|--------|------------------|-------|----------------------------------|-------------|
| 100    | `DI_FILTER_MS`   | R/W   | 10 … 1000, по умолчанию 50       | сразу       |
| 101    | `LED_MODE`       | R/W   | 0 = ALW_OFF, 1 = ALW_ON, 2 = STATE_MACHINE (def) | сразу |
| 102    | `SLAVE_ID`       | R/W   | 1 … 247                          | сразу       |
| 103    | `TCP_PORT`       | R/W   | 1 … 65535, по умолчанию 502      | после save+reboot |
| 104…107| `IP_BASE`        | R/W   | по 1 октету на регистр (104=octet0…107=octet3) | после save+reboot |
| 108…111| `NETMASK_BASE`   | R/W   | аналогично                       | после save+reboot |
| 112…115| `GATEWAY_BASE`   | R/W   | аналогично                       | после save+reboot |
| 116    | `USE_DHCP`       | R/W   | 0 / 1, по умолчанию 0            | после save+reboot |
| 117    | `TRIG_SAVE`      | W     | запись `0xA5A5` → save в Flash   | моментально |
| 118    | `TRIG_REBOOT`    | W     | запись `0xB00B` → soft reset     | через ~100 мс |
| 119    | `TRIG_FACTORY_RESET` | W | запись `0xDEAD` → defaults + save + reset | через ~2.5 с (с миганием) |

> **Важно:** сетевые параметры (IP/mask/gw/port/DHCP) записываются в RAM-кэш и применяются **только после** записи `TRIG_SAVE` + `TRIG_REBOOT` (или `TRIG_FACTORY_RESET`). Так защищены от случайной потери связи.

Любая запись в зарезервированный или защищённый регистр возвращает `ILLEGAL_DATA_ADDRESS`; запись значения вне диапазона — `ILLEGAL_DATA_VALUE`.

### Пример клиента

```bash
# modpoll, slave id = 1, port = 502
# Прочитать состояния всех 12 DI как input registers
modpoll -m tcp -a 1 -t 4 -r 1 -c 12 192.168.1.10

# Установить фильтр 200 мс и сохранить
modpoll -m tcp -a 1 -t 4:hex -r 101 -1 192.168.1.10 0x00C8   # HR100 = 200
modpoll -m tcp -a 1 -t 4:hex -r 118 -1 192.168.1.10 0xA5A5   # HR117 = save
```

(в `modpoll` адресация регистров с 1: -r 101 = HR 100, -r 118 = HR 117).

---

## 5. Настройки

Структура `settings_t` (см. `Application/settings/settings.h`) хранится во внутреннем Flash, **сектор 11** (`0x080E0000`–`0x080FFFFF`, 128 КБ — заведомо больше необходимого, чтобы переписывание не задело прошивку).

Поля:

| Поле               | Дефолт          | Описание                                |
|--------------------|-----------------|-----------------------------------------|
| `magic`            | `0x12D14A57`    | Сигнатура валидности                    |
| `version`          | `1`             | Бамп при изменении layout               |
| `di_filter_ms`     | 50              | Анти-дребезг, мс (10…1000)              |
| `led_mode`         | 2 (`STATE_MACHINE`) | 0/1/2                                |
| `modbus_tcp_port`  | 502             |                                         |
| `modbus_slave_id`  | 1               |                                         |
| `use_dhcp`         | 0               | 0 = static, 1 = DHCP                    |
| `ip[4]`            | 192.168.1.10    |                                         |
| `netmask[4]`       | 255.255.255.0   |                                         |
| `gateway[4]`       | 192.168.1.1     |                                         |
| `crc32`            | вычисляемый     | CRC32 (HAL) по всем предыдущим байтам   |

**Сохранение во Flash:** `settings_save()` стирает сектор 11 целиком (`HAL_FLASHEx_Erase`) и пишет структуру по 4 байта (`HAL_FLASH_Program(WORD)`). Текущий вариант — синхронная блокирующая запись, ~0.5–2 с с прерыванием прерываний.

---

## 6. STAT_LED

Управление через `led_module_set_mode()` (от Modbus HR 101) и `led_module_set_state()` (от прикладной логики).

| Режим            | Поведение                                                          |
|------------------|--------------------------------------------------------------------|
| `ALW_OFF`        | Постоянно выключен                                                 |
| `ALW_ON`         | Постоянно включен                                                  |
| `STATE_MACHINE`  | Согласно состоянию (см. ниже)                                      |

Состояния в `STATE_MACHINE`:

| Состояние               | Паттерн                          | Когда                                |
|-------------------------|----------------------------------|--------------------------------------|
| `LED_STATE_NO_POLLING`  | 1 короткий импульс / 3 с         | Нет TCP-сессии с Modbus-мастером     |
| `LED_STATE_POLLING`     | 2 коротких импульса / 1.5 с      | TCP-сессия активна, идут запросы     |
| `LED_STATE_FACTORY_RESET`| 10 коротких импульсов (one-shot) | Идёт factory reset                   |

«Короткий импульс» = 100 мс ON / 200 мс OFF (см. `led_module.c`).

---

## 7. Кнопка FACT_RES

* Удержание `FACT_RES` ≥ 2000 мс **при подаче питания** → factory reset:
  1. STAT_LED делает 10 миганий
  2. `settings_t` заменяется дефолтами
  3. Запись в Flash
  4. `NVIC_SystemReset()`
* Удержание уже после старта **не реагирует** (намеренно — чтобы случайное нажатие в эксплуатации не сбросило настройки).
* Длительность удержания меняется константой `BUTTON_HOLD_FOR_FACTORY_RESET_MS` в `Application/button/button_module.h`.

---

## 8. Структура проекта

### Каталоги верхнего уровня (CubeMX-автогенерация)

| Путь                                | Что                                                          |
|-------------------------------------|--------------------------------------------------------------|
| `Core/`                             | `main.c`, `freertos.c`, `gpio.c`, `it.c`, `system_*` — автоген CubeMX |
| `Drivers/`                          | STM32 HAL/CMSIS, не редактируется                            |
| `Middlewares/Third_Party/FreeRTOS/` | FreeRTOS 10.x + CMSIS_V2 wrapper                             |
| `Middlewares/Third_Party/LwIP/`     | LwIP 2.1.2                                                   |
| `LWIP/`                             | `lwipopts.h` и Cube-glue для LwIP                            |
| `cmake/`                            | `starm-clang.cmake` — тулчейн-файл (генерирует CubeMX)       |
| `STM32F407XX_FLASH.ld`              | Линкер-скрипт                                                |
| `*.ioc`                             | Проект CubeMX (источник правды для пинов и периферии)        |
| `DOC/`                              | Принципиальная схема и документация                          |

### Прикладной код (`Application/`)

| Каталог / файл                              | Назначение                                                   |
|---------------------------------------------|--------------------------------------------------------------|
| `app/app.c`, `app.h`                        | Оркестратор: `app_run()`, deferred-команды, IWDG, тики DI/LED|
| `settings/settings.c`, `settings.h`         | NVS во Flash sector 11; magic/version/CRC32; runtime API     |
| `di/di_module.c`, `di_module.h`             | 12 каналов, pull-up, инвертированная логика, фильтр 10–1000 мс|
| `led/led_module.c`, `led_module.h`          | STAT_LED FSM: `ALW_ON` / `ALW_OFF` / `STATE_MACHINE`         |
| `button/button_module.c`, `button_module.h` | FACT_RES опрос на старте, ручка длительности                 |
| `modbus/modbus_app.c`, `modbus_app.h`       | Карта регистров, валидация, deferred-триггеры, callback'и nanoMODBUS |
| `modbus/modbus_tcp_server.c`, `modbus_tcp_server.h` | LwIP netconn slave, single-client, listen/accept loop |
| `third_party/nanomodbus/`                   | nanoMODBUS (MIT), server-only, без RTU                       |

### Места правок в автогене (помечены комментариями)

| Файл                          | Что добавлено                                                                 |
|-------------------------------|-------------------------------------------------------------------------------|
| `Core/Src/freertos.c`         | `StartDefaultTask` после `MX_LWIP_Init()` вызывает `app_run()` (USER CODE)    |
| `LWIP/Target/lwipopts.h`      | `LWIP_SO_RCVTIMEO=1`, `LWIP_SO_SNDTIMEO=1` в USER CODE BEGIN 1               |
| `CMakeLists.txt` (корень)     | Подключение `Application/` сорсов и инклудов, nanoMODBUS, `NMBS_CLIENT_DISABLED=1` |

---

## 9. nanoMODBUS — полезные мелочи

* Используется **server-only** сборка (`-DNMBS_CLIENT_DISABLED=1`) — экономит ~6 КБ Flash.
* Внутренний макрос `DEBUG(...)` в `nanomodbus.c` конфликтует с проектным `-DDEBUG`. В вендоре добавлен `#undef DEBUG` перед собственным `#define`, чтобы не было `-Wmacro-redefined`.
* Один TCP-клиент за раз. Вторая сессия примется только после разрыва первой — стандартное поведение индустриальных слейвов.

---

## 10. Тестирование на железе (минимум)

1. Прошить плату.
2. STAT_LED после старта моргает «1 импульс / 3 с» (`LED_STATE_NO_POLLING`).
3. Подключить мастер (modpoll / QModMaster / ModbusPoll) на `192.168.1.10:502`. STAT_LED переключается на «2 импульса / 1.5 с».
4. FC04 регистры 0…11 — подача 1 на DI меняет соответствующий бит (с задержкой = `di_filter_ms`).
5. FC06 HR 100 = 200, FC06 HR 117 = `0xA5A5`. После `TRIG_REBOOT` (HR 118 = `0xB00B`) значение должно сохраниться (FC03 HR 100 = 200).
6. Удержать FACT_RES + reset платы → STAT_LED моргнёт 10 раз, IP/настройки вернутся к дефолтам.

---

## 11. Что не реализовано / TODO

* MII-конфигурация KSZ8863MLLI — сейчас PHY работает в дефолтном режиме unmanaged switch. Добавление SPI/MII-driver файла настроек — задел на будущее.
* Diagnostic counters (число пакетов / ошибок / разрывов) пока не выведены в input registers.
* Watchdog-таймаут не выведен в настройки (фиксирован в CubeMX).
* `build/` каталог тянется в репозитории (унаследовано); рекомендую добавить в `.gitignore` отдельным коммитом.

---

## Лицензия

Прикладной код в `Application/` (кроме `third_party/`) — без явной лицензии, рассматривать как proprietary владельца репозитория.
nanoMODBUS — MIT (см. `Application/third_party/nanomodbus/LICENSE`).
LwIP — BSD-style (см. соответствующий каталог).
HAL/CMSIS — стандартные лицензии STMicroelectronics.
