#include "sensors.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

#include "driver/i2c_master.h"
#include "esp_log.h"
#include "esp_rom_sys.h"

#include "bmi270.h"
#include "bmm150.h"
#include "board.h"

#define GRAVITY_EARTH_MPS2 9.80665f
#define DEGREES_TO_RADIANS 0.01745329251994329577f
#define BMI270_I2C_SPEED_HZ 400000
#define BMI270_TRANSFER_TIMEOUT_MS 150
#define BMI270_MAX_WRITE_BYTES 64

static const char *TAG = "sensors";
static i2c_master_bus_handle_t internal_bus;
static i2c_master_dev_handle_t bmi_device;
static struct bmi2_dev bmi;
static struct bmm150_dev bmm;
static bool bmi_ready;
static bool bmm_ready;
static int8_t last_result;

static int8_t bmi_i2c_read(uint8_t reg_addr, uint8_t *data, uint32_t length,
                           void *context) {
    (void)context;
    esp_err_t err = i2c_master_transmit_receive(
        bmi_device, &reg_addr, 1, data, length, BMI270_TRANSFER_TIMEOUT_MS);
    return err == ESP_OK ? BMI2_INTF_RET_SUCCESS : (int8_t)-1;
}

static int8_t bmi_i2c_write(uint8_t reg_addr, const uint8_t *data,
                            uint32_t length, void *context) {
    (void)context;
    if (length > BMI270_MAX_WRITE_BYTES) {
        return (int8_t)-1;
    }
    uint8_t transfer[BMI270_MAX_WRITE_BYTES + 1];
    transfer[0] = reg_addr;
    memcpy(transfer + 1, data, length);
    esp_err_t err = i2c_master_transmit(
        bmi_device, transfer, length + 1, BMI270_TRANSFER_TIMEOUT_MS);
    return err == ESP_OK ? BMI2_INTF_RET_SUCCESS : (int8_t)-1;
}

static void sensor_delay_us(uint32_t period, void *context) {
    (void)context;
    esp_rom_delay_us(period);
}

static int8_t aux_read(uint8_t reg_addr, uint8_t *data, uint32_t length,
                       void *context) {
    return bmi2_read_aux_man_mode(reg_addr, data, (uint16_t)length,
                                 (struct bmi2_dev *)context);
}

static int8_t aux_write(uint8_t reg_addr, const uint8_t *data, uint32_t length,
                        void *context) {
    return bmi2_write_aux_man_mode(reg_addr, data, (uint16_t)length,
                                  (struct bmi2_dev *)context);
}

static void configure_bmm_interface(void) {
    memset(&bmm, 0, sizeof(bmm));
    bmm.intf = BMM150_I2C_INTF;
    bmm.intf_ptr = &bmi;
    bmm.read = aux_read;
    bmm.write = aux_write;
    bmm.delay_us = sensor_delay_us;
}

static int8_t configure_accel_gyro(void) {
    struct bmi2_sens_config config[2] = {
        {.type = BMI2_ACCEL},
        {.type = BMI2_GYRO},
    };
    int8_t result = bmi2_get_sensor_config(config, 2, &bmi);
    if (result != BMI2_OK) {
        return result;
    }

    config[0].cfg.acc.filter_perf = BMI2_PERF_OPT_MODE;
    config[0].cfg.acc.bwp = BMI2_ACC_OSR2_AVG2;
    config[0].cfg.acc.odr = BMI2_ACC_ODR_100HZ;
    config[0].cfg.acc.range = BMI2_ACC_RANGE_2G;

    config[1].cfg.gyr.filter_perf = BMI2_PERF_OPT_MODE;
    config[1].cfg.gyr.noise_perf = BMI2_POWER_OPT_MODE;
    config[1].cfg.gyr.bwp = BMI2_GYR_OSR2_MODE;
    config[1].cfg.gyr.odr = BMI2_GYR_ODR_100HZ;
    config[1].cfg.gyr.range = BMI2_GYR_RANGE_2000;
    config[1].cfg.gyr.ois_range = BMI2_GYR_OIS_2000;

    return bmi2_set_sensor_config(config, 2, &bmi);
}

static int8_t configure_bmm150(void) {
    struct bmi2_sens_config aux = {
        .type = BMI2_AUX,
    };
    aux.cfg.aux.odr = BMI2_AUX_ODR_100HZ;
    aux.cfg.aux.aux_en = BMI2_ENABLE;
    aux.cfg.aux.i2c_device_addr = BMM150_DEFAULT_I2C_ADDRESS;
    aux.cfg.aux.fcu_write_en = BMI2_ENABLE;
    aux.cfg.aux.manual_en = BMI2_ENABLE;
    aux.cfg.aux.aux_rd_burst = BMI2_AUX_READ_LEN_3;
    aux.cfg.aux.man_rd_burst = BMI2_AUX_READ_LEN_3;
    aux.cfg.aux.read_addr = BMM150_REG_DATA_X_LSB;

    int8_t result = bmi2_set_sensor_config(&aux, 1, &bmi);
    if (result != BMI2_OK) {
        return result;
    }

    result = bmm150_init(&bmm);
    if (result != BMM150_OK) {
        return result;
    }

    struct bmm150_settings settings = {
        .pwr_mode = BMM150_POWERMODE_FORCED,
        .preset_mode = BMM150_PRESETMODE_REGULAR,
    };
    result = bmm150_set_presetmode(&settings, &bmm);
    if (result != BMM150_OK) {
        return result;
    }
    result = bmm150_set_op_mode(&settings, &bmm);
    if (result != BMM150_OK) {
        return result;
    }

    aux.cfg.aux.manual_en = BMI2_DISABLE;
    return bmi2_set_sensor_config(&aux, 1, &bmi);
}

