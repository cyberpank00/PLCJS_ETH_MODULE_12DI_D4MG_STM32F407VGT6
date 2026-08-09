# AGENTS.md

Firmware for the PLCJS Ethernet 12DI module (12 discrete inputs, STM32F407VGT6,
KSZ8863 switch, Modbus TCP). This file is the orientation map for agents;
user-facing documentation (register tables, wiring, electrical limits) lives in
`README.md` / `README_EN.md`.

This repo is the **reference variant** of the family: most shared subsystems
(`discovery/`, `net_id/`, `ksz8863/`, `fw_header/`, `led/`, `button/`) were
developed here first and copy-pasted to the other module firmwares.

## Build (CMake)

Toolchain: STM32 Arm Clang (`starm-clang`) from STM32CubeCLT, generator Ninja.
Toolchain file: `cmake/starm-clang.cmake`. Presets in `CMakePresets.json`.

```
cmake --preset Debug
cmake --build --preset Debug
```

Release: substitute `Release`. Build output:
`build/<preset>/PLCJS_ETH_MODULE_12DI_D4MG_STM32F407VGT6.{elf,hex,bin,map}`.

Clean rebuild: delete `build/<preset>` and re-run the configure step.

There are no host-side unit tests. "Verified" means it compiles, and where the
change is observable it was exercised against hardware.

### Tools required
- CMake >= 3.22, Ninja
- `starm-clang` (STM32CubeCLT) on PATH. Alternative GCC toolchain at
  `cmake/gcc-arm-none-eabi.cmake`.

## Repository layout

| Path | Owner | Notes |
|---|---|---|
| `Application/` | hand-written | All real logic. Edit here. |
| `Core/`, `Drivers/`, `Middlewares/`, `LWIP/`, `cmake/stm32cubemx/` | STM32CubeMX | Regenerated from the `.ioc`. |
| `startup_stm32f407xx.s`, `STM32F407XX_FLASH.ld` | hand-edited | Diverged from CubeMX output — see *Linker*. |
| `DOC/` | assets | Schematic, product photos. |

**CubeMX regeneration hazard.** Re-running code generation from the `.ioc`
overwrites `Core/`, `Drivers/`, `Middlewares/`, `LWIP/` and
`cmake/stm32cubemx/CMakeLists.txt`. Several carry hand edits outside `USER CODE`
guards — notably `LWIP/Target/ethernetif.c` (link polling, `g_eth_any_link_up`)
and `LWIP/Target/lwipopts.h`. Diff carefully afterwards.

## Module map (`Application/`)

| Module | Responsibility |
|---|---|
| `app/` | Orchestrator: boot order, factory reset, network bring-up, housekeeping loop. Start here. |
| `di/` | 12-channel discrete input driver with software debounce/filter. |
| `temp/` | On-chip temperature sensor (ADC1_IN16), exposed as HR130, signed 0.1 °C. |
| `modbus/modbus_app.c` | Register-map adapter. **The map is documented in the header comment of `modbus_app.h`.** |
| `modbus/modbus_tcp_server.c` | Single-client TCP server on LwIP netconn; newest connection wins. |
| `settings/` | Flash-backed settings, CRC32-protected, magic + version. |
| `discovery/` | PDP responder, UDP/20556 broadcast. Address a device by MAC without an IP. |
| `net_id/` | Derives MAC and link-local IPv4 from the 96-bit MCU UID. |
| `ksz8863/` | SMI/MIIM driver for the Ethernet switch; reset policy and recovery. |
| `led/` | STAT_LED state machine, 10 ms tick. |
| `button/` | FACT_RES button, boot-time hold detection. |
| `fw_header/` | Firmware image header consumed by the bootloader. Module identity. |
| `third_party/nanomodbus/` | Vendored protocol library, locally patched. |

Inputs are exposed twice: as discrete inputs (FC02, 0..11) and as input
registers (FC04, 0..11). Both report the *filtered* state. Filter time is
HR100, 10..1000 ms, default 50.

## Invariants

### Single sources of truth
- **Module identity** — `Application/fw_header/fw_header.h`:
  `FW_PRODUCT_ID = 0x504C1201`, `FW_HW_REVISION = 0x0101`,
  `FW_VERSION_VALUE = 0x0107`. `CMakeLists.txt` passes no identity defines.
