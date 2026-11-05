#pragma once

#include <stdbool.h>
#include <stdint.h>

enum {
    TACTILE_QUALITY_ACCEL_VALID = 1u << 0,
    TACTILE_QUALITY_GYRO_VALID = 1u << 1,
    TACTILE_QUALITY_MAG_VALID = 1u << 2,
    TACTILE_QUALITY_DEADLINE_MISSED = 1u << 3,
};

typedef struct {
    float acceleration_mps2[3];
    float angular_velocity_rps[3];
    float magnetic_field_ut[3];
    uint8_t quality_flags;
} tactile_sensor_sample_t;

bool tactile_sensors_init(void);
bool tactile_sensors_sample(tactile_sensor_sample_t *sample);
bool tactile_sensors_bmi270_ready(void);
bool tactile_sensors_bmm150_ready(void);
int8_t tactile_sensors_last_result(void);
