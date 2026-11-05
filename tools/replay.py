#!/usr/bin/env python3
"""Validate and deterministically replay Tactile P1 JSONL captures."""

from __future__ import annotations

import argparse
import sys
import time
from pathlib import Path

from tactile_stream import (
    ClutchInterpreter,
    RecordError,
    StreamStats,
    encode_record,
    iter_records,
)


def arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("capture", type=Path)
    parser.add_argument("--transitions", type=Path, help="write derived transition JSONL")
    parser.add_argument("--realtime", action="store_true", help="pace replay by monotonic timestamps")
    return parser.parse_args()


def main() -> int:
    args = arguments()
    stats = StreamStats()
    interpreter = ClutchInterpreter()
    previous_timestamp = None
    transition_file = None
    if args.transitions:
        args.transitions.parent.mkdir(parents=True, exist_ok=True)
        transition_file = args.transitions.open("w", encoding="utf-8", newline="\n")
    try:
        with args.capture.open(encoding="utf-8") as source:
            for _, record in iter_records(source):
                if args.realtime and previous_timestamp is not None:
                    delay = (record["monotonic_us"] - previous_timestamp) / 1_000_000
                    if delay > 0:
                        time.sleep(delay)
                previous_timestamp = record["monotonic_us"]
                stats.add(record)
                transition = interpreter.consume(record)
                if transition:
                    encoded = encode_record(transition)
                    if transition_file:
                        transition_file.write(encoded + "\n")
                    else:
                        print(encoded)
    except (OSError, RecordError) as error:
        print(error, file=sys.stderr)
        return 1
    finally:
        if transition_file:
            transition_file.close()
    print(
        f"replayed={stats.observations} markers={stats.markers} "
        f"rate_hz={stats.rate_hz:.2f} device_dropped={stats.device_dropped} "
        f"transport_gaps={stats.transport_gaps}",
        file=sys.stderr,
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
