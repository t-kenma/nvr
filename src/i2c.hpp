#ifndef __NVR_I2C_HPP__
#define __NVR_I2C_HPP__


#include <string>
#include <cstdint>
#include <spdlog/spdlog.h>

namespace nvr
{
    class I2CDevice {
    public:
        I2CDevice(int bus, int address);
        ~I2CDevice();

        void writeRegister(uint8_t reg, uint8_t value);
        uint8_t readRegister(uint8_t reg);

    private:
        int bus_;
        int address_;
        int file_;
    };
}
#endif
