# Telemetry envelope — target P1

P0 USB logs are **diagnostics**, not a stable telemetry protocol.

The P1 transport-neutral observation schema will carry:
- schema_version, device_id, firmware_version
- sequence number, monotonic timestamp in microseconds, dropped-frame indicator
- acceleration (m/s^2), angular velocity (rad/s), magnetic field (uT or unavailable)
- button state, raw capacitive electrode vector, pressure vector, quality flags
- optional event-marker records with provenance and labels

A web studio or local tool should record these observations unchanged, replay them deterministically, and adapt old SIMCAP/GAMBIT collector files for comparison. Derived intention events are a separate versioned stream.
