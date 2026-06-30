# PLCJS_ETH_MODULE_12DI_D4MG_STM32F407VGT6

Основная прошивка Ethernet-модуля PLCJS с 12 дискретными входами на базе `STM32F407VGT6`.

Прошивка читает 12 active-low входов, фильтрует дребезг, публикует состояния по `Modbus TCP`, хранит настройки во внутренней Flash, управляет индикатором `STAT_LED`, поддерживает factory reset и программный переход в Ethernet bootloader для OTA-обновления.

## Текущий статус

Прошивка интегрирована с отдельным bootloader-проектом и протестирована на реальном модуле.

Проверено:

- сборка `CMake + Ninja`
- запуск приложения из bootloader
- `ping` и `Modbus TCP` на рабочем приложении
- сохранение настроек во Flash и применение после reboot
- переключение `DHCP <-> static IP`
- переключение `TCP_PORT 502 <-> 1502`
- команды `reboot`, `factory reset`, `enter bootloader`
- все входы `DI1..DI12`
- LED-паттерны `idle`, `polling`, `no link`, `factory reset`
- негативные Modbus-сценарии
- длительный polling / reconnect / ping soak

## Аппаратная платформа

| Узел | Описание |
|---|---|
| MCU | `STM32F407VGT6`, Cortex-M4F |
| Ethernet | `KSZ8863` Ethernet switch/PHY, RMII |
| Дискретные входы | 12 входов, active-low, внутренние pull-up MCU |
| Температура | внутренний датчик MCU (`ADC1_IN16`), доступен по HR130 |
| Factory reset | кнопка `FACT_RES`, active-low |
| Индикация | `STAT_LED`, active-high |
| Watchdog | `IWDG`, обновляется из основного цикла приложения |

## Flash и RAM layout

Приложение собрано как image для работы вместе с bootloader.

| Область | Адрес | Размер | Назначение |
|---|---:|---:|---|
| Bootloader | `0x08000000` | 128 KB | sectors 0-4, другой проект |
| Metadata | `0x08020000` | 128 KB | sector 5, состояние OTA |
| Application | `0x08040000` | 256 KB | sectors 6-7, эта прошивка |
| Staging | `0x08080000` | 256 KB | sectors 8-9, OTA staging |
| Settings | `0x080C0000` | 128 KB | sector 10, настройки приложения |

RAM:

- основной RAM начинается с `0x20000000`
- верхние 16 байт зарезервированы под shared boot-request flag
- shared flag address: `0x2001FFF0`
- shared flag magic: `0xB007CAFE`

## Сборка

Требуется STM32CubeCLT с `starm-clang`, `cmake` и `ninja`.

Стандартная сборка для варианта 12-DI/D4MG (`PRODUCT_ID=0x12D1D4A0`, `HW_REVISION=1`):

```powershell
cmake -S . -B build/Debug -G Ninja `
  -DCMAKE_BUILD_TYPE=Debug `
  -DCMAKE_TOOLCHAIN_FILE=cmake/starm-clang.cmake
cmake --build build/Debug
```

Основные результаты сборки:

- `build/Debug/PLCJS_ETH_MODULE_12DI_D4MG_STM32F407VGT6.elf`
- `build/Debug/PLCJS_ETH_MODULE_12DI_D4MG_STM32F407VGT6.hex`
- `build/Debug/PLCJS_ETH_MODULE_12DI_D4MG_STM32F407VGT6.bin` — OTA image для bootloader

Post-build шаг генерирует `.hex` и `.bin` из ELF через `objcopy`. Дополнительных скриптов не требуется.

`.bin` содержит встроенный `fw_header_t` по смещению `0x200` (magic `"PLCJ"`) с `product_id`,
`hw_revision` и `fw_version`. Бутлоадер проверяет эти поля при OTA-обновлении.

## Многовариантная сборка

