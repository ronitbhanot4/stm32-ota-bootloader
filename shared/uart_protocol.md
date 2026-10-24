# UART update protocol (v1 — DRAFT, needs Ronit's OK)

How the PC sends an update image (see `image_format.md`) to the bootloader over a serial cable, in small checked pieces.

**Status: draft.** Ronit and Ismail both sign off before the receiver is written. Change this file first, then the code.

## The link

- USART2 — the Nucleo's USB cable (ST-LINK virtual COM port). 115200 baud, 8 data bits, no parity, 1 stop bit.
- **Entering update mode:** hold the blue USER button while pressing RESET. The bootloader prints `BOOT: update mode` once, then sends **only reply frames** until the update ends. Without the button it boots the app as normal.
- In Milestone 6 the same frames travel over USART1 via the XIAO instead. Nothing else changes.

## Frames

Both directions use the same frame shape. All numbers little-endian.

| Offset | Size | Field | Notes |
|---|---|---|---|
| 0 | 1 | `sof` | Start-of-frame marker. `0xA5` PC→board, `0x5A` board→PC |
| 1 | 1 | `type` | See below |
| 2 | 2 | `seq` | Chunk number. Replies echo the `seq` they answer |
| 4 | 2 | `len` | Payload length, 0–256 |
| 6 | `len` | `payload` | |
| 6+`len` | 4 | `crc32` | CRC-32 of bytes 1 to 5+`len` (`type` through end of payload). Same CRC as `image_format.md` |

Biggest frame: 6 + 256 + 4 = 266 bytes.

### PC → board

| `type` | Name | `seq` | Payload | Board does |
|---|---|---|---|---|
| `0x01` | `START` | 0 | The 128-byte image header | Checks the header (rules 1–4 in `image_format.md`), erases Slot B, then replies |
| `0x02` | `DATA` | 0, 1, 2… | Next ≤256 bytes of the image payload (multiple of 4) | Writes to Slot B at offset `seq × 256` |
| `0x03` | `END` | 0 | none | Reads Slot B back, CRC-32s `payload_size` bytes, compares with the header |

### Board → PC

| `type` | Name | Payload |
|---|---|---|
| `0x80` | `ACK` | none — "done, send the next one" |
| `0x81` | `NAK` | 1 byte: reason code |

| Reason | Name | Meaning | PC should |
|---|---|---|---|
| 1 | `FRAME_CRC` | Frame arrived damaged | Resend the same frame |
| 2 | `BAD_SEQ` | Chunk out of order | Give up |
| 3 | `BAD_HEADER` | Header failed a check | Give up |
| 4 | `WRONG_SLOT` | Image built for the other slot | Give up |
| 5 | `TOO_BIG` | Payload over 192 KB, or a chunk past the end | Give up |
| 6 | `FLASH_ERROR` | Erase or write failed | Give up |
| 7 | `IMAGE_CRC` | Read-back CRC doesn't match the header | Give up |
| 8 | `NOT_STARTED` | `DATA`/`END` before a good `START` | Give up |

## Rules

**PC (sender)**
- Send one frame, then wait for its reply before sending anything else.
- Wait up to **15 s** after `START` (erasing two 128 KB sectors takes seconds), **1 s** after `DATA`, **3 s** after `END`.
- No reply, or `NAK FRAME_CRC` → resend the same frame. Give up after 5 tries.
- Any other `NAK` → stop and print the reason.

**Board (receiver)**
- Ignore bytes until it sees `0xA5`. A `len` over 256 → throw the frame away and look for the next `0xA5`.
- More than **100 ms** of silence in the middle of a frame → throw it away. (Junk bytes, like the ones seen when a board resets, can't jam the receiver.)
- `DATA` with `seq` = the one expected → write it, `ACK`, expect `seq + 1`.
- `DATA` with `seq` = the one **just** written → don't write again, just `ACK` again. (The PC never got the first `ACK` and resent.)
- Any other `seq` → `NAK BAD_SEQ`.
- Never write outside Slot B (`0x08040000`–`0x0807FFFF`), whatever the frames say.
- The 128-byte header stays in RAM for Milestone 3. Milestone 4 decides where it lives in flash.
- Slot B is never booted in Milestone 3.

## Example (an 8,568-byte payload = 34 chunks)

```
PC  → START seq 0  [128-byte header]      board erases Slot B (~2-4 s)
PC  ← ACK   seq 0
PC  → DATA  seq 0  [bytes 0–255]
PC  ← ACK   seq 0
...
PC  → DATA  seq 33 [bytes 8448–8567, 120 bytes]
PC  ← ACK   seq 33
PC  → END   seq 0
PC  ← ACK   seq 0                          read-back CRC matched
```

## Open questions for Ronit

- 256-byte chunks OK, or smaller to save RAM?
- Erase both Slot B sectors every time (simple), or only the ones the image needs (faster)?
- Should the board print one human-readable line (`BOOT: Slot B CRC OK`) after the final `ACK`?
