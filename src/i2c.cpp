#include "i2c.hpp"
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>
#include <stdexcept>

namespace nvr {
	I2CDevice::I2CDevice(int bus, int address) : bus_(bus), address_(address) {
		std::string filename = "/dev/i2c-" + std::to_string(bus_);
		file_ = open(filename.c_str(), O_RDWR);
		if (file_ < 0) {
			SPDLOG_INFO("Failed to open I2C bus: {}", filename);
		}

		if (ioctl(file_, I2C_SLAVE, address_) < 0) {
			SPDLOG_ERROR("Failed to set I2C slave address: 0x{:02X}, Error: {}", address_, strerror(errno));
		    close(file_);
		}
	}

	I2CDevice::~I2CDevice() {
		if (file_ >= 0) {
		    close(file_);
		}
	}

	void I2CDevice::writeRegister(uint8_t reg, uint8_t value) {
		uint8_t buf[2] = { reg, value };
		SPDLOG_INFO("Writing to I2C - Reg: 0x{:02X}, Value: 0x{:02X}", reg, value);
		if (::write(file_, buf, 2) != 2) {
		     SPDLOG_INFO("Failed to write to I2C device at Reg: 0x{:02X}", reg);
		}
	}

	uint8_t I2CDevice::readRegister(uint8_t reg) {
		SPDLOG_INFO("Reading from I2C - Reg: 0x{:02X}", reg);
		if (::write(file_, &reg, 1) != 1) {
			SPDLOG_INFO("Failed to write register address for reading: 0x{:02X}", reg);
		    throw std::runtime_error("Failed to write register address for reading");
		}

		uint8_t value;
		if (::read(file_, &value, 1) != 1) {
			SPDLOG_INFO("Failed to read from I2C register: 0x{:02X}", reg);
		}
		
		SPDLOG_INFO("Read value: 0x{:02X} from Reg: 0x{:02X}", value, reg);
		return value;
	}
}