Прошивка поддерживает сборку для разных вариантов модуля через CMake-параметры.
При каждой сборке в бинарник вшиваются корректные `product_id` и `hw_revision`,
а бутлоадер, скомпилированный для того же варианта, примет **только** этот образ.

Доступные параметры:

| Параметр CMake | Значение по умолчанию | Описание |
|---|---|---|
| `-DPRODUCT_ID=0x...` | `0x12D1D4A0` | Идентификатор варианта модуля |
| `-DHW_REVISION=N` | `1` | Ревизия платы |
| `-DFW_VERSION=0xMMmmpp` | `0x00010000` | Версия прошивки (major/minor/patch) |

Примеры для разных вариантов:

```powershell
# 12 дискретных входов / 4 выхода — вариант по умолчанию
cmake -S . -B build/12di -G Ninja `
  -DCMAKE_TOOLCHAIN_FILE=cmake/starm-clang.cmake `
  -DPRODUCT_ID=0x12D1D4A0 -DHW_REVISION=1 -DFW_VERSION=0x00010000

# 12 дискретных выходов
cmake -S . -B build/12do -G Ninja `
  -DCMAKE_TOOLCHAIN_FILE=cmake/starm-clang.cmake `
  -DPRODUCT_ID=0x12D00000 -DHW_REVISION=1

# 4 входа RTD
cmake -S . -B build/4rtd -G Ninja `
  -DCMAKE_TOOLCHAIN_FILE=cmake/starm-clang.cmake `
  -DPRODUCT_ID=0x04D00000 -DHW_REVISION=1

# 8 аналоговых входов
cmake -S . -B build/8ai -G Ninja `
  -DCMAKE_TOOLCHAIN_FILE=cmake/starm-clang.cmake `
  -DPRODUCT_ID=0x08A10000 -DHW_REVISION=1

# 8 аналоговых выходов
cmake -S . -B build/8ao -G Ninja `
  -DCMAKE_TOOLCHAIN_FILE=cmake/starm-clang.cmake `
  -DPRODUCT_ID=0x08A00000 -DHW_REVISION=1
```

После сборки `app.bin` в папке варианта несёт правильный `product_id`, и бутлоадер
для **другого** варианта отклонит его при `FINALIZE_UPDATE`.

### Как это работает

```
Сборка → fw_header.c компилируется с -DFW_PRODUCT_ID=0x12D1D4A0
                                       -DFW_HW_REVISION=1
                                       -DFW_VERSION_VALUE=0x00010000
       → линкер помещает g_fw_header в секцию .fw_header по смещению 0x200
       → objcopy генерирует .bin — он же является OTA image

OTA-обновление → бутлоадер читает product_id/hw_revision из бинарника
               → при несоответствии: PRODUCT_MISMATCH, BOOT_ERROR
               → при совпадении: образ устанавливается
```

## Прошивка

Через ST-Link:

```powershell
STM32_Programmer_CLI -c port=SWD -w build/Debug/PLCJS_ETH_MODULE_12DI_D4MG_STM32F407VGT6.elf -v -rst
```

Через bootloader OTA используется `.bin` из `build/Debug` и клиент `tools/fw_update.mjs` из bootloader-репозитория (см. раздел ниже).

## OTA обновление (fw_update.mjs)

Клиент `tools/fw_update.mjs` находится в репозитории **bootloader** рядом с этим проектом.  
Он говорит с bootloader по Modbus TCP и может управлять как bootloader, так и запущенным приложением.

### Требования

- **Node.js 18+** (ES-модули, top-level await)
- внешних npm-пакетов **нет** — встроенный `node:net`

### IP-адреса по умолчанию

| Роль | Адрес | Опция |
|---|---|---|
| Bootloader | `192.168.142.99` | `--boot-ip` / `--ip` |
| Приложение | `192.168.142.98` | `--app-ip` |
| Modbus TCP port | `502` | `--port` |

### Команды

