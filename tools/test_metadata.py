import struct
import unittest

import metadata as m


def sample_meta(seq=1, active_slot=0, state_a=m.SLOT_CONFIRMED, state_b=m.SLOT_EMPTY):
    return m.BootMeta(
        seq=seq,
        active_slot=active_slot,
        slots=(
            m.SlotMeta(state=state_a, attempts_left=0, fw_version=1, payload_size=8568, payload_crc32=0x11223344),
            m.SlotMeta(state=state_b, attempts_left=3, fw_version=2, payload_size=8568, payload_crc32=0xB68CF200),
        ),
    )


class TestMetadata(unittest.TestCase):
    def test_record_size_and_offsets(self):
        raw = m.pack_record(sample_meta(seq=5, active_slot=1, state_b=m.SLOT_PENDING))
        self.assertEqual(len(raw), 64)
        self.assertEqual(raw[0:4], b"META")
        self.assertEqual(struct.unpack_from("<I", raw, 0)[0], 0x4154454D)
        self.assertEqual(struct.unpack_from("<HH", raw, 4), (1, 64))
        self.assertEqual(struct.unpack_from("<I", raw, 8)[0], 5)
        self.assertEqual(raw[12], 1)
        # Slot A at offset 16, Slot B at offset 32
        self.assertEqual(raw[16], m.SLOT_CONFIRMED)
        self.assertEqual(raw[32], m.SLOT_PENDING)
        self.assertEqual(raw[33], 3)
        self.assertEqual(struct.unpack_from("<I", raw, 44)[0], 0xB68CF200)

    def test_round_trip(self):
        orig = sample_meta(seq=7, active_slot=1, state_b=m.SLOT_TRIAL)
        raw = m.pack_record(orig)
        parsed = m.parse_record(raw)
        self.assertEqual(parsed, orig)

    def test_parse_ignores_trailing_sector_bytes(self):
        raw = m.pack_record(sample_meta(seq=2)) + b"\xFF" * (m.SECTOR_SIZE - m.RECORD_SIZE)
        parsed = m.parse_record(raw)
        self.assertEqual(parsed.seq, 2)

    def test_bad_magic_rejected(self):
        raw = bytearray(m.pack_record(sample_meta()))
        raw[0:4] = b"NOPE"
        with self.assertRaises(m.MetaError):
            m.parse_record(bytes(raw))

    def test_flipped_byte_rejected(self):
        raw = bytearray(m.pack_record(sample_meta()))
        raw[20] ^= 0x01
        with self.assertRaises(m.MetaError):
            m.parse_record(bytes(raw))

    def test_erased_flash_rejected(self):
        with self.assertRaises(m.MetaError):
            m.parse_record(b"\xFF" * m.RECORD_SIZE)

    def test_zero_seq_rejected(self):
        with self.assertRaises(m.MetaError):
            m.pack_record(sample_meta(seq=0))

    def test_invalid_slot_state_rejected(self):
        with self.assertRaises(m.MetaError):
            m.pack_record(sample_meta(state_b=99))

    def test_select_active_picks_higher_seq(self):
        c0 = m.pack_record(sample_meta(seq=3))
        c1 = m.pack_record(sample_meta(seq=4, active_slot=1, state_b=m.SLOT_TRIAL))
        idx, active = m.select_active(c0, c1)
        self.assertEqual(idx, 1)
        self.assertEqual(active.seq, 4)
        self.assertEqual(m.next_write_copy(idx), 0)

        c0_newer = m.pack_record(sample_meta(seq=5, active_slot=1, state_b=m.SLOT_CONFIRMED))
        idx, active = m.select_active(c0_newer, c1)
        self.assertEqual(idx, 0)
        self.assertEqual(active.seq, 5)
        self.assertEqual(m.next_write_copy(idx), 1)

    def test_select_active_recovers_from_torn_write(self):
        good = m.pack_record(sample_meta(seq=10))
        torn = bytearray(m.pack_record(sample_meta(seq=11)))
        torn[40:] = b"\xFF" * (len(torn) - 40)  # simulated power cut mid-write
        idx, active = m.select_active(good, bytes(torn))
        self.assertEqual(idx, 0)
        self.assertEqual(active.seq, 10)

    def test_select_active_recovers_from_erased_copy(self):
        good = m.pack_record(sample_meta(seq=1))
        erased = b"\xFF" * m.RECORD_SIZE
        idx, active = m.select_active(erased, good)
        self.assertEqual(idx, 1)
        self.assertEqual(active.seq, 1)

    def test_select_active_both_blank_raises(self):
        with self.assertRaises(m.MetaError):
            m.select_active(b"\xFF" * m.RECORD_SIZE, b"\xFF" * m.RECORD_SIZE)

    def test_select_from_32k_region_dump(self):
        c0 = m.pack_record(sample_meta(seq=1))
        c1 = m.pack_record(sample_meta(seq=2, active_slot=1, state_b=m.SLOT_PENDING))
        region = (
            c0 + b"\xFF" * (m.SECTOR_SIZE - m.RECORD_SIZE)
            + c1 + b"\xFF" * (m.SECTOR_SIZE - m.RECORD_SIZE)
        )
        idx, active = m.select_from_region(region)
        self.assertEqual(idx, 1)
        self.assertEqual(active.seq, 2)
        self.assertEqual(active.slots[1].state, m.SLOT_PENDING)


if __name__ == "__main__":
    unittest.main()
