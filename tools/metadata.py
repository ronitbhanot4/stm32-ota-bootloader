import argparse
from dataclasses import dataclass
import struct
import sys

from package import crc32

MAGIC = b"META"
FORMAT_VERSION = 1
RECORD_SIZE = 64
MAX_ATTEMPTS = 3

SECTOR_SIZE = 16 * 1024
REGION_SIZE = 2 * SECTOR_SIZE
COPY0_ADDR = 0x08008000
COPY1_ADDR = 0x0800C000

SLOT_EMPTY = 0
SLOT_PENDING = 1
SLOT_TRIAL = 2
SLOT_CONFIRMED = 3
SLOT_BAD = 4

STATE_NAMES = {
    SLOT_EMPTY: "EMPTY",
    SLOT_PENDING: "PENDING",
    SLOT_TRIAL: "TRIAL",
    SLOT_CONFIRMED: "CONFIRMED",
    SLOT_BAD: "BAD",
}
NAME_TO_STATE = {v: k for k, v in STATE_NAMES.items()}
SLOT_NAMES = {0: "A", 1: "B"}

HEADER_STRUCT = struct.Struct("<4sHHIBBH")
SLOT_STRUCT = struct.Struct("<BBHIII")
BODY_SIZE = 60


class MetaError(Exception):
    pass


@dataclass
class SlotMeta:
    state: int = SLOT_EMPTY
    attempts_left: int = 0
    fw_version: int = 0
    payload_size: int = 0
    payload_crc32: int = 0
    reserved: int = 0


@dataclass
class BootMeta:
    seq: int
    active_slot: int
    slots: tuple
    flags: int = 0
    reserved: int = 0
    reserved2: bytes = b"\x00" * 12


def _validate_slot(slot, idx):
    if slot.state not in STATE_NAMES:
        raise MetaError(f"slot {idx}: invalid state {slot.state}")
    if not 0 <= slot.attempts_left <= 255:
        raise MetaError(f"slot {idx}: invalid attempts_left {slot.attempts_left}")


def pack_record(meta):
    if meta.seq <= 0 or meta.seq > 0xFFFFFFFF:
        raise MetaError(f"seq must be 1..0xFFFFFFFF, got {meta.seq}")
    if meta.active_slot not in (0, 1):
        raise MetaError(f"active_slot must be 0 or 1, got {meta.active_slot}")
    if len(meta.slots) != 2:
        raise MetaError("slots must have exactly 2 entries")
    if len(meta.reserved2) != 12:
        raise MetaError(f"reserved2 must be 12 bytes, got {len(meta.reserved2)}")

    for idx, slot in enumerate(meta.slots):
        _validate_slot(slot, idx)

    body = (
        HEADER_STRUCT.pack(
            MAGIC,
            FORMAT_VERSION,
            RECORD_SIZE,
            meta.seq,
            meta.active_slot,
            meta.flags,
            meta.reserved,
        )
        + SLOT_STRUCT.pack(
            meta.slots[0].state,
            meta.slots[0].attempts_left,
            meta.slots[0].reserved,
            meta.slots[0].fw_version,
            meta.slots[0].payload_size,
            meta.slots[0].payload_crc32,
        )
        + SLOT_STRUCT.pack(
            meta.slots[1].state,
            meta.slots[1].attempts_left,
            meta.slots[1].reserved,
            meta.slots[1].fw_version,
            meta.slots[1].payload_size,
            meta.slots[1].payload_crc32,
        )
        + meta.reserved2
    )
    assert len(body) == BODY_SIZE
    return body + struct.pack("<I", crc32(body))


def parse_record(data):
    if len(data) < RECORD_SIZE:
        raise MetaError(f"record too short ({len(data)} bytes, need {RECORD_SIZE})")
    raw = data[:RECORD_SIZE]

    magic, fver, rsize, seq, active_slot, flags, reserved = HEADER_STRUCT.unpack_from(raw, 0)
    if magic != MAGIC:
        raise MetaError(f"bad magic {magic!r}")
    if fver != FORMAT_VERSION or rsize != RECORD_SIZE:
        raise MetaError(f"unsupported version {fver} / record size {rsize}")
    if seq == 0:
        raise MetaError("seq must be > 0")
    if active_slot not in (0, 1):
        raise MetaError(f"bad active_slot {active_slot}")

    (stored_crc,) = struct.unpack_from("<I", raw, BODY_SIZE)
    calc_crc = crc32(raw[:BODY_SIZE])
    if stored_crc != calc_crc:
        raise MetaError(
            f"CRC mismatch: stored 0x{stored_crc:08X}, computed 0x{calc_crc:08X}"
        )

    slots = []
    for idx, offset in enumerate((16, 32)):
        state, attempts, sres, fw_ver, psize, pcrc = SLOT_STRUCT.unpack_from(raw, offset)
        slot = SlotMeta(
            state=state,
            attempts_left=attempts,
            fw_version=fw_ver,
            payload_size=psize,
            payload_crc32=pcrc,
            reserved=sres,
        )
        _validate_slot(slot, idx)
        slots.append(slot)

    return BootMeta(
        seq=seq,
        active_slot=active_slot,
        slots=(slots[0], slots[1]),
        flags=flags,
        reserved=reserved,
        reserved2=raw[48:60],
    )