bool tactile_sensors_init(void) {
    i2c_master_bus_config_t bus_config = {
        .i2c_port = TACTILE_INTERNAL_I2C_PORT,
        .sda_io_num = TACTILE_SYS_SDA,
        .scl_io_num = TACTILE_SYS_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    esp_err_t err = i2c_new_master_bus(&bus_config, &internal_bus);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Internal I2C init failed: %s", esp_err_to_name(err));
        return false;
    }

    i2c_device_config_t device_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = TACTILE_BMI270_ADDR,
        .scl_speed_hz = BMI270_I2C_SPEED_HZ,
    };
    err = i2c_master_bus_add_device(internal_bus, &device_config, &bmi_device);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "BMI270 attach failed: %s", esp_err_to_name(err));
        return false;
    }

    memset(&bmi, 0, sizeof(bmi));
    bmi.intf = BMI2_I2C_INTF;
    bmi.intf_ptr = bmi_device;
    bmi.read = bmi_i2c_read;
    bmi.write = bmi_i2c_write;
    bmi.delay_us = sensor_delay_us;
    bmi.read_write_len = 32;
    configure_bmm_interface();

    last_result = bmi270_init(&bmi);
    if (last_result != BMI2_OK) {
        ESP_LOGE(TAG, "BMI270 initialization failed: %d", last_result);
        return false;
    }
    ESP_LOGI(TAG, "BMI270 initialized: chip_id=0x%02x", bmi.chip_id);

    uint8_t pullup = BMI2_ASDA_PUPSEL_10K;
    last_result = bmi2_set_regs(BMI2_AUX_IF_TRIM, &pullup, 1, &bmi);
    if (last_result != BMI2_OK) {
        ESP_LOGW(TAG, "BMI270 auxiliary pull-up setup failed: %d", last_result);
    }

    last_result = configure_accel_gyro();
    if (last_result != BMI2_OK) {
        ESP_LOGE(TAG, "Accel/gyro configuration failed: %d", last_result);
        return false;
    }

    bmm_ready = configure_bmm150() == BMI2_OK &&
                bmm.chip_id == BMM150_CHIP_ID;
    if (bmm_ready) {
        ESP_LOGI(TAG, "BMM150 initialized through BMI270 auxiliary bus");
    } else {
        ESP_LOGW(TAG, "BMM150 unavailable; continuing with 6-axis data");
    }

    uint8_t sensors[3] = {BMI2_ACCEL, BMI2_GYRO, BMI2_AUX};
    last_result = bmi2_sensor_enable(sensors, bmm_ready ? 3 : 2, &bmi);
    if (last_result != BMI2_OK) {
        ESP_LOGE(TAG, "Sensor enable failed: %d", last_result);
        return false;
    }

    bmi_ready = true;
    return true;
}

bool tactile_sensors_sample(tactile_sensor_sample_t *sample) {
    if (!bmi_ready || sample == NULL) {
        return false;
    }
    struct bmi2_sens_data data;
    memset(&data, 0, sizeof(data));
    last_result = bmi2_get_sensor_data(&data, &bmi);
    if (last_result != BMI2_OK ||
        (data.status & (BMI2_DRDY_ACC | BMI2_DRDY_GYR)) !=
            (BMI2_DRDY_ACC | BMI2_DRDY_GYR)) {
        return false;
    }

    const float acceleration_scale =
        GRAVITY_EARTH_MPS2 * 2.0f / 32768.0f;
    const float gyro_scale =
        2000.0f / 32768.0f * DEGREES_TO_RADIANS;
    sample->acceleration_mps2[0] = data.acc.x * acceleration_scale;
    sample->acceleration_mps2[1] = data.acc.y * acceleration_scale;
    sample->acceleration_mps2[2] = data.acc.z * acceleration_scale;
    sample->angular_velocity_rps[0] = data.gyr.x * gyro_scale;
    sample->angular_velocity_rps[1] = data.gyr.y * gyro_scale;
    sample->angular_velocity_rps[2] = data.gyr.z * gyro_scale;
    sample->quality_flags =
        TACTILE_QUALITY_ACCEL_VALID | TACTILE_QUALITY_GYRO_VALID;

    if (bmm_ready && (data.status & BMI2_DRDY_AUX)) {
        struct bmm150_mag_data magnetic;
        int8_t result = bmm150_aux_mag_data(data.aux_data, &magnetic, &bmm);
        if (result == BMM150_OK) {
            sample->magnetic_field_ut[0] = magnetic.x;
            sample->magnetic_field_ut[1] = magnetic.y;
            sample->magnetic_field_ut[2] = magnetic.z;
            sample->quality_flags |= TACTILE_QUALITY_MAG_VALID;
        }
    }
    return true;
}

bool tactile_sensors_bmi270_ready(void) {
    return bmi_ready;
}

bool tactile_sensors_bmm150_ready(void) {
    return bmm_ready;
}

int8_t tactile_sensors_last_result(void) {
    return last_result;
}
