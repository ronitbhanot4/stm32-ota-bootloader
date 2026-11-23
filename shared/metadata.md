# Boot metadata & trial-boot spec (v1 — DRAFT, needs Ronit's OK)

The exact flash layout and state machine shared by the bootloader (`stm32/bootloader`) and the application (`stm32/app`) for safe A/B updates, trial boot, and automatic rollback. **Change this file first, then both sides.**

All numbers are **little-endian**. All multi-byte fields are naturally aligned (no compiler padding).

## Flash layout (dual-sector ping-pong)

The 32 KB metadata region (`0x08008000`–`0x0800FFFF`) is split across two 16 KB flash sectors:

| Copy | Sector | Start address | Size |
|---|---|---|---|
| Copy 0 | Sector 2 | `0x08008000` | 16 KB |
| Copy 1 | Sector 3 | `0x0800C000` | 16 KB |

Each copy holds a single 64-byte `BootMeta` record at the start of its sector.

### Why two sectors
STM32F4 flash can only be erased a whole sector at a time. Writing the new record to the **other** sector means the last valid record is never erased before the new one is completely written:

1. **Read on boot (or before update):** check both Copy 0 and Copy 1. A copy is valid only if `magic`, `format_version`, `record_size`, `seq > 0`, and `crc32` all match.
   - Both valid → use the one with the higher `seq`.
   - One valid → use it (the other was either blank or interrupted mid-write).
   - Neither valid → first-boot fallback (see below).
2. **Write a new record:**
   - Increment `seq = current.seq + 1`.
   - Recompute `crc32` over bytes `0–59`.
   - Erase the **inactive** metadata sector (if current came from Copy 0, erase Copy 1; if Copy 1 or neither, erase Copy 0).
   - Write the 64 bytes as 16 × 32-bit words, then read back and verify.
   - If power dies mid-erase or mid-write, the old sector is untouched and still has a valid CRC.

> **Safety rule:** the metadata driver must refuse any erase/write address outside `0x08008000`–`0x0800FFFF` (Sectors 2–3 only).

## Record layout (`BootMeta`, 64 bytes)

| Offset | Size | Field | Value / Notes |
|---|---|---|---|
| 0 | 4 | `magic` | ASCII `META` (bytes `4D 45 54 41`, u32 = `0x4154454D`) |
| 4 | 2 | `format_version` | `1` |
| 6 | 2 | `record_size` | `64` |
| 8 | 4 | `seq` | Monotonically increasing counter, starts at `1` (`0` is invalid) |
| 12 | 1 | `active_slot` | `0` = Slot A (`0x08010000`), `1` = Slot B (`0x08040000`) |
| 13 | 1 | `flags` | `0` (reserved) |
| 14 | 2 | `reserved` | `0` |
| 16 | 16 | `slot[0]` | Slot A metadata (`SlotMeta`, see below) |
| 32 | 16 | `slot[1]` | Slot B metadata (`SlotMeta`, see below) |
| 48 | 12 | `reserved2` | All `0x00` (reserved for M5/M6) |
| 60 | 4 | `crc32` | Standard IEEE CRC-32 of bytes `0–59` (same CRC as `image_format.md`) |

### Per-slot struct (`SlotMeta`, 16 bytes)

| Offset (in `SlotMeta`) | Size | Field | Value / Notes |
|---|---|---|---|
| 0 | 1 | `state` | Slot state enum (see below) |
| 1 | 1 | `attempts_left` | Remaining trial-boot attempts (e.g. starts at `3`) |
| 2 | 2 | `reserved` | `0` |
| 4 | 4 | `fw_version` | From image header (`0` if empty or pre-flashed via debugger) |
| 8 | 4 | `payload_size` | From image header (`0` if empty or pre-flashed via debugger) |
| 12 | 4 | `payload_crc32` | From image header (`0` if empty or pre-flashed via debugger) |

### Slot states (`state`)

| Value | Name | Meaning |
|---|---|---|
| `0` | `SLOT_EMPTY` | Nothing valid stored |
| `1` | `SLOT_PENDING` | New image written & CRC-verified; ready for trial boot on next reset |
| `2` | `SLOT_TRIAL` | Bootloader started it under the watchdog; waiting for `boot_confirm()` |
| `3` | `SLOT_CONFIRMED` | App called `boot_confirm()` in time; known-good fallback |
| `4` | `SLOT_BAD` | Failed its trial attempts; never booted again |

### C struct reference

```c
#define BOOT_META_MAGIC           0x4154454DU  /* "META" */
#define BOOT_META_VERSION         1U
#define BOOT_META_RECORD_SIZE     64U
#define BOOT_META_MAX_ATTEMPTS    3U

typedef enum {
    SLOT_EMPTY     = 0,
    SLOT_PENDING   = 1,
    SLOT_TRIAL     = 2,
    SLOT_CONFIRMED = 3,
    SLOT_BAD       = 4
} SlotState;

typedef struct {
    uint8_t  state;
    uint8_t  attempts_left;
    uint16_t reserved;
    uint32_t fw_version;
    uint32_t payload_size;
    uint32_t payload_crc32;
} SlotMeta;

typedef struct {
    uint32_t magic;
    uint16_t format_version;
    uint16_t record_size;
    uint32_t seq;
    uint8_t  active_slot;
    uint8_t  flags;
    uint16_t reserved;
    SlotMeta slot[2];
    uint8_t  reserved2[12];
    uint32_t crc32;
} BootMeta;

_Static_assert(sizeof(SlotMeta) == 16, "SlotMeta must be 16 bytes");
_Static_assert(sizeof(BootMeta) == 64, "BootMeta must be 64 bytes");
```

