# OTA Bootloader — Project Tracker & Engineering Notebook

**Secure STM32 firmware updates** · **Team:** Ismail Ahmed & Ronit Bhanot · **Last updated:** 2026-11-22

> Living team doc. Tracks milestones, progress commits, bugs, failure-test evidence, known debt, and core concepts worth bringing to an interview.

---

## Project at a glance

An STM32 that can receive new firmware, reject damaged or fake images, trial-boot the new version under a watchdog, and automatically roll back to the last confirmed working version if the trial fails. Built following six milestones: wired updates first, then rollback, then signatures, then Wi-Fi.

| Piece | Choice | Why |
|---|---|---|
| MCU board | NUCLEO-F411RE | 512 KB flash, built-in ST-LINK programmer + USB serial, same board as the IMU Logger |
| Wi-Fi radio | Seeed XIAO ESP32-C3 | Small, cheap, already owned; only added after rollback works |
| Board-to-radio link | USART1 (PA9 TX / PA10 RX) at 115200 baud | Simple 2-wire serial; ST-LINK serial (USART2) stays free for logs |
| Integrity check | CRC-32 (IEEE 802.3 / `zlib.crc32`) | Catches accidental corruption (flipped bit, dropped byte) |
| Authenticity check | Digital signature (Ed25519, tested library) | Proves the image came from us; never write custom crypto |
| Safety net | Watchdog + automatic rollback | A new app that hangs gets reset and replaced by the last good slot |
| Toolchain | STM32CubeIDE (HAL), Arduino IDE (ESP32), Python 3 (host tools), Go (update server) | Free, fast bring-up with HAL, Go stdlib HTTP server for M6 |
| Version control | GitHub, one clear commit per milestone | Rule: don't start the next milestone until the current one is verified and committed |

**Ownership:**
- **Ronit** leads the device side (bootloader, flash driver, UART receiver, metadata/watchdog).
- **Ismail** leads the host & server side (Python packager and UART sender, Go update server, ESP32 download protocol).
- **Both** build, review, and test every milestone together so either teammate can explain the entire system end-to-end.

### Why a bootloader at all
Without a bootloader, the only way to change firmware is plugging in a debugger. A bootloader is a small program that always runs first after reset and decides which app to start — letting a device in the field update itself and recover automatically when an update goes wrong.

### Why wired first, Wi-Fi last
Wi-Fi adds a second chip, a network, and a server — three extra places for bugs to hide. Proving updates and rollback over a plain USB serial cable first means that when Wi-Fi breaks in M6, we already know the bootloader isn't the problem.

---

## Flash map

Flash is split into non-overlapping regions. The STM32F4 can only erase a whole sector at a time, so every region starts on a sector boundary — erasing Slot B can never touch the bootloader or Slot A.

| Region | Start address | Size | Flash sectors | Holds |
|---|---|---|---|---|
| Bootloader | `0x08000000` | 32 KB | 0–1 (16 KB × 2) | Runs first after reset; never updated |
| Metadata | `0x08008000` | 32 KB | 2–3 (16 KB × 2) | Active slot, pending/trial state, attempts, version, CRC (dual power-safe records) |
| Slot A | `0x08010000` | 192 KB | 4–5 (64 KB + 128 KB) | Current or fallback app |
| Slot B | `0x08040000` | 256 KB | 6–7 (128 KB × 2) | New or fallback app |

**Hard limit:** every app binary must be at most **196,608 bytes (192 KB)** — the size of the smaller slot — so any image can fit in either Slot A or Slot B. Enforced in both the app linker scripts (`LENGTH = 192K`) and [`tools/package.py`](../tools/package.py).

---

## Milestones and progress

**Rule:** don't start the next milestone until the current checkpoint works repeatedly on hardware and is committed.

