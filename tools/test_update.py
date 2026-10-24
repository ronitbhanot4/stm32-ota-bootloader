import unittest

import package
import uart_protocol as up
from fake_board import FakeBoard, FakePort
from send_update import UpdateError, send_image
from test_package import fake_bin

FAST = {up.START: 0.2, up.DATA: 0.05, up.END: 0.1}


def image_for(slot, size=8568):
    return package.build_image(fake_bin(slot, size), slot, 2)


def send(board, image):
    return send_image(FakePort(board, timeout=0.002), image, timeouts=FAST, log=lambda m: None)


def payload_of(image):
    return image[package.HEADER_SIZE:]


class TestFrames(unittest.TestCase):
    def test_round_trip(self):
        frame = up.encode(up.SOF_PC, up.DATA, 7, b"\x01\x02\x03\x04")
        (f,) = up.Decoder(up.SOF_PC).feed(frame)
        self.assertEqual((f.type, f.seq, f.payload, f.ok), (up.DATA, 7, b"\x01\x02\x03\x04", True))

    def test_split_across_reads(self):
        frame = up.encode(up.SOF_PC, up.DATA, 1, bytes(256))
        d = up.Decoder(up.SOF_PC)
        self.assertEqual(d.feed(frame[:100]), [])
        (f,) = d.feed(frame[100:])
        self.assertTrue(f.ok)

    def test_junk_before_frame_ignored(self):
        frame = up.encode(up.SOF_PC, up.END, 0)
        (f,) = up.Decoder(up.SOF_PC).feed(b"\x00\x13\x37garbage" + frame)
        self.assertEqual(f.type, up.END)

    def test_damaged_frame_flagged(self):
        frame = bytearray(up.encode(up.SOF_PC, up.DATA, 3, bytes(16)))
        frame[10] ^= 0xFF
        (f,) = up.Decoder(up.SOF_PC).feed(bytes(frame))
        self.assertFalse(f.ok)


class TestSender(unittest.TestCase):
    def test_happy_path(self):
        image = image_for("B")
        board = FakeBoard()
        stats = send(board, image)
        size = len(payload_of(image))
        self.assertEqual(bytes(board.flash[:size]), payload_of(image))
        self.assertEqual(stats["chunks"], 34)
        self.assertEqual(stats["retries"], 0)

    def test_lost_ack_is_resent_but_written_once(self):
        board = FakeBoard(drop_ack={5, 20})
        stats = send(board, image_for("B"))
        self.assertEqual(stats["retries"], 2)
        self.assertEqual(board.writes, 34)

    def test_damaged_chunk_is_resent(self):
        image = image_for("B")
        board = FakeBoard(corrupt={0, 33})
        stats = send(board, image)
        self.assertEqual(stats["retries"], 2)
        size = len(payload_of(image))
        self.assertEqual(bytes(board.flash[:size]), payload_of(image))

    def test_junk_between_replies(self):
        send(FakeBoard(junk=True), image_for("B"))

    def test_slot_a_image_rejected(self):
        with self.assertRaisesRegex(UpdateError, "WRONG_SLOT"):
            send(FakeBoard(), image_for("A"))

    def test_silent_board_gives_up(self):
        class DeadBoard(FakeBoard):
            def handle(self, f):
                pass
        with self.assertRaisesRegex(UpdateError, "after 5 tries"):
            send(DeadBoard(), image_for("B"))

    def test_largest_image(self):
        image = image_for("B", package.MAX_PAYLOAD)
        board = FakeBoard()
        self.assertEqual(send(board, image)["chunks"], 768)


class TestFakeBoard(unittest.TestCase):
    def reply(self, board, ftype, seq, payload=b""):
        board.receive(up.encode(up.SOF_PC, ftype, seq, payload))
        (f,) = up.Decoder(up.SOF_BOARD).feed(bytes(board.out))
        board.out.clear()
        return f

    def test_data_before_start(self):
        f = self.reply(FakeBoard(), up.DATA, 0, bytes(4))
        self.assertEqual((f.type, f.payload[1]), (up.NAK, up.NOT_STARTED))

    def test_out_of_order_chunk(self):
        board = FakeBoard()
        self.reply(board, up.START, 0, image_for("B")[:package.HEADER_SIZE])
        f = self.reply(board, up.DATA, 5, bytes(256))
        self.assertEqual((f.type, f.payload[1]), (up.NAK, up.BAD_SEQ))

    def test_chunk_past_end(self):
        board = FakeBoard()
        self.reply(board, up.START, 0, image_for("B", 64)[:package.HEADER_SIZE])
        f = self.reply(board, up.DATA, 0, bytes(128))
        self.assertEqual((f.type, f.payload[1]), (up.NAK, up.TOO_BIG))

    def test_missing_chunk_fails_crc_at_end(self):
        image = image_for("B", 512)
        board = FakeBoard()
        self.reply(board, up.START, 0, image[:package.HEADER_SIZE])
        self.reply(board, up.DATA, 0, payload_of(image)[:256])
        f = self.reply(board, up.END, 0)
        self.assertEqual((f.type, f.payload[1]), (up.NAK, up.IMAGE_CRC))


if __name__ == "__main__":
    unittest.main()
