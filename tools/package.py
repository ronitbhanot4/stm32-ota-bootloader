import argparse
import struct
import sys
import zlib

MAGIC = b"OTA1"
HEADER_VERSION = 1
HEADER_SIZE = 128
MAX_PAYLOAD = 192 * 1024
SLOTS = {"A": 0x08010000, "B": 0x08040000}
SLOT_SIZE = {"A": 192 * 1024, "B": 256 * 1024}
RAM_START = 0x20000000
RAM_END = 0x20020000

HEADER_FIELDS = struct.Struct("<4sHHIIIII")


class ImageError(Exception):
    pass


def crc32(data):
    return zlib.crc32(data) & 0xFFFFFFFF


def pad4(data):
    return data + b"\xFF" * (-len(data) % 4)


def check_vectors(payload, slot):
    if len(payload) < 8:
        raise ImageError("binary too small to hold a vector table")
    sp, reset = struct.unpack_from("<II", payload, 0)
    start = SLOTS[slot]
    end = start + SLOT_SIZE[slot]
    if not RAM_START <= sp <= RAM_END:
        raise ImageError(f"stack pointer 0x{sp:08X} is not in RAM")
    if not start <= reset < end:
        raise ImageError(
            f"reset address 0x{reset:08X} is not in slot {slot} "
            f"(0x{start:08X}-0x{end - 1:08X}); was the app linked for slot {slot}?"
        )


def build_image(binary, slot, fw_version):
    payload = pad4(binary)
    if len(payload) > MAX_PAYLOAD:
        raise ImageError(f"payload is {len(payload)} bytes, max is {MAX_PAYLOAD}")
    check_vectors(payload, slot)
    first28 = HEADER_FIELDS.pack(
        MAGIC, HEADER_VERSION, HEADER_SIZE, fw_version,
        len(payload), crc32(payload), SLOTS[slot], 0,
    )
    header = first28 + struct.pack("<I", crc32(first28))
    header += b"\x00" * (HEADER_SIZE - len(header))
    return header + payload


def parse_image(image):
    if len(image) < HEADER_SIZE:
        raise ImageError("file is shorter than a header")
    magic, hver, hsize, fw_version, size, pcrc, load_addr, flags = \
        HEADER_FIELDS.unpack_from(image, 0)
    (hcrc,) = struct.unpack_from("<I", image, 28)
    if magic != MAGIC:
        raise ImageError(f"bad magic {magic!r}")
    if hver != HEADER_VERSION or hsize != HEADER_SIZE:
        raise ImageError(f"unsupported header version {hver} / size {hsize}")
    if hcrc != crc32(image[:28]):
        raise ImageError("header CRC mismatch")
    payload = image[HEADER_SIZE:]
    if len(payload) != size:
        raise ImageError(f"header says {size} payload bytes, file has {len(payload)}")
    if crc32(payload) != pcrc:
        raise ImageError("payload CRC mismatch")
    slot = next((s for s, a in SLOTS.items() if a == load_addr), None)
    if slot is None:
        raise ImageError(f"load address 0x{load_addr:08X} is not a slot")
    check_vectors(payload, slot)
    return {
        "fw_version": fw_version,
        "payload_size": size,
        "payload_crc32": pcrc,
        "load_addr": load_addr,
        "slot": slot,
        "header_crc32": hcrc,
    }


def describe(info):
    return (
        f"slot {info['slot']} (0x{info['load_addr']:08X}), "
        f"version {info['fw_version']}, "
        f"{info['payload_size']} bytes, "
        f"payload CRC 0x{info['payload_crc32']:08X}"
    )


def main(argv=None):
    parser = argparse.ArgumentParser(description="Build or inspect OTA update images.")
    sub = parser.add_subparsers(dest="cmd", required=True)

    pack = sub.add_parser("pack", help="wrap an app .bin in an update header")
    pack.add_argument("bin")
    pack.add_argument("--slot", choices=SLOTS, required=True)
    pack.add_argument("--version", type=int, required=True)
    pack.add_argument("-o", "--out", required=True)

    info = sub.add_parser("info", help="check an image and print its header")
    info.add_argument("image")

    args = parser.parse_args(argv)
    try:
        if args.cmd == "pack":
            with open(args.bin, "rb") as f:
                image = build_image(f.read(), args.slot, args.version)
            with open(args.out, "wb") as f:
                f.write(image)
            print(f"wrote {args.out}: {describe(parse_image(image))}")
        else:
            with open(args.image, "rb") as f:
                print(f"OK: {describe(parse_image(f.read()))}")
    except (ImageError, OSError) as e:
        print(f"error: {e}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