def select_active(copy0, copy1):
    valid = []
    for idx, buf in ((0, copy0), (1, copy1)):
        try:
            valid.append((idx, parse_record(buf)))
        except MetaError:
            pass
    if not valid:
        raise MetaError("no valid metadata copy found")
    return max(valid, key=lambda item: item[1].seq)


def select_from_region(region):
    if len(region) < SECTOR_SIZE + RECORD_SIZE:
        raise MetaError(
            f"region dump too small ({len(region)} bytes, need at least {SECTOR_SIZE + RECORD_SIZE})"
        )
    return select_active(region[:RECORD_SIZE], region[SECTOR_SIZE:SECTOR_SIZE + RECORD_SIZE])


def next_write_copy(active_copy):
    return 1 if active_copy == 0 else 0


def describe(meta, copy_index=None):
    header = f"seq={meta.seq}, active=Slot {SLOT_NAMES[meta.active_slot]}"
    if copy_index is not None:
        addr = COPY0_ADDR if copy_index == 0 else COPY1_ADDR
        header = f"Copy {copy_index} (0x{addr:08X}): {header}"
    lines = [header]
    for idx, slot in enumerate(meta.slots):
        lines.append(
            f"  Slot {SLOT_NAMES[idx]}: {STATE_NAMES[slot.state]:<9} "
            f"attempts={slot.attempts_left} ver={slot.fw_version} "
            f"size={slot.payload_size} crc=0x{slot.payload_crc32:08X}"
        )
    return "\n".join(lines)


def _inspect_cmd(path):
    with open(path, "rb") as f:
        data = f.read()

    if len(data) >= SECTOR_SIZE + RECORD_SIZE:
        for idx, offset in ((0, 0), (1, SECTOR_SIZE)):
            addr = COPY0_ADDR if idx == 0 else COPY1_ADDR
            try:
                m = parse_record(data[offset:offset + RECORD_SIZE])
                print(f"[VALID]   {describe(m, copy_index=idx)}")
            except MetaError as e:
                print(f"[INVALID] Copy {idx} (0x{addr:08X}): {e}")
        active_idx, active = select_from_region(data)
        print(f"\nActive: Copy {active_idx} (seq={active.seq}, next write -> Copy {next_write_copy(active_idx)})")
    else:
        meta = parse_record(data)
        print(describe(meta))


def _build_cmd(args):
    active_slot = 0 if args.active == "A" else 1
    meta = BootMeta(
        seq=args.seq,
        active_slot=active_slot,
        slots=(
            SlotMeta(
                state=NAME_TO_STATE[args.slot_a],
                attempts_left=args.attempts_a,
                fw_version=args.ver_a,
                payload_size=args.size_a,
                payload_crc32=args.crc_a,
            ),
            SlotMeta(
                state=NAME_TO_STATE[args.slot_b],
                attempts_left=args.attempts_b,
                fw_version=args.ver_b,
                payload_size=args.size_b,
                payload_crc32=args.crc_b,
            ),
        ),
    )
    raw = pack_record(meta)
    with open(args.out, "wb") as f:
        f.write(raw)
    print(f"wrote {args.out} ({len(raw)} bytes):\n{describe(meta)}")


def main(argv=None):
    parser = argparse.ArgumentParser(description="Build or inspect BootMeta flash records.")
    sub = parser.add_subparsers(dest="cmd", required=True)

    insp = sub.add_parser("inspect", help="inspect a 64-byte record, 16 KB sector, or 32 KB metadata dump")
    insp.add_argument("file")

    bld = sub.add_parser("build", help="create a 64-byte BootMeta record")
    bld.add_argument("-o", "--out", required=True)
    bld.add_argument("--seq", type=int, default=1)
    bld.add_argument("--active", choices=("A", "B"), default="A")
    bld.add_argument("--slot-a", choices=tuple(NAME_TO_STATE), default="CONFIRMED")
    bld.add_argument("--attempts-a", type=int, default=0)
    bld.add_argument("--ver-a", type=int, default=1)
    bld.add_argument("--size-a", type=int, default=0)
    bld.add_argument("--crc-a", type=lambda s: int(s, 0), default=0)
    bld.add_argument("--slot-b", choices=tuple(NAME_TO_STATE), default="EMPTY")
    bld.add_argument("--attempts-b", type=int, default=0)
    bld.add_argument("--ver-b", type=int, default=0)
    bld.add_argument("--size-b", type=int, default=0)
    bld.add_argument("--crc-b", type=lambda s: int(s, 0), default=0)

    args = parser.parse_args(argv)
    try:
        if args.cmd == "inspect":
            _inspect_cmd(args.file)
        else:
            _build_cmd(args)
    except (MetaError, OSError) as e:
        print(f"error: {e}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