- **Firmware version over Modbus** — IR120/IR121 derive from `FW_VERSION_VALUE`.
  Never hardcode a version in `modbus_app.c`.
- **Register map** — the header comment of `modbus_app.h`, mirrored by the
  `MB_HR_*` / `MB_IR_*` constants. Keep comment and constants in step.
- **Module ID** — `MODULE_ID_12DI = 0x12D1`, reported in IR125.

### Version policy — bump the minor on every change

**Mandatory.** Every change to firmware behaviour ships with `FW_VERSION_VALUE`
in `fw_header.h` incremented by one minor (`0x0107` → `0x0108`). The version is
the operator's only way to tell which build is running on a device in the field,
so an un-bumped change is a defect.

- Minor bump: any firmware-only change — fixes, features, register-map
  additions, timing changes.
- Major bump: only together with a `FW_HW_REVISION` major change (MCU pinout).
  OTA requires `fw_version` major == `hw_revision` major.
- Pure documentation-only commits do not need a bump.

Bump checklist — all three places, they drift easily:
1. `FW_VERSION_VALUE` in `fw_header.h`.
2. Version rows in `README.md` (image `fw_version` row and the IR120/IR121 row).
3. The same rows in `README_EN.md`.

A chronological version-review / changelog file is planned; once it exists, add
an entry there in the same commit as the bump.

### Persistence
- `settings_t` layout is frozen. Reordering or resizing fields requires bumping
  `SETTINGS_VERSION`; a mismatch makes deployed units silently fall back to
  factory defaults (link-local), which looks like a field failure.
- The field is still named `use_dhcp` but holds a tri-state net mode
  (`NET_MODE_STATIC/DHCP/LINKLOCAL`). Kept for on-flash compatibility — do not
  "clean this up".
- Settings live in **sector 10 @ `0x080C0000`**. Sector 11 is bootloader
  staging — never write settings there.

### Threading
- All KSZ8863 SMI access must stay on the link-polling thread.
  `ksz8863_request_recovery()` only sets a flag; `ksz8863_service()` performs the
  reset. Never call `ksz8863_hw_reset()` from a Modbus or housekeeping context.
- LwIP calls must run in the tcpip thread; the live network re-apply goes through
  `tcpip_callback()`.
- Flash writes and resets requested over Modbus/discovery are deferred to the
  housekeeping loop in `app_run()` via the `*_take_pending_*()` flags, so they
  never run inside the tcpip thread.
- Any loop blocking longer than the IWDG period must call
  `HAL_IWDG_Refresh(&hiwdg)`.

### Boot order (`app_run()`)
Two ordering constraints, both load-bearing and both the subject of past bug
fixes:
- The LED task starts **before** the FACT_RES button check, otherwise the
  factory-reset blink is silently dropped (commit `fix(led): start LED task
  before button hold check`).
- Factory reset writes Flash **before** the visual confirmation: the sector
  erase blocks the CPU for ~1–2 s and would freeze the blink.

## Gotchas

- **Never reset the KSZ8863 on a warm reboot.** The switch forwards traffic
  between ports 1 and 2 autonomously, so it must survive an MCU reset — a reset
  drops both external links for seconds of auto-negotiation, and this module may
  be mid-chain. Only cold boot resets it defensively, plus explicit recovery
  (HR118 = `0x8863`, or ~5 s of dead SMI).
- `ksz8863_hw_reset()` runs before `HAL_ETH_Init()`; every other KSZ8863 API
  needs SMI up, i.e. after `MX_LWIP_Init()`.
- **Device name is 15 chars + NUL in a fixed 16-byte field**, and the PDP
  IDENTIFY response is a fixed 38 bytes. Must stay identical across every module
  variant and ModbusTool.
- Modbus TCP is single-client, newest-wins: a new connection drops the old one,
  so a hung client cannot lock the device out. A silent client is dropped after
  `MB_IDLE_DROP_MS` (30 s).
- `LED_STATE_FACTORY_RESET` is sticky until reboot and overrides all other
  states and modes.
