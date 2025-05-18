#ifndef __NVR_COMMON_HPP__
#define __NVR_COMMON_HPP__

#include "common.hpp"

#include <time.h>

namespace nvr
{
	extern void ByteToCC( unsigned char val, unsigned char* dat );
	extern unsigned char CCToByte( unsigned char* cc );
	
	extern void BcdToCC( unsigned char bcd, unsigned char* dat );
	
	extern unsigned char BcdToByte( unsigned char bcd );
	extern unsigned char ByteToBcd( unsigned char val );
	extern struct tm* GetNowDt();

	
	
	
	
	
	
}
#endif