| # | Milestone | Done when (checkpoint) | Target week | Status |
|---|---|---|---|---|
| 1 | Set up and prove the board | LED blinks and `"APP A"` prints once a second; both CubeIDE projects exist | Wk 1 (15–21 Oct) | **Done** |
| 2 | Place and jump to an app | Bootloader prints, then jumps to Slot A; breakpoints work in both projects | Wk 2 (22–28 Oct) | **Done** |
| 3 | Receive and program a second image | App B arrives over UART, lands in Slot B, read-back CRC matches (not booted yet) | Wk 3 (29 Oct–4 Nov) | **Done** |
| 4 | Metadata + safe trial boot | Good image becomes active; a broken one rolls back on its own | Wk 4–5 (5–18 Nov) | **Not started** |
| 5 | Authenticity (signatures) | Signed image installs; a 1-byte change or wrong key is rejected | Wk 6 (19–25 Nov) | **Not started** |
| 6 | Wi-Fi + server | Signed image downloads over Wi-Fi, trial-boots, confirms, reports status | Wk 7–8 (26 Nov–9 Dec) | **Not started** |
| — | Failure tests + cleanup | Evidence recorded for every required test below | Wk 9 (10–16 Dec) | **Not started** |
| — | Final demo + README | Someone else can reproduce the demo from the README | Wk 10 (17–23 Dec) | **Not started** |

> **Schedule risk:** Weeks 8–10 land on December exams. As of **22 Nov (Week 6)**, M1–M3 are done and M4 is ~1 week behind schedule. If the gap grows, push Wi-Fi (M6) to January — **M1–M5 alone is already a complete, defensible project**.

---

## Status and next steps

**Current snapshot (22 Nov 2026):** M1–M3 are complete. Ronit's receiver (`f276934`, 14 Nov) closed M3: App B arrives over UART, writes to Slot B, and the read-back CRC-32 matches the header. Next up is verifying M3 on Ismail's board, retiring `stm32/board_test`, and starting M4 together.

### Checklist

- [x] **[Both]** Finish Milestone 1: print `"APP A"` once a second over USART2 (ST-LINK serial), not just the ESP32 link
- [x] **[Both]** Replace `board_test` with two real CubeIDE projects: `stm32/bootloader` and `stm32/app`
- [x] **[Both]** Confirm the board's flash is 512 KB and the sector map matches RM0383
- [x] **[Ismail]** Write [`shared/image_format.md`](../shared/image_format.md) before either side codes against it
- [x] **[Both]** Commit M1, then implement Milestone 2 (linker scripts + `jump_to_app()`)
- [x] **[Ismail]** M2 breakpoint check: breakpoints stop in both bootloader and app
- [x] **[Ronit]** Review `jump_to_app()` and [`shared/uart_protocol.md`](../shared/uart_protocol.md)
- [x] **[Ronit]** Software CRC-32 in C (`0xCBF43926` for `"123456789"`), Slot B flash driver, UART receiver, blue-button update mode
- [x] **[Both]** Real sender to real receiver over COM3, Slot B read-back CRC matches (M3 checkpoint)
- [ ] **[Both]** Delete `stm32/board_test` (agree before removing) and remove `(DRAFT)` status from [`shared/uart_protocol.md`](../shared/uart_protocol.md)
- [ ] **[Ismail]** Pull `f276934` and repeat the M3 hardware test on Ismail's board with [`tools/send_update.py`](../tools/send_update.py) (proves reproducibility on both setups)
- [ ] **[Both]** Start M4 together: design metadata struct & dual-sector layout in `shared/`, then implement trial boot + IWDG watchdog rollback

---

## Progress log

Newest first. One entry per meaningful step forward, linked to its commit.

