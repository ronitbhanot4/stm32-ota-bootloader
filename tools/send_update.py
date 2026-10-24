import argparse
import sys
import time

import package
import uart_protocol as up

TIMEOUTS = {up.START: 15.0, up.DATA: 1.0, up.END: 3.0}
RETRIES = 5


class UpdateError(Exception):
    pass


def wait_reply(port, decoder, ftype, seq, timeout):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        data = port.read(up.MAX_FRAME)
        if not data:
            continue
        for f in decoder.feed(data):
            if f.ok and f.type in (up.ACK, up.NAK) and f.seq == seq \
                    and f.payload[:1] == bytes([ftype]):
                return f
    return None


def transact(port, decoder, ftype, seq, payload, timeouts, log):
    name = f"{up.TYPE_NAMES[ftype]} {seq}"
    frame = up.encode(up.SOF_PC, ftype, seq, payload)
    for attempt in range(1, RETRIES + 1):
        port.write(frame)
        reply = wait_reply(port, decoder, ftype, seq, timeouts[ftype])
        if reply is None:
            log(f"no reply to {name}, retry {attempt}/{RETRIES}")
            continue
        if reply.type == up.ACK:
            return attempt
        reason = reply.payload[1] if len(reply.payload) > 1 else 0
        if reason == up.FRAME_CRC:
            log(f"{name} arrived damaged, retry {attempt}/{RETRIES}")
            continue
        raise UpdateError(f"board rejected {name}: {up.REASON_NAMES.get(reason, reason)}")
    raise UpdateError(f"no good reply to {name} after {RETRIES} tries")


def send_image(port, image, timeouts=TIMEOUTS, log=print, progress=None):
    info = package.parse_image(image)
    header = image[:package.HEADER_SIZE]
    payload = image[package.HEADER_SIZE:]
    chunks = [payload[i:i + up.MAX_CHUNK] for i in range(0, len(payload), up.MAX_CHUNK)]
    decoder = up.Decoder(up.SOF_BOARD)
    started = time.monotonic()
    port.reset_input_buffer()

    log(f"sending {package.describe(info)} in {len(chunks)} chunks")
    retries = transact(port, decoder, up.START, 0, header, timeouts, log) - 1
    for seq, chunk in enumerate(chunks):
        retries += transact(port, decoder, up.DATA, seq, chunk, timeouts, log) - 1
        if progress:
            progress(seq + 1, len(chunks))
    retries += transact(port, decoder, up.END, 0, b"", timeouts, log) - 1

    return {"chunks": len(chunks), "retries": retries, "seconds": time.monotonic() - started}


def open_port(args):
    if args.fake:
        from fake_board import FakeBoard, FakePort
        return FakePort(FakeBoard(drop_ack=args.drop_ack, corrupt=args.corrupt, junk=args.junk))
    try:
        import serial
    except ImportError:
        raise UpdateError("pyserial is not installed: run  pip install pyserial")
    try:
        return serial.Serial(args.port, args.baud, timeout=0.05)
    except serial.SerialException as e:
        raise UpdateError(f"can't open {args.port} (is a Serial Monitor using it?): {e}")


def main(argv=None):
    parser = argparse.ArgumentParser(description="Send an update image to the bootloader.")
    parser.add_argument("image")
    where = parser.add_mutually_exclusive_group(required=True)
    where.add_argument("--port", help="serial port, e.g. COM3")
    where.add_argument("--fake", action="store_true", help="send to a simulated board")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--drop-ack", type=int, nargs="*", default=[], metavar="SEQ",
                        help="fake board: lose the ACK for these DATA chunks")
    parser.add_argument("--corrupt", type=int, nargs="*", default=[], metavar="SEQ",
                        help="fake board: damage these DATA chunks on the wire")
    parser.add_argument("--junk", action="store_true",
                        help="fake board: send junk bytes before every reply")
    args = parser.parse_args(argv)

    def progress(done, total):
        print(f"\r  chunk {done}/{total}", end="", flush=True)
        if done == total:
            print()

    try:
        with open(args.image, "rb") as f:
            image = f.read()
        port = open_port(args)
        try:
            stats = send_image(port, image, log=lambda m: print(f"\r  {m}"), progress=progress)
        finally:
            port.close()
    except (UpdateError, package.ImageError, OSError) as e:
        print(f"\nerror: {e}", file=sys.stderr)
        return 1
    print(f"done: Slot B CRC matches ({stats['chunks']} chunks, "
          f"{stats['retries']} retries, {stats['seconds']:.1f} s)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
