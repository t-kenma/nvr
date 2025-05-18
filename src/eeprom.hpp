#ifndef __NVR_EEPROM_HPP__
#define __NVR_EEPROM_HPP__

#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include <filesystem>
#include <thread>
#include <mutex>

#define MEM_SIZE		(64 * 1024)

namespace nvr
{
	class eeprom
	{
	public:
		eeprom( const char* path );
		~eeprom() = default;
		
		int Load();
		int Save();
		
		int Read_RecordCyc( unsigned int* rec_cyc, unsigned int* com_rec_cyc );
		int Read_FSize( unsigned char* size );
		int Read_SNo( unsigned char* no );
		int Read_RecWatchCount( unsigned char* count );
		int Write_RecWatchCount( unsigned char count );
		
		int Read_Address( unsigned short* addr );
		int Read_LogData( unsigned short addr, unsigned char* data );
		int Write_LogData( unsigned short addr, unsigned char* data );
		
		int Read( unsigned short addr, unsigned short len, unsigned char* data );
		int Write( unsigned short addr, unsigned short len, unsigned char* data );
	
	private:
		std::filesystem::path path_;
        std::mutex mtx_;
        off_t max_file_size_;
		
		unsigned char mem_[ MEM_SIZE ];
		
	};

    class locked_fp
    {
    public:
        locked_fp(): fp_(nullptr), fd_(-1), locked_(false) {}
        ~locked_fp();
        locked_fp(const locked_fp&) = delete;
        locked_fp(locked_fp&&) = delete;

        FILE* open(const std::filesystem::path& path) noexcept;

        constexpr int fd() { return fd_; }
    private:
        FILE* fp_;
        int fd_;
        bool locked_;
    };
}
#endif