| Команда | Цель | Описание |
|---|---|---|
| `status` | bootloader | Прочитать и вывести все статусные регистры bootloader |
| `update <file.bin>` | bootloader | Загрузить новую прошивку (4 шага: BEGIN → WRITE → FINALIZE → INSTALL) |
| `abort` | bootloader | Прервать текущую OTA-сессию |
| `reboot` | bootloader | Software reset (bootloader немедленно уходит в reset) |
| `app-bootloader` | приложение | Попросить приложение перейти в bootloader (ждёт готовности и выводит `status`) |
| `app-reboot` | приложение | Перезагрузить приложение |
| `app-factory-reset` | приложение | Сбросить настройки приложения на дефолты |

### Типичный сценарий OTA

```powershell
# 1. Убедиться, что приложение запущено и отвечает
node tools/fw_update.mjs app-reboot --app-ip 192.168.142.98

# 2. Перевести приложение в режим bootloader
#    (скрипт ждёт до 20 с и автоматически выводит status)
node tools/fw_update.mjs app-bootloader --app-ip 192.168.142.98 --boot-ip 192.168.142.99

# 3. Загрузить новую прошивку
node tools/fw_update.mjs update build/Debug/PLCJS_ETH_MODULE_12DI_D4MG_STM32F407VGT6.bin `
  --boot-ip 192.168.142.99

# После INSTALL bootloader автоматически перезагружается в приложение.
```

### Опции

| Опция | Описание | Дефолт |
|---|---|---|
| `--boot-ip <addr>` / `--ip <addr>` | IP bootloader | `192.168.142.99` |
| `--app-ip <addr>` | IP приложения | `192.168.142.98` |
| `--port <n>` | Modbus TCP порт | `502` |
| `--version <hex>` | Версия прошивки в формате `0xMMmmpp` (передаётся в BEGIN_UPDATE) | `0x00010000` |

Пример с явными параметрами:

```powershell
node tools/fw_update.mjs update build/Debug/PLCJS_ETH_MODULE_12DI_D4MG_STM32F407VGT6.bin `
  --boot-ip 192.168.10.50 --port 502 --version 0x00010100
```

### Статус bootloader

Команда `status` выводит:

```
Magic:          0xB00710AD  OK
BL version:     1.0.0
Boot state:     BOOT_WAIT_COMMAND (2)
App valid:      1
App version:    1.1.0
Product ID:     0x12D1D4A0
HW revision:    1
Last error:     NONE (0)
Cmd status:     IDLE (0)
Staging valid:  0
```

Состояние `BOOT_WAIT_COMMAND (2)` означает, что bootloader готов принять команду `update`.

### Ошибки и восстановление

| Ошибка | Причина | Действие |
|---|---|---|
| `PRODUCT_MISMATCH` | `product_id` в `.bin` не совпадает с bootloader | Убедиться, что собран правильный вариант (`-DPRODUCT_ID=...`) |
| `HW_REV_MISMATCH` | `hw_revision` не совпадает | Проверить `-DHW_REVISION=...` при сборке |
| `BLOCK_CRC` / `IMAGE_CRC` | Повреждение данных при передаче | Повторить `update`; если не помогает — `abort` |
| `FLASH_ERASE` / `FLASH_WRITE` | Ошибка записи Flash | `abort`, затем `reboot`; при повторении — ST-Link |
| `UPDATE_TIMEOUT` | Bootloader не получил все блоки за отведённое время | `abort`, повторить с более стабильной сетью |

Если bootloader завис в состоянии `BOOT_ERROR` или `BOOT_RECEIVE_FW`:

```powershell
# Прервать сессию и перезагрузить bootloader
node tools/fw_update.mjs abort --boot-ip 192.168.142.99
node tools/fw_update.mjs reboot --boot-ip 192.168.142.99
```

## Сеть

Настройки сети хранятся во Flash и доступны через Modbus holding registers.

Дефолты:

