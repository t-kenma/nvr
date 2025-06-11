#include "logging.hpp"

#include <filesystem>
#include <sys/file.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>

#include "common.hpp"

namespace nvr
{
	/*------------------------------------------------------
	------------------------------------------------------*/
	logger::logger( std::shared_ptr<nvr::eeprom> eeprom )
		 : eeprom_( eeprom )
	{
	}
	
	/*------------------------------------------------------
	------------------------------------------------------*/
	void logger::MakeLogData( unsigned char code, log_dt_t dt, unsigned char* buf )
	{
		::nvr::ByteToCC( code, &buf[ 0] );
		::nvr::BcdToCC(  dt.Y, &buf[ 2] );
		::nvr::BcdToCC(  dt.M, &buf[ 4] );
		::nvr::BcdToCC(  dt.D, &buf[ 6] );
		::nvr::BcdToCC(  dt.h, &buf[ 8] );
		::nvr::BcdToCC(  dt.m, &buf[10] );
		::nvr::BcdToCC(  dt.s, &buf[12] );
	}
	
	/*------------------------------------------------------
	------------------------------------------------------*/
	void logger::MakeLogData( unsigned char code, struct tm* _tm, unsigned char* buf )
	{
		unsigned char Y = _tm->tm_year % 100;	// 年 [1900からの経過年数] : (Y + 1900) % 100 = Y
		unsigned char M = _tm->tm_mon + 1;		// 月 [0-11] 0から始まることに注意
		unsigned char D = _tm->tm_mday;			// 日 [1-31]
		unsigned char h = _tm->tm_hour;			// 時 [0-23]
		unsigned char m = _tm->tm_min;			// 分 [0-59]
		unsigned char s = _tm->tm_sec;			// 秒 [0-61] 最大2秒までのうるう秒を考慮
		
		::nvr::ByteToCC( code, &buf[ 0] );
		::nvr::ByteToCC(    Y, &buf[ 2] );
		::nvr::ByteToCC(    M, &buf[ 4] );
		::nvr::ByteToCC(    D, &buf[ 6] );
		::nvr::ByteToCC(    h, &buf[ 8] );
		::nvr::ByteToCC(    m, &buf[10] );
		::nvr::ByteToCC(    s, &buf[12] );
	}
	
	/*------------------------------------------------------
	------------------------------------------------------*/
	int logger::LogOut( unsigned char code )
	{
		time_t tme = time( NULL );
		struct tm* _tm = localtime( &tme ); 
		
		return LogOut( code, _tm );
	}
	
	/*------------------------------------------------------
	------------------------------------------------------*/
	int logger::LogOut( unsigned char code, unsigned char* buf )
	{
		int ret;
		ret = eeprom_->Read_Address( &logout_address_ );
		if( ret < 0 )
		{
			return -1;
		}
		
		unsigned char data[14];
		::nvr::ByteToCC( code, &data[ 0] );
		memcpy(&data[2], buf, 12);
		
		
//		printf( "WRITE LOG ADDR=%d\n", logout_address_ );
//		printf( "WRITE LOG DATA=" );
//		for( int i=0; i< 14; i++ )
//		{
//			printf( "%02X ", data[i] );
//		}
//		printf( "\n" );

		ret = eeprom_->Write_LogData( logout_address_, data );
		if( ret == -1 )
		{
			if( code >= 18 )
			{
				return -1;
			}
			
			ret = eeprom_->Read_Address( &logout_address_ );
			if( ret < 0 )
			{
				return -1;
			}
			
			time_t tme = time( NULL );
			struct tm* _tm = localtime( &tme ); 
			MakeLogData( 19, _tm, data );	
			eeprom_->Write_LogData( logout_address_, data );
			
		}
		
		return 0;
	}
	
	
	/*------------------------------------------------------
	------------------------------------------------------*/
	int logger::LogOut( unsigned char code, struct tm* _tm )
	{
		int ret;
		ret = eeprom_->Read_Address( &logout_address_ );
		if( ret < 0 )
		{
			return -1;
		}
		
		unsigned char data[14];
		MakeLogData( code, _tm, data );
		
//		printf( "WRITE LOG ADDR=%d\n", logout_address_ );
//		printf( "WRITE LOG DATA=" );
//		for( int i=0; i< 14; i++ )
//		{
//			printf( "%02X ", data[i] );
//		}
//		printf( "\n" );

		ret = eeprom_->Write_LogData( logout_address_, data );
		if( ret == -1 )
		{
			if( code >= 18 )
			{
				return -1;
			}
			
			ret = eeprom_->Read_Address( &logout_address_ );
			if( ret < 0 )
			{
				return -1;
			}
			
			MakeLogData( 19, _tm, data );	
			eeprom_->Write_LogData( logout_address_, data );
			
		}
		
		return 0;
	}
}