## State transitions & boot flow

```
Update END OK ──► PENDING (attempts = 3)
                     │
                     ▼ (next boot: start IWDG, attempts--)
                   TRIAL ◄───────┐
                  ╱     ╲        │ (reset before confirm & attempts > 0:
   boot_confirm()╱       ╲       │  attempts--, retry with IWDG)
                ╱         ╲──────┘
               ▼           ▼ (reset before confirm & attempts == 0)
          CONFIRMED       BAD ──► rollback to other CONFIRMED slot
```

### 1. First boot (both metadata sectors invalid / blank flash)
When a board is freshly flashed via ST-LINK with Bootloader + App A, Sectors 2–3 are erased (`0xFF`).
- If Slot A passes the vector-table check (SP in RAM, reset address in Slot A), initialize metadata and write Copy 0 (`seq = 1`):
  - `active_slot = 0`
  - `slot[0].state = SLOT_CONFIRMED`
  - `slot[1].state = SLOT_EMPTY`
- Boot Slot A normally.
- If Slot A also has no valid vector table, print an error and stay in the bootloader (or enter update mode).

### 2. Normal boot (USER button not held)
Load the newest valid `BootMeta`, then decide in this order:

1. **A slot is `SLOT_PENDING`** (new update waiting):
   - Validate vector table (and `payload_crc32` if `payload_size > 0`). If invalid, mark it `SLOT_BAD`, save metadata, and fall through to step 3.
   - Set `state = SLOT_TRIAL`, `active_slot = <that slot>`, decrement `attempts_left` (3 → 2).
   - Save new metadata record.
   - Start **IWDG**, then jump to `<that slot>`.
2. **A slot is `SLOT_TRIAL`** (it booted, then reset before confirming):
   - If `attempts_left > 0`:
     - Decrement `attempts_left`, save new metadata record.
     - Start **IWDG**, then jump to `<that slot>` again.
   - If `attempts_left == 0`:
     - **Rollback:** mark the trial slot `SLOT_BAD`.
     - If the other slot is `SLOT_CONFIRMED`, set `active_slot` to the other slot, save new metadata record, and boot it (without starting the trial watchdog).
     - Otherwise save metadata, print error, and stay in bootloader.
3. **Otherwise (no `PENDING` or `TRIAL`):**
   - If `slot[active_slot].state == SLOT_CONFIRMED`, boot `active_slot`.
   - Else if the other slot is `SLOT_CONFIRMED`, switch `active_slot` to it, save metadata, and boot it.
   - Else print error and stay in bootloader.

### 3. Completing a UART update (`END` passes CRC-32)
- Keep the currently `CONFIRMED` slot untouched.
- Update the target slot's entry:
  - `state = SLOT_PENDING`
  - `attempts_left = BOOT_META_MAX_ATTEMPTS` (`3`)
  - `fw_version`, `payload_size`, `payload_crc32` copied from the validated 128-byte image header.
- Save the new metadata record **before** replying `ACK [END]`. (If metadata write fails, reply `NAK FLASH_ERROR`.)

### 4. App self-confirmation (`boot_confirm()`)
After initializing clocks, UART, and core peripherals, the app calls `boot_confirm()`:
1. Load the newest valid `BootMeta`.
2. If `slot[active_slot].state == SLOT_TRIAL`:
   - Set `slot[active_slot].state = SLOT_CONFIRMED` and `attempts_left = 0`.
   - Save the new metadata record to the opposite metadata sector (`seq + 1`).
3. If already `SLOT_CONFIRMED`, do nothing (avoids unnecessary flash erases on every normal boot).

## Watchdog (IWDG)

- **Peripheral:** STM32F411RE Independent Watchdog (`IWDG`), clocked by LSI (~32 kHz nominal; 17–47 kHz range).
- **Proposed timeout:** **~4 seconds** (prescaler `/64` → ~500 Hz, reload `2000` → ~4 s nominal, ~2.7–7.5 s across LSI tolerance).
  - Erasing a 16 KB metadata sector during `boot_confirm()` takes ~250–500 ms, so 4 seconds leaves plenty of headroom while keeping rollback demos snappy.
- **Important STM32 hardware rule:** once `IWDG` is started by the bootloader, **software cannot stop it** — only a reset stops it.
  - Because of this, every app must periodically pet the watchdog in its main loop (`IWDG->KR = 0xAAAAU;`). Writing `0xAAAA` to `IWDG->KR` is a harmless no-op when IWDG wasn't started, and keeps the app alive after a `TRIAL` → `CONFIRMED` transition.
  - A deliberately "bad" test app for rollback testing simply skips `boot_confirm()` and loops forever without petting `IWDG`.

## Open questions for Ronit

1. **`BOOT_META_MAX_ATTEMPTS`:** `3` attempts (~12 s total before rollback), or `2` for faster demos?
2. **After `END` writes `PENDING` and sends `ACK`:** should the bootloader auto-reboot (`NVIC_SystemReset()` after UART flush), or wait for a manual RESET press?
3. **Code sharing:** should `boot_meta.c/.h` live in a shared folder included by both `stm32/bootloader` and `stm32/app` (so `boot_confirm()` uses the exact same driver), or be duplicated in `stm32/app`?
4. **Slot direction in M4:** keep M4 single-direction first (always update Slot B from Slot A, rollback B → A), or generalize `flash_slot_b.c` to erase/write whichever slot is inactive (`1 - active_slot`) right away?
