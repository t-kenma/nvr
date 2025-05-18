#ifndef __NVR_LOGGING_HPP__
#define __NVR_LOGGING_HPP__

#ifndef SPDLOG_ACTIVE_LEVEL
#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_INFO
#endif
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include <filesystem>
#include <thread>

#include "eeprom.hpp"


namespace nvr
{
	typedef struct
	{
		unsigned char Y;
		unsigned char M;
		unsigned char D;
		unsigned char h;
		unsigned char m;
		unsigned char s;
	} log_dt_t;
	

	class logger
	{
	public:
		logger( std::shared_ptr<nvr::eeprom> eeprom );
		~logger() = default;

		int LogOut( unsigned char code );
		int LogOut( unsigned char code, struct tm* _tm );

	private:
	std::shared_ptr<nvr::eeprom> eeprom_;
		unsigned short logout_address_;

		void MakeLogData( unsigned char code, log_dt_t dt, unsigned char* buf );
		void MakeLogData( unsigned char code, struct tm* _tm, unsigned char* buf );
	};
}
#endif