| Date | What happened | Commit | Who |
|---|---|---|---|
| 2026-11-14 | **M3 done (device side):** bootloader receives App B over UART and writes it to Slot B; `END` re-reads Slot B and verifies CRC-32 against the header. Holding the blue USER button at reset enters update mode. Added `crc32.c`, `flash_slot_b.c` (erases sectors 6–7, writes 4-byte words, refuses addresses outside Slot B), `update_frame.c`, `update_image.c`, `update_session.c` (re-ACKs resent chunks without double-writing), `update_uart.c`. Matches `shared/uart_protocol.md` reason codes 1–8. | `f276934` | Ronit |
| 2026-10-24 | **Architecture decision:** M6 update server will be written in Go instead of Python (stdlib HTTP server, C-like readability). Packager and UART sender stay in Python. | — | Ismail |
| 2026-10-24 | **M2 done + M5 prep:** confirmed hardware breakpoints stop in app (`main.c:107`, `0x0801051E`) and bootloader (`main.c:139`, `0x08000646`). Updated `.gitignore` to block signing keys (`*.pem`, `*.key`, `keys/`) ahead of M5. Hit the 6-breakpoint hardware limit (Bug 1). | `18ee3f0` | Ismail |
| 2026-10-24 | **M3 host side done:** drafted [`shared/uart_protocol.md`](../shared/uart_protocol.md), implemented [`tools/send_update.py`](../tools/send_update.py), [`tools/uart_protocol.py`](../tools/uart_protocol.py), and [`tools/fake_board.py`](../tools/fake_board.py). 23 unit tests in [`tools/test_update.py`](../tools/test_update.py) pass (lost ACKs, damaged chunks, junk bytes, wrong slot, silent board, full 192 KB image). Replies name the frame type they answer. | `3a85e53`, `6da3f36` | Ismail |
| 2026-10-24 | Added `SlotB` build config in `stm32/app` with `STM32F411RETX_FLASH_B.ld` so both Slot A and Slot B binaries build from one project. | `8b2aa41` | Ismail |
| 2026-10-24 | **M3 started:** wrote [`shared/image_format.md`](../shared/image_format.md) (128-byte header with magic, version, size, CRC-32, load addr, header CRC, reserved signature slot) and [`tools/package.py`](../tools/package.py) (`pack` + `info`) with 8 unit tests in [`tools/test_package.py`](../tools/test_package.py). | `8d49fa2` | Ismail |
| 2026-10-24 | **M2 hardware jump working:** created separate `stm32/bootloader` (`0x08000000`, 32 KB) and `stm32/app` (`0x08010000`, 192 KB) CubeIDE projects. Bootloader validates SP and reset vector, disables interrupts/clocks, sets VTOR, and jumps; app prints `"APP A"`. | `19544e6` | Ismail |
| 2026-10-24 | Repeated ESP32 ↔ STM32 link test on Ismail's boards (passes). Added non-blocking 1 Hz `"APP A"` print over USART2 (`HAL_GetTick`). Observed junk bytes on XIAO while STM32 was reflashing (TX pin undriven during reset). | `c22ba27` | Ismail |
| 2026-10-20 | Two-way UART link verified: XIAO sends `'T'` every second, Nucleo toggles LED and replies `'A'`. Wiring: XIAO D6 (TX) → PA10, XIAO D7 (RX) ← PA9, shared GND. | `40b2a8e`, `5dbe781` | Ronit |
| 2026-10-15 | LED blink bring-up on NUCLEO-F411RE (`stm32/board_test`, HAL). | `95215d0` | Ronit |
| 2026-10-15 | Repo initialized with `stm32/`, `esp32/`, `tools/`, `docs/` folder layout. | `95209a9`, `322412b` | Ronit |

---

## Bug log

A working demo proves little; a bug we diagnosed and explained proves we can debug. Logged in the order hit using four parts: **Symptom**, **Root cause**, **Fix**, **Lesson**.

### Bug 1 — "Failed to insert all hardware breakpoints" (2026-10-24)
- **Symptom:** Debugging the bootloader, CubeIDE refused to resume with *"too many hardware breakpoints/watchpoints"*. After clearing them, a breakpoint on `jump_to_app()` still failed to halt once — the bootloader ran straight into the app.
- **Root cause:** Code executes from flash, which the debugger cannot patch with a software breakpoint instruction, so the Cortex-M4 uses built-in hardware address comparators — only **6** of them. CubeIDE applies every breakpoint across the entire workspace (including old IMU Logger, `board_test`, and `app` breakpoints) to every debug session, exceeding 6. Once the session hit that error, its state stayed corrupted.
- **Fix:** Breakpoints view → **Remove All**, set only the single needed breakpoint, and start a fresh debug session. Confirmed via Run to Line (`Ctrl+R`): halted at `main.c:139` (`0x08000646`).
- **Lesson:** An MCU running from flash has a small, fixed pool of hardware breakpoints (6 on Cortex-M4) — clear stale workspace breakpoints before debugging a different project.

### Watchlist — where bugs are most likely to show up
- Erasing the sector the running bootloader lives in *(mitigation: strict address-range checks before every erase/write)*
- Jumping to the app without relocating `VTOR`, causing interrupts to land in the bootloader
- Header or protocol format drifting between Python host tools and C bootloader *(mitigation: single source of truth in `shared/`)*
- Linker-script addresses not matching the flash map
- Power cut mid-write leaving half-written metadata *(mitigation: dual metadata records with sequence number + CRC)*

---

## Core concepts & architecture reference

Plain-English explanations tied directly to our codebase.

