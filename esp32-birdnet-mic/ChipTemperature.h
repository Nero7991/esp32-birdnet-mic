#pragma once
#include "soc/soc_caps.h"
#include "TemperatureReader.h"

#if SOC_TEMP_SENSOR_SUPPORTED
#include "driver/temperature_sensor.h"

class ChipTemperatureDriver {
public:
    bool select(int minimum, int maximum) {
        if (sensor_) {
            temperature_sensor_disable(sensor_);
            temperature_sensor_uninstall(sensor_);
            sensor_ = nullptr;
        }
        temperature_sensor_config_t config = TEMPERATURE_SENSOR_CONFIG_DEFAULT(minimum, maximum);
        if (temperature_sensor_install(&config, &sensor_) != ESP_OK) return false;
        if (temperature_sensor_enable(sensor_) == ESP_OK) return true;
        temperature_sensor_uninstall(sensor_);
        sensor_ = nullptr;
        return false;
    }
    bool read(float& value) {
        return sensor_ && temperature_sensor_get_celsius(sensor_, &value) == ESP_OK;
    }
private:
    temperature_sensor_handle_t sensor_ = nullptr;
};

inline float readChipTemperature() {
    static ChipTemperatureDriver driver;
    static TemperatureReader<ChipTemperatureDriver> reader(driver);
    return reader.read();
}
#else
inline float readChipTemperature() { return temperatureRead(); }
#endif
