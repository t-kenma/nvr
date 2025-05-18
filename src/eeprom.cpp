#include "eeprom.hpp"

#include <filesystem>
#include <stdio.h>
#include <sys/file.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>

#define ADDR_SERIAL_NUMBER		0x0000		// 8 Byte
#define ADDR_FILE_SIZE			0x0011		// 8 Byte
#define ADDR_RECORD_CYCLE		0x0021		// 1 Byte
#define ADDR_RECORD_WATCH		0x0030		// 1 Byte
#define ADDR_LOG_OUT_POINTER	0x1000		// 
											// アドレス 2 Byte, ログデータ 0x1010～, 1ログ 16Byte, 最大500件

#define RECORDCYC_DEF			100
#define LOG_START_NUBER			1
#define LOG_MAX					500

namespace nvr
{
	/*------------------------------------------------------
	------------------------------------------------------*/
	static bool is_file_exists( const char *path ) noexcept
	{
		struct stat st;
		if( stat(path, &st) == 0 )
		{
			return S_ISREG(st.st_mode);
		}
		
		return false;
	}

	/*******************************************************
		locked_fp
	*******************************************************/
	/*------------------------------------------------------
	------------------------------------------------------*/
	FILE* locked_fp::open( const std::filesystem::path& path ) noexcept
	{
		const char* p = path.c_str();
		fp_ = fopen(p, "ae");
		if( fp_ == nullptr )
		{
			SPDLOG_ERROR("Failed to open {}: {}", p, strerror(errno));
			return nullptr;
		}

		fd_ = fileno(fp_);
		if( flock(fd_, LOCK_EX) )
		{
			SPDLOG_ERROR("Failed to lock {}: {}", p, strerror(errno));
			return nullptr; 
		}
		
		locked_ = true;
		return fp_;
	}

	/*------------------------------------------------------
	------------------------------------------------------*/
	locked_fp::~locked_fp()
	{
		if( locked_ )
		{
			if( flock(fd_, LOCK_UN) )
			{
				SPDLOG_WARN("Failed to unlock: {}", strerror(errno));
			}
		}

		if( fp_ )
		{
			fclose(fp_);
		}
	}

	/*******************************************************
		eeprom
	*******************************************************/
	/*------------------------------------------------------
	------------------------------------------------------*/
	eeprom::eeprom( const char* path )
		: path_( path ),
		  max_file_size_( 64 * 1024 )
	{
		memset( mem_, 0xFF, MEM_SIZE );
		
		path_ = path;
		
		unsigned char no[8];
		Read_SNo( &no[0] );
		Write( ADDR_SERIAL_NUMBER, 8, &no[0] );
		
		unsigned int rec_cyc;
		unsigned int com_rec_cyc;
		Read_RecordCyc( &rec_cyc, &com_rec_cyc );
		unsigned char redata = com_rec_cyc;
		Write( ADDR_RECORD_CYCLE, 1, &redata );
		
		unsigned char size;
		Read_FSize( &size );
		Write( ADDR_FILE_SIZE, 1, &size );
	}
	
	/*------------------------------------------------------
	------------------------------------------------------*/
	int eeprom::Load()
	{
		const char* p = path_.c_str();
		
		if( !is_file_exists( p ) )
		{
			return -1;
		}
		
		FILE *fp = fopen( p, "ae" );
		
		if( fp == NULL )
		{
			return -1;
		}
		
		fread( mem_, 1, MEM_SIZE, fp );
		
		fclose( fp );
		
		return 0;
	}
	
	/*------------------------------------------------------
	------------------------------------------------------*/
	int eeprom::Save()
	{
		int ret = 1;
		FILE *fp = nullptr;

		do
		{
			std::lock_guard<std::mutex> lock(mtx_);
			const char* path = path_.c_str();
			locked_fp lfp;

			fp = lfp.open( path_ );
			if( fp == nullptr )
			{
				ret = -1;
				break;
			}

			fwrite( mem_, 1, MEM_SIZE, fp );
			fflush( fp );

			ret = 0;
		
		} while(ret == 1);

		return ret;
	}
	
	/*------------------------------------------------------
	------------------------------------------------------*/
	int eeprom::Read( unsigned short addr, unsigned short len, unsigned char* data )
	{
		memcpy( data, &mem_[addr], len );
		
		return 0;
	}
	
