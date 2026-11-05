"""Validation, accounting, and deterministic P1 stream interpretation."""

from __future__ import annotations

import json
from dataclasses import dataclass
from typing import Any, Iterable, Iterator


class RecordError(ValueError):
    """Raised when a telemetry record does not satisfy schema version 1."""


def decode_record(line: str) -> dict[str, Any]:
    try:
        record = json.loads(line)
    except json.JSONDecodeError as error:
        raise RecordError(str(error)) from error
    validate_record(record)
    return record


def validate_record(record: Any) -> None:
    if not isinstance(record, dict):
        raise RecordError("record must be a JSON object")
    if record.get("schema_version") != 1:
        raise RecordError("unsupported schema_version")
    record_type = record.get("type")
    if record_type not in {"observation", "marker"}:
        raise RecordError(f"unsupported record type: {record_type!r}")
    for key in ("device_id", "role", "firmware_version", "sequence", "monotonic_us"):
        if key not in record:
            raise RecordError(f"missing field: {key}")
    if not isinstance(record["sequence"], int) or record["sequence"] < 0:
        raise RecordError("sequence must be a non-negative integer")
    if not isinstance(record["monotonic_us"], int) or record["monotonic_us"] < 0:
        raise RecordError("monotonic_us must be a non-negative integer")
    if record_type == "marker":
        for key in ("provenance", "label"):
            if not isinstance(record.get(key), str) or not record[key]:
                raise RecordError(f"marker {key} must be a non-empty string")
        return
    for key in ("acceleration_mps2", "angular_velocity_rps"):
        vector = record.get(key)
        if not isinstance(vector, list) or len(vector) != 3:
            raise RecordError(f"{key} must contain three values")
        if not all(isinstance(value, (int, float)) for value in vector):
            raise RecordError(f"{key} values must be numeric")
    magnetic = record.get("magnetic_field_ut")
    if magnetic is not None and (
        not isinstance(magnetic, list)
        or len(magnetic) != 3
        or not all(isinstance(value, (int, float)) for value in magnetic)
    ):
        raise RecordError("magnetic_field_ut must be null or contain three values")
    if not isinstance(record.get("button"), bool):
        raise RecordError("button must be boolean")
    if not isinstance(record.get("dropped"), int) or record["dropped"] < 0:
        raise RecordError("dropped must be a non-negative integer")
    if not isinstance(record.get("quality_flags"), int):
        raise RecordError("quality_flags must be an integer")


def encode_record(record: dict[str, Any]) -> str:
    return json.dumps(record, separators=(",", ":"), sort_keys=True)


@dataclass
class StreamStats:
    observations: int = 0
    markers: int = 0
    device_dropped: int = 0
    transport_gaps: int = 0
    first_timestamp_us: int | None = None
    last_timestamp_us: int | None = None
    last_sequence: int | None = None

    def add(self, record: dict[str, Any]) -> None:
        if record["type"] == "marker":
            self.markers += 1
            return
        sequence = record["sequence"]
        timestamp = record["monotonic_us"]
        if self.last_sequence is not None and sequence > self.last_sequence + 1:
            self.transport_gaps += sequence - self.last_sequence - 1
        if self.last_timestamp_us is not None and timestamp <= self.last_timestamp_us:
            raise RecordError("observation timestamps must increase")
        self.observations += 1
        self.device_dropped += record["dropped"]
        self.first_timestamp_us = (
            timestamp if self.first_timestamp_us is None else self.first_timestamp_us
        )
        self.last_timestamp_us = timestamp
        self.last_sequence = sequence

    @property
    def rate_hz(self) -> float:
        if (
            self.observations < 2
            or self.first_timestamp_us is None
            or self.last_timestamp_us is None
            or self.last_timestamp_us == self.first_timestamp_us
        ):
            return 0.0
        duration = (self.last_timestamp_us - self.first_timestamp_us) / 1_000_000
        return (self.observations - 1) / duration


class ClutchInterpreter:
    """Reference P1 interpreter: button state maps deterministically to clutch."""

    name = "button-clutch/1"

    def __init__(self) -> None:
        self._button = False

    def consume(self, record: dict[str, Any]) -> dict[str, Any] | None:
        if record["type"] != "observation":
            return None
        button = record["button"]
        if button == self._button:
            return None
        self._button = button
        return {
            "schema_version": 1,
            "type": "intention",
            "interpreter": self.name,
            "device_id": record["device_id"],
            "source_sequence": record["sequence"],
            "monotonic_us": record["monotonic_us"],
            "state": "clutched" if button else "idle",
            "event": "clutch_down" if button else "clutch_up",
        }


def iter_records(lines: Iterable[str]) -> Iterator[tuple[str, dict[str, Any]]]:
    for line_number, line in enumerate(lines, 1):
        raw = line.rstrip("\r\n")
        if not raw:
            continue
        try:
            yield raw, decode_record(raw)
        except RecordError as error:
            raise RecordError(f"line {line_number}: {error}") from error
