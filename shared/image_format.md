# Update image format (v1)

The exact byte layout of a firmware update file. The Python packager (`tools/package.py`) writes it; the bootloader reads it. **Change this file first, then both sides** — if they drift apart, every update gets rejected.

An image is a **128-byte header** followed by the **payload** (the app's raw `.bin`, padded).

All numbers are **little-endian** (lowest byte first — what the STM32 and Python's `struct '<'` both use).

## Header

| Offset | Size | Field | Value |
|---|---|---|---|
| 0 | 4 | `magic` | ASCII `OTA1` (bytes `4F 54 41 31`, read as a u32 = `0x3141544F`) |
| 4 | 2 | `header_version` | `1` |
| 6 | 2 | `header_size` | `128` |
| 8 | 4 | `fw_version` | App version number. Must go up each release. |
| 12 | 4 | `payload_size` | Payload length in bytes, after padding. Multiple of 4. Max 196,608 (192 KB). |
| 16 | 4 | `payload_crc32` | CRC-32 of the payload (see below) |
| 20 | 4 | `load_addr` | Slot the app was linked for: `0x08010000` (A) or `0x08040000` (B) |
| 24 | 4 | `flags` | `0` (reserved) |
| 28 | 4 | `header_crc32` | CRC-32 of header bytes 0–27 |
| 32 | 32 | reserved | all `0x00` |
| 64 | 64 | `signature` | all `0x00` until Milestone 5 (Ed25519 signature goes here) |

## Payload

- The app's `.bin` exactly as CubeIDE outputs it, padded with `0xFF` up to a multiple of 4 bytes (flash is written 4 bytes at a time; `0xFF` is what erased flash already holds).
- Word 0 must be a stack pointer inside RAM (`0x20000000`–`0x20020000`).
- Word 1 must be a reset address inside the slot named by `load_addr`.

## CRC-32

Standard CRC-32 (the zip/Ethernet one): polynomial `0x04C11DB7` reflected (`0xEDB88320`), start `0xFFFFFFFF`, final XOR `0xFFFFFFFF`. Same as Python's `zlib.crc32()`.

Check value: CRC-32 of ASCII `123456789` = `0xCBF43926`.

**Bootloader warning:** the STM32F4's built-in CRC hardware does **not** compute this variant (no bit reflection, no final XOR, works on whole words). Use a software CRC in C, and test it against `0xCBF43926` before trusting it.

## Bootloader rejects the image if

1. `magic`, `header_version` or `header_size` don't match
2. `header_crc32` is wrong
3. `payload_size` is 0, not a multiple of 4, or over 196,608
4. `load_addr` isn't the slot being written
5. `payload_crc32` doesn't match after the payload is written and read back from flash