| Concept | Plain English | Where it shows up |
|---|---|---|
| Flash memory | Non-volatile on-chip storage that keeps code when power is off | Bootloader, metadata, Slot A, Slot B |
| Sector | Smallest erasable chunk of flash. On STM32F411: four 16 KB, one 64 KB, three 128 KB | Drives the flash map boundaries |
| Bootloader | Small program at `0x08000000` that always runs first and decides which slot to boot | `stm32/bootloader` (M2+) |
| Linker script (`.ld`) | Tells the linker which flash/RAM addresses a binary will live at | `STM32F411RETX_FLASH.ld` / `FLASH_B.ld` |
| Vector table | First words of an app: initial stack pointer (word 0), then reset handler and ISR addresses | Bootloader validates words 0–1 before jumping |
| `VTOR` | CPU register pointing to the active vector table | Must be set to the app's slot address so interrupts hit the app |
| CRC-32 | 4-byte checksum catching accidental corruption; proves nothing about authenticity | Every UART frame, image header, payload, and metadata record |
| Digital signature | Signed with private key (on PC), verified with public key (in bootloader); proves origin + integrity | Milestone 5 (Ed25519 in header bytes 64–127) |
| Watchdog (IWDG) | Hardware timer that resets the MCU if firmware hangs and stops petting it | Milestone 4 trial boot safety net |
| Rollback | Falling back to the last confirmed slot after a new image fails its trial | Milestone 4 |
| A/B slots | Two app regions: one runs while the other receives the update; the working copy is untouched until the new one confirms | Entire design |

### Boot states (Milestone 4)
Each slot is always in exactly one state:
1. `EMPTY` — nothing valid stored
2. `PENDING` — new image received and CRC passed; try it on next reset
3. `TRIAL` — bootloader started it with the watchdog active
4. `CONFIRMED` — app called `boot_confirm()` in time; now the known-good copy
5. `BAD` — failed its trial attempts; never booted again

### Power-safe metadata (Milestone 4)
If power dies while writing *"Slot B is now active"*, a half-written record could brick the board. We store **two records** (across sectors 2–3), each with a monotonically increasing sequence number and its own CRC-32. A new record is written in full before it counts; on boot, the bootloader picks the newest record with a valid CRC and ignores any torn write.

### Update delivery pipeline
- **Packager (`tools/package.py`):** prepends the 128-byte header (`magic`, `fw_version`, `payload_size`, `payload_crc32`, `load_addr`, `header_crc32`, and reserved `signature` space) and pads the payload with `0xFF` to a 4-byte boundary.
- **Serial sender (`tools/send_update.py`, M3):** streams numbered ≤256-byte frames over UART (`START` / `DATA` / `END`), each with its own CRC-32, waiting for an `ACK` naming the frame type before sending the next chunk.
- **Update server (Go, M6):** hosts the current signed image, serves a JSON manifest (version, size, signature), and supports HTTP byte-range requests so the ESP32-C3 can pull 1 KB chunks and resume cleanly after dropouts.

---

## Required failure tests

Breaking the system on purpose and proving it recovers is the real demo. Record evidence (serial logs, screenshots, or short videos) in `docs/` as each test is run.

| Test | How to cause it | Expected result | Result | Evidence |
|---|---|---|---|---|
| Bad header | Corrupt the `OTA1` magic bytes | Rejected before erasing or booting anything | Not run | — |
| Corrupt payload | Flip one byte in the app payload | CRC-32 fails; old app keeps running | Not run | — |
| Invalid signature | Sign with a different private key | Signature verification fails; old app keeps running | Not run | — |
| Interrupted transfer | Unplug UART / drop Wi-Fi mid-download | Old app unaffected; next attempt resumes or restarts cleanly | Not run | — |
| Power loss while writing | Cut power at repeatable points during flash write | Next boot ignores the half-written image | Not run | — |
| Trial app hangs | Flash an app that loops forever before confirming | Watchdog resets; rolls back after allowed attempts | Not run | — |
| Metadata write interrupted | Cut power while saving a new metadata record | Bootloader picks the older valid record, never guesses | Not run | — |
| Oversized image | Package a binary larger than 192 KB | Linker or packager refuses it before sending | Not run | — |

---

## Interview talking points