	/*------------------------------------------------------
	------------------------------------------------------*/
	int eeprom::Write( unsigned short addr, unsigned short len, unsigned char* data )
	{
		memcpy( &mem_[addr], data, len );
		return Save();
	}
	
	
	/********************************************************************************/
	/* 関数名称	：	Read_RecordCyc													*/
	/* 概要		：	録画周期														*/
	/********************************************************************************/
	int eeprom::Read_RecordCyc( unsigned int* rec_cyc, unsigned int* com_rec_cyc )
	{
		unsigned char	redata;
		
		
		Read( ADDR_RECORD_CYCLE, 1, &redata );
		
		if( redata == 0xFF )
		{
			*com_rec_cyc = RECORDCYC_DEF;
			*rec_cyc = 1;
		}
		else if( redata <= 20 )
		{
			*com_rec_cyc = (unsigned int)(redata * 10);
			*com_rec_cyc = *com_rec_cyc >> 1;
			*rec_cyc = *com_rec_cyc;
			
			if( *com_rec_cyc < 40 )
			{
				*com_rec_cyc = RECORDCYC_DEF;
				*rec_cyc = 1;
			}
		}
		else
		{
			*com_rec_cyc = RECORDCYC_DEF;
			*rec_cyc = 1;
		}
		
		
//		cycl_dataset();
		
		return 0;
	}
	
	/********************************************************************************/
	/* 関数名称	：	Read_FSize														*/
	/* 概要		：	ファイルサイズ													*/
	/********************************************************************************/
	int eeprom::Read_FSize( unsigned char* size )
	{
		unsigned char	redata;
		
		
		Read( ADDR_FILE_SIZE, 1, &redata );
		
		if( (redata == 0x00) || (redata == 0xFF) )
		{
		 	*size = 64;
		}
		else
		{
			*size = redata;
		}
		
		return 0;
	}
	
	/********************************************************************************/
	/* 関数名称	：	Read_SNo														*/
	/* 概要		：	シリアルNo読み出し												*/
	/********************************************************************************/
	int eeprom::Read_SNo( unsigned char* no )
	{
		return Read( ADDR_SERIAL_NUMBER, 8, no );	
	}
	
	/********************************************************************************/
	/* 関数名称	：	Read_RecWatchCount_												*/
	/* 概要		：	録画監視エラー回数リード										*/
	/********************************************************************************/
	int eeprom::Read_RecWatchCount( unsigned char* count )
	{
		int				ret;
		unsigned char	redata;
		
		ret = Read( ADDR_RECORD_WATCH, 1, &redata );
		
		if( ret >= 0)
		{
			if( redata > 5 )
			{
				*count = 0;
			}
			else
			{
				*count = redata;
			}
		}
		
		return ret;
	}
	
	/********************************************************************************/
	/* 関数名称	：	Write_RecWatchCount												*/
	/* 概要		：																	*/
	/********************************************************************************/
	int eeprom::Write_RecWatchCount( unsigned char count )
	{
		return Write( ADDR_RECORD_WATCH, 1, &count );
	}
	
	
	
	
	/********************************************************************************/
	/* 関数名称	：	Read_Addres														*/
	/* 概要		：	書き込み済みアドレス											*/
	/********************************************************************************/
	int eeprom::Read_Address( unsigned short* addr )
	{
		int	ret;
		ret = Read( ADDR_LOG_OUT_POINTER, 2, (unsigned char*)&addr[0] );
		if( ret < 0 )
		{
			return -1;
		}
		
		if( *addr == 0xFFFF )
		{
			*addr = LOG_START_NUBER;
		}
		else
		{
			*addr++;
		}
		
		if( *addr > LOG_MAX )
		{
			*addr = LOG_START_NUBER;
		}
		
		return Write( ADDR_LOG_OUT_POINTER, 2, (unsigned char*)&addr[0] );
	}
	
	/********************************************************************************/
	/* 関数名称	：	Read_LogData													*/
	/* 概要		：																	*/
	/********************************************************************************/
	int eeprom::Read_LogData( unsigned short addr, unsigned char* data )
	{
		unsigned short	address;
		address = ADDR_LOG_OUT_POINTER + (addr * 16);
		
		// コード 年 月 日 時 分 秒	
		return Read( address, 14, data );
	}
	
	/********************************************************************************/
	/* 関数名称	：	Write_LogData													*/
	/* 概要		：																	*/
	/********************************************************************************/
	int eeprom::Write_LogData( unsigned short addr, unsigned char* data )
	{
		unsigned short	address;
		address = ADDR_LOG_OUT_POINTER + (addr * 16);
		
		// コード 年 月 日 時 分 秒	
		return Write( address, 14, data );
	}
}