| Параметр | Значение |
|---|---|
| DHCP | `1` / включен |
| Static IP | `192.168.142.147` |
| Netmask | `255.255.255.0` |
| Gateway | `192.168.142.1` |
| Modbus TCP port | `502` |
| Modbus unit id | `1` |

В тестовой сети DHCP выдавал приложению адрес `192.168.142.98`. Bootloader в протестированной конфигурации доступен на `192.168.142.99`.

Важно:

- `USE_DHCP = 1` означает, что static IP хранится как запасная настройка, но не применяется
- изменения IP/port/DHCP вступают в силу после `TRIG_SAVE` и `TRIG_REBOOT`
- static IP mode был проверен на `192.168.142.147`

## Дискретные входы

Входы active-low: замыкание входа на землю публикуется как логическая `1`.

| Modbus index | Silkscreen | MCU pin | Mask bit |
|---:|---|---|---:|
| 0 | DI1 | PB3 | `0x001` |
| 1 | DI2 | PD7 | `0x002` |
| 2 | DI3 | PD6 | `0x004` |
| 3 | DI4 | PD5 | `0x008` |
| 4 | DI5 | PD4 | `0x010` |
| 5 | DI6 | PD3 | `0x020` |
| 6 | DI7 | PD2 | `0x040` |
| 7 | DI8 | PD1 | `0x080` |
| 8 | DI9 | PD0 | `0x100` |
| 9 | DI10 | PC12 | `0x200` |
| 10 | DI11 | PC11 | `0x400` |
| 11 | DI12 | PC10 | `0x800` |

Фильтр входов:

- период опроса: `1 ms`
- дефолт: `50 ms`
- диапазон: `10..1000 ms`
- настройка применяется сразу после записи `HR100`
- сохранение в Flash требует `TRIG_SAVE`

## STAT_LED

Режимы LED:

| Код | Режим |
|---:|---|
| `0` | всегда выключен |
| `1` | всегда включен |
| `2` | state machine, дефолт |

Паттерны в режиме state machine:

| Состояние | Поведение |
|---|---|
| No polling | 1 короткая вспышка каждые 3 секунды |
| Polling | 2 короткие вспышки каждые 1.5 секунды |
| No link | 3 короткие вспышки каждые 3 секунды |
| Factory reset | непрерывное мигание, дефолт `100 ms ON / 100 ms OFF` |

Паттерны `idle -> polling -> idle` и `no link` были подтверждены визуально на модуле.

## Modbus TCP карта

### Discrete Inputs, FC02

| Address | Описание |
|---:|---|
| `0..11` | DI1..DI12, отфильтрованное состояние |

### Input Registers, FC04

| Address | Описание |
|---:|---|
| `0..11` | DI1..DI12, значения `0/1` |
| `120` | firmware version major |
| `121` | firmware version minor |
| `122` | uptime seconds, low word |
| `123` | uptime seconds, high word |
| `124` | 12-bit DI mask |
| `125` | module ID (идентификатор варианта модуля, read-only; `0x12D1` для 12DI/D4MG) |

### Holding Registers, FC03 / FC06 / FC16

| Address | Имя | Диапазон / значение | Применение |
|---:|---|---|---|
| `100` | `DI_FILTER_MS` | `10..1000`, default `50` | сразу |
| `101` | `LED_MODE` | `0..2`, default `2` | сразу |
| `102` | `SLAVE_ID` | `1..247`, default `1` | для новых Modbus-сессий |
| `103` | `TCP_PORT` | `1..65535`, default `502` | после save + reboot |
| `104..107` | `IP_BASE` | IPv4 octets | после save + reboot |
| `108..111` | `NETMASK_BASE` | IPv4 octets | после save + reboot |
| `112..115` | `GATEWAY_BASE` | IPv4 octets | после save + reboot |
| `116` | `USE_DHCP` | `0/1`, default `1` | после save + reboot |
| `117` | `TRIG_SAVE` | write `0xA5A5` | сохранить настройки |
| `118` | `TRIG_REBOOT` | write `0xB00B` | soft reset |
| `118` | `TRIG_BOOTLOADER` | write `0xB007` | перейти в bootloader |
| `119` | `TRIG_FACTORY_RESET` | write `0xDEAD` | defaults + save + reset |
| `130` | `TEMPERATURE` | signed, 0.1 degC (read-only) | — |