### "Walk me through the project."
> Thirty seconds: An STM32 bootloader that installs firmware updates safely — it verifies every new image with a CRC-32 and digital signature, trial-boots it under a hardware watchdog, and rolls back automatically to the last confirmed version if the new app fails. Updates arrive over UART or Wi-Fi via an ESP32-C3. Ronit led the STM32 bootloader/flash side and Ismail led the host packager, sender, and Go update server, building and testing every milestone together.

### "What happens if power dies halfway through an update?"
> The running app lives in the other slot and is never touched until the new image confirms itself. Metadata is stored as two records with sequence numbers and CRCs, so a torn write fails its CRC and the bootloader falls back to the previous valid record.

### "Why isn't a CRC enough?"
> A CRC catches accidental corruption, not attackers — anyone can modify a binary and recompute the CRC. A digital signature requires our private key (which never leaves the PC). CRC = *"is it damaged?"*, signature = *"is it ours?"*

### "How does the bootloader hand control to the app?"
> Read the app's first two words: verify the initial stack pointer points into SRAM (`0x20000000`–`0x20020000`) and the reset handler points inside the target slot. Then disable interrupts, de-init peripherals/clocks, set MSP to the app's stack pointer, point `VTOR` to the app's vector table, and jump to its reset handler.

### "Why didn't you write your own crypto?"
> Home-made crypto is where subtle security bugs hide. We use a well-tested library and validate it against standard test vectors on the PC first, then on the STM32.

### Details to have ready
- Why the flash map follows sector boundaries (STM32F4 erases per whole sector).
- Why the max app size is **192 KB**, not 256 KB (must fit in either slot, and Slot A is 192 KB).
- Why the STM32F4 hardware CRC peripheral wasn't used (no bit reflection, no final XOR, word-only — doesn't match standard IEEE CRC-32 `0xCBF43926`).
- What the per-chunk `ACK` prevents (overrunning flash write speed over UART).
- Why every `ACK`/`NAK` echoes the frame `type` (`START`, `END`, and `DATA 0` all use `seq 0`, so a late `ACK` for `START` could otherwise be mistaken for `DATA 0`).
- Why a resent chunk isn't written twice (if an `ACK` is lost, the PC retries; the bootloader sees the duplicate `seq` and re-ACKs without re-programming flash).
- Why the XIAO saw junk bytes while the STM32 was reflashing (STM32 TX pin goes undriven during reset).
- What watchdog timeout we chose and why *(fill in during M4)*.

---

## Known debt & housekeeping

- **`stm32/board_test` still in repo:** throwaway M1 bring-up project (`USER CODE BEGIN 2` markers are still doubled in its `main.c`). Agree and delete it rather than fixing it.
- **`shared/uart_protocol.md` header still says `DRAFT`:** M3 is now implemented against it (`f276934`); update the header status to finalized.
- **ESP32 link built ahead of plan:** fine as an early wiring check (`40b2a8e`, `5dbe781`), but pause all Wi-Fi work until M4 rollback is verified.
- **CubeIDE import noise:** never commit local `.cproject` / `language.settings.xml` churn or stray root `.project` files; discard with `git restore`.
- **`.gitignore` key protection (done):** blocks `*.pem`, `*.key`, and `keys/` (`18ee3f0`), verified ahead of M5.
- **192 KB limit enforced twice (done):** both app linker scripts (`LENGTH = 192K`) and `tools/package.py` reject oversized binaries.
- **Repo layout note:** uses `stm32/`, `esp32/`, `shared/`, `tools/`, `docs/`. Decide where the M6 Go update server will live (e.g., `server/`) before starting M6.

---

## Tie-in with the IMU Logger

Once Milestone 4 works, the IMU Logger becomes a real application in Slot A: relocate its linker script start to `0x08010000` (192 KB) and set `VTOR` accordingly.

---

## Repository map

- **GitHub:** `github.com/ronitbhanot4/stm32-ota-bootloader`
- `stm32/bootloader/` — custom bootloader (`0x08000000`, 32 KB)
- `stm32/app/` — application project with Slot A (`0x08010000`) and Slot B (`0x08040000`) build configs
- `stm32/board_test/` — legacy M1 bring-up project (scheduled for deletion)
- `shared/` — byte-level specs ([`image_format.md`](../shared/image_format.md), [`uart_protocol.md`](../shared/uart_protocol.md))
- `tools/` — Python packager, UART sender, fake board, and unit tests
- `esp32/` — XIAO ESP32-C3 UART link test (M6 Wi-Fi client goes here)
- `docs/` — this tracker and future failure-test evidence
