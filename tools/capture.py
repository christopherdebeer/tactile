#!/usr/bin/env python3
"""Capture unchanged Tactile P1 JSON records from an AtomS3R."""

from __future__ import annotations

import argparse
import sys
import time
from pathlib import Path

from tactile_stream import (
    ClutchInterpreter,
    RecordError,
    StreamStats,
    decode_record,
    encode_record,
)


def arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", required=True, help="USB serial port")
    parser.add_argument("--output", required=True, type=Path, help="raw JSONL output")
    parser.add_argument("--duration", type=float, default=0, help="seconds; 0 runs until Ctrl-C")
    parser.add_argument(
        "--transitions", type=Path, help="optional derived clutch-transition JSONL"
    )
    parser.add_argument(
        "--marker",
        action="append",
        default=[],
        help="emit a start marker; may be supplied more than once",
    )
    parser.add_argument("--baud", type=int, default=115200)
    return parser.parse_args()


def main() -> int:
    args = arguments()
    try:
        import serial
    except ImportError:
        print("pyserial is required: python3 -m pip install -r tools/requirements.txt", file=sys.stderr)
        return 2

    stats = StreamStats()
    interpreter = ClutchInterpreter()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    transition_file = None

    try:
        with serial.Serial(args.port, args.baud, timeout=0.25) as device:
            time.sleep(0.25)
            device.reset_input_buffer()
            with args.output.open("w", encoding="utf-8", newline="\n") as raw_file:
                if args.transitions:
                    args.transitions.parent.mkdir(parents=True, exist_ok=True)
                    transition_file = args.transitions.open(
                        "w", encoding="utf-8", newline="\n"
                    )
                device.write(b"stream on\n")
                for marker in args.marker:
                    device.write(f"mark {marker}\n".encode("ascii"))
                device.flush()
                started = time.monotonic()
                try:
                    while not args.duration or time.monotonic() - started < args.duration:
                        wire = device.readline()
                        start = wire.find(b"{")
                        if start < 0:
                            continue
                        try:
                            raw = wire[start:].rstrip(b"\r\n").decode("utf-8")
                            record = decode_record(raw)
                        except (UnicodeDecodeError, RecordError) as error:
                            print(f"ignored malformed serial record: {error}", file=sys.stderr)
                            continue
                        raw_file.write(raw + "\n")
                        raw_file.flush()
                        stats.add(record)
                        transition = interpreter.consume(record)
                        if transition_file and transition:
                            transition_file.write(encode_record(transition) + "\n")
                            transition_file.flush()
                finally:
                    device.write(b"stream off\n")
                    device.flush()
    except KeyboardInterrupt:
        pass
    finally:
        if transition_file:
            transition_file.close()

    print(
        f"captured={stats.observations} markers={stats.markers} "
        f"rate_hz={stats.rate_hz:.2f} device_dropped={stats.device_dropped} "
        f"transport_gaps={stats.transport_gaps}",
        file=sys.stderr,
    )
    return 0 if stats.observations else 1


if __name__ == "__main__":
    raise SystemExit(main())
