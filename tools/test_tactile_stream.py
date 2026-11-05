import unittest

from tactile_stream import (
    ClutchInterpreter,
    RecordError,
    StreamStats,
    decode_record,
    encode_record,
)


def observation(sequence: int, timestamp: int, button: bool = False) -> dict:
    return {
        "schema_version": 1,
        "type": "observation",
        "device_id": "d34238",
        "role": "pebble-01",
        "firmware_version": "0.2.0",
        "sequence": sequence,
        "monotonic_us": timestamp,
        "dropped": 0,
        "acceleration_mps2": [0.0, 0.0, 9.80665],
        "angular_velocity_rps": [0.0, 0.0, 0.0],
        "magnetic_field_ut": [20.0, 1.0, -40.0],
        "button": button,
        "capacitance": [],
        "pressure": [],
        "quality_flags": 7,
    }


class TelemetryTests(unittest.TestCase):
    def test_validation_and_gap_accounting(self) -> None:
        stats = StreamStats()
        stats.add(decode_record(encode_record(observation(10, 100_000))))
        stats.add(decode_record(encode_record(observation(12, 120_000))))
        self.assertEqual(stats.observations, 2)
        self.assertEqual(stats.transport_gaps, 1)
        self.assertAlmostEqual(stats.rate_hz, 50.0)

    def test_rejects_non_monotonic_time(self) -> None:
        stats = StreamStats()
        stats.add(observation(1, 100))
        with self.assertRaises(RecordError):
            stats.add(observation(2, 100))

    def test_clutch_replay_is_deterministic(self) -> None:
        records = [
            observation(1, 10_000, False),
            observation(2, 20_000, True),
            observation(3, 30_000, True),
            observation(4, 40_000, False),
        ]
        first = ClutchInterpreter()
        second = ClutchInterpreter()
        transitions_a = [
            encode_record(event)
            for record in records
            if (event := first.consume(record)) is not None
        ]
        transitions_b = [
            encode_record(event)
            for record in records
            if (event := second.consume(record)) is not None
        ]
        self.assertEqual(transitions_a, transitions_b)
        self.assertIn('"event":"clutch_down"', transitions_a[0])
        self.assertIn('"event":"clutch_up"', transitions_a[1])


if __name__ == "__main__":
    unittest.main()