- HR118 multiplexes distinct magics: `0xB00B` reboot, `0xB007` bootloader,
  `0x8863` switch reset. HR117 = `0xA5A5` save, HR119 = `0xDEAD` factory reset.
- Saving settings re-applies network config live — IP/DHCP changes take effect
  without a reboot.
- HR130 (on-chip temperature) is read-only despite living in the holding-register
  space.

## Linker / memory contract with the bootloader

`STM32F407XX_FLASH.ld` is **not** a stock CubeMX script:

- `FLASH` origin is `0x08040000`, length 256 K — the application slot. The image
  only runs via the bootloader.
- `RAM` length is `0x1FFF0`, not 128 K. The top 16 bytes hold the no-init
  boot-request cell at `0x2001FFF0` (`BOOT_REQUEST_MAGIC = 0xB007CAFE`).
- `.fw_header` is padded to offset `0x200` from the start of FLASH.

`fw_header_t` must stay byte-identical to `fw_header_t` in the bootloader's
`Application/validate/app_validate.h` (28 bytes, packed). CRC32 and image size
are *not* in the header — they travel in OTA metadata.

OTA acceptance: `product_id` exact match **and** `hw_revision` major byte match.

## Multi-repo workspace

Sibling repos under `E:\STM_Programming\`:

| Repo | Role |
|---|---|
| `PLCJS_ETH_MODULE_12DI_D4MG_...` | This module — `0x504C1201` / IR125 `0x12D1`. |
| `PLCJS_ETH_MODULE_12DQ_D4MG_...` | 12 discrete outputs, `0x504C1202` / `0x12D0`. Closest sibling. Has `Tools/*.mjs` stress tests that pair a 12DO with **this** module as the readback path. |
| `PLCJS_ETH_MODULE_4RTD_D4MG_...` | 4x RTD, `0x504C0403` / `0x04D1`. |
| `BOOTLOADER_PLCJS_ETH_MODULE_STM32F407VGT6` | Shared bootloader (serves every variant). Owns `flash_map.h`, `app_validate.h`, `scripts/variants.csv`. |
| `PLCJS_Module_ModbusTool` | Qt6/C++17 desktop client. Register maps in `src/maps/ModuleMaps.cpp`, PDP in `src/protocol/Pdp.cpp`. |

**`Application/` subsystems are copy-pasted between firmware variants, not
shared via a submodule.** A fix to a shared subsystem here is **not** fixed
elsewhere — say so explicitly rather than implying a repo-wide fix. Because this
repo is where shared code usually originates, changes here are the ones most
likely to need porting outward.

Cross-repo contracts that must change in lockstep:
- **Wire format** (PDP frame layout, 38-byte IDENTIFY, 16-byte name) — every
  firmware + `Pdp.cpp`.
- **`fw_header_t` layout, `FW_HEADER_OFFSET`, `BOOT_REQUEST_FLAG_ADDR`/`MAGIC`,
  flash map** — every firmware + bootloader + both linker scripts.
- **product_id** — `fw_header.h` here and `scripts/variants.csv` in the
  bootloader. (`variants.csv` uses the 3-byte hw encoding `0x010101`, firmware
  headers use the 2-byte `0x0101`; the bootloader compares major only.)
- **Register map changes** — `modbus_app.h` here and `build12DI()` in
  `ModuleMaps.cpp`, or the tool shows stale registers.

## Maintaining this file

`AGENTS.md` is a living document, not a one-time write. Update it **in the same
commit** as the change it describes — a stale map is worse than no map, because
it actively misleads. Touch it when:

- an invariant, gotcha or threading rule is added or changes;
- a module is added, removed or repurposed (`Application/` map);
- the build procedure, toolchain or linker contract changes;
- a register-map change alters the header comment of `modbus_app.h`;
- `FW_VERSION_VALUE` is bumped and the version-policy text needs the new
  example value;
- a cross-repo contract changes (PDP wire format, `fw_header_t`, flash map,
  `product_id`) — update the *Multi-repo* section here **and** the corresponding
  section in the sibling repo(s).

Pure refactors with no behavioural change do not require an update, but when in
doubt, update — the cost is a few lines of text, the cost of a stale invariant
is a field bug.