Некорректные значения возвращают Modbus exception `ILLEGAL_DATA_VALUE`. Некорректные адреса возвращают `ILLEGAL_DATA_ADDRESS`.

## Примеры PyModbus

Чтение версии и DI mask:

```python
from pymodbus.client import ModbusTcpClient

client = ModbusTcpClient("192.168.142.98", port=502, timeout=5)
client.connect()
rr = client.read_input_registers(address=120, count=5, device_id=1)
print(rr.registers)
client.close()
```

Сохранить настройки:

```python
client.write_register(address=117, value=0xA5A5, device_id=1)
```

Перезагрузить приложение:

```python
client.write_register(address=118, value=0xB00B, device_id=1)
```

Перейти в bootloader:

```python
client.write_register(address=118, value=0xB007, device_id=1)
```

Factory reset:

```python
client.write_register(address=119, value=0xDEAD, device_id=1)
```

Чтение температуры MCU (HR130, signed 0.1 degC):

```python
rr = client.read_holding_registers(address=130, count=1, device_id=1)
raw = rr.registers[0]
# Интерпретировать как знаковое 16-bit
temp_dc = raw if raw < 0x8000 else raw - 0x10000
print(f"Температура: {temp_dc / 10:.1f} °C")
```

## Настройки во Flash

Настройки хранятся в sector 10 по адресу `0x080C0000`.

Структура защищена:

- magic: `0x12D14A57`
- version: `1`
- CRC32 по всем полям до `crc32`

Если структура во Flash невалидна, приложение загружает дефолты. `TRIG_SAVE` стирает sector 10 и записывает актуальную структуру настроек.

## Переход в bootloader

Приложение и bootloader используют shared no-init RAM flag:

- address: `0x2001FFF0`
- magic: `0xB007CAFE`

При записи `0xB007` в `HR118` приложение:

1. записывает magic в shared RAM cell
2. ждет короткую паузу, обновляя IWDG
3. выполняет `NVIC_SystemReset()`
4. bootloader считывает magic, очищает его и остается в `BOOT_WAIT_COMMAND`

## Recovery и эксплуатационные заметки

- после `factory reset` настройки возвращаются к дефолтам и устройство перезагружается
- после смены IP/port/DHCP обязательно делайте `TRIG_SAVE` и `TRIG_REBOOT`
- при переходе в bootloader приложение уходит с app IP, bootloader поднимается на bootloader IP
- если bootloader остался после неудачной OTA-сессии, используйте `ABORT_UPDATE`, затем `REBOOT` на стороне bootloader

## Проверенный тестовый набор

| Тест | Результат |
|---|---|
| Build app | OK |
| Ping `.98` | OK |
| Modbus read/write | OK |
| Reconnect 20/20 | OK |
| Polling 100/100 | OK |
| Soak ping 120/120 | OK, 0% loss |
| Soak Modbus 240/240 | OK |
| `DI1..DI12` | OK |
| `TCP_PORT 502 <-> 1502` | OK |
| `DHCP <-> static .147` | OK |
| `reboot` | OK |
| `factory reset` | OK |
| `app -> bootloader -> app` | OK |
| Negative Modbus values | OK |
| LED idle/polling/no-link | OK |

## Известные ограничения

- нет аутентификации Modbus TCP команд
- Modbus server обслуживает одного клиента за раз; дополнительные клиенты ждут освобождения соединения
- текущий MAC address является locally-administered и должен быть заменен для серийного производства
- bootloader IP настраивается в отдельном проекте и сейчас статический
