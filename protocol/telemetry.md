# P1 telemetry protocol

Firmware 0.2.0 and later emits UTF-8 newline-delimited JSON (JSONL) over native USB
serial at 115200 baud. Send `stream on` to begin and `stream off` to stop.
Diagnostic text may share the serial connection, so collectors accept only
lines containing a valid JSON object.

## Observation record

```json
{"schema_version":1,"type":"observation","device_id":"d34238","role":"pebble-01","firmware_version":"0.6.0","sequence":790,"monotonic_us":9090090,"dropped":0,"acceleration_mps2":[0.708684,8.432379,4.384382],"angular_velocity_rps":[-0.001065,0.002131,0.0],"magnetic_field_ut":[30.0,5.0,2.0],"reader":{"ready":true,"roll_rad":0.12,"pitch_rad":-0.03,"rate":1.3,"roll_active":true,"roll_armed":true,"tilt_latched":false,"focused":false,"tutorial_active":true,"tutorial_complete":false,"tutorial_stage":"reverse"},"button":false,"capacitance":[],"pressure":[],"quality_flags":7}
```

Fields:

- `schema_version`: integer protocol version; currently `1`.
- `type`: `observation`.
- `device_id`: stable lower 24 bits of the factory MAC, encoded as six hex
  characters.
- `role`: locally assigned device role.
- `firmware_version`: build version.
- `sequence`: unsigned sample-attempt sequence. Gaps indicate unavailable
  samples or missed scheduling periods.
- `monotonic_us`: microseconds since device boot from the ESP monotonic timer.
- `dropped`: sequence slots missing since the previous emitted observation.
- `acceleration_mps2`: x/y/z acceleration in metres per second squared.
- `angular_velocity_rps`: x/y/z angular velocity in radians per second.
- `magnetic_field_ut`: compensated x/y/z magnetic field in microtesla, or
  `null` when the BMM150 is unavailable.
- `reader`: optional embodied-reader state, present in firmware 0.6.0 and
  later:
  - `ready`: a stable reading pose has been captured.
  - `roll_rad`, `pitch_rad`: filtered screen-space angles relative to that
    pose.
  - `rate`: retained signed RSVP multiplier; negative values read backward
    and zero is paused.
  - `roll_active`, `roll_armed`: analog pace-control recognition state.
  - `tilt_latched`: a depth gesture fired and must return to neutral.
  - `focused`: the paused word completed its stillness dwell.
  - `tutorial_active`, `tutorial_complete`, `tutorial_stage`: first-use
    capability-gate state.
- `button`: current active-low screen-button state.
- `capacitance`, `pressure`: reserved raw vectors, empty until later phases.
- `quality_flags`: bit mask: `0x01` acceleration valid, `0x02` gyro valid,
  `0x04` magnetometer valid, `0x08` sample follows a missed deadline.

The nominal rate is 100 Hz. Consumers must use timestamps rather than assuming
perfect cadence.

## Marker record

The USB command `mark <label>` emits:

```json
{"schema_version":1,"type":"marker","device_id":"d34238","role":"pebble-01","firmware_version":"0.2.0","sequence":812,"monotonic_us":9315000,"provenance":"usb-console","label":"roll-left"}
```

Labels are 1-48 ASCII letters, digits, `.`, `_`, or `-`. A recorder stores
observation and marker JSON unchanged.

## Derived stream

Interpretation never mutates the raw capture. The P1 reference interpreter
maps button edges to separate `button-clutch/1` intention records. Running
`tools/replay.py` over the same JSONL must produce byte-identical transition
records to those produced during capture.

Schema additions remain optional within version 1. Removing fields, changing
units or changing field meaning requires a new `schema_version`.
