/***********************************************************
***********************************************************/
#include <cstdio>
#include <cstdlib>
#include <cerrno>
#include <atomic>
#include <getopt.h>
#include <filesystem>
#include <sys/epoll.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/signalfd.h>
#include <netinet/in.h>
#include <net/if.h>
#include <iostream>

#include <jpeglib.h>

#include "config.hpp"
#include "pipeline.hpp"

#include "gpio.hpp"
#include "sd_manager.hpp"
#include "led_manager.hpp"
#include "util.hpp"

#include "eeprom.hpp"
#include "logging.hpp"
#include "usb.hpp"

#include <stdexcept>

#include "ring_buffer.hpp"
#include <vector>
#include <string>
#include <fstream>


#ifdef G_OS_UNIX
#include <glib-unix.h>
#endif

#include "common.hpp"

/***********************************************************
***********************************************************/
struct callback_data_t
{
	callback_data_t() : timer1_id(0),
						bus_watch_id(0),
						led(nullptr),
						sd_manager(nullptr),
						jpeg_file(nullptr),
						pipeline(nullptr),
						jpeg_time(0),
						is_video_error(false),
						interrupted(false),
						main_loop(nullptr)
	{}
	
	guint timer1_id;
	guint bus_watch_id;
	
	std::shared_ptr<nvr::pipeline> pipeline;
	std::shared_ptr<nvr::logger> logger;	
	nvr::led_manager *led;
	nvr::sd_manager *sd_manager;
	nvr::gpio_in *cminsig;
	nvr::gpio_in *gpio_stop_btn;
	nvr::gpio_in *gpio_battery;
	nvr::gpio_in *pgood;
	nvr::gpio_out *pwd_decoder;
	std::shared_ptr<nvr::gpio_out> gpio_alrm_a;
	std::shared_ptr<nvr::gpio_out> gpio_alrm_b;
	std::filesystem::path done_dir;
	const char *jpeg_file;
	
	std::atomic<std::time_t> jpeg_time;
	bool is_video_error;
	std::atomic<bool> interrupted;
	int copy_interval;
	bool low_battry;
#ifdef NVR_DEBUG_POWER
	nvr::gpio_out *tmp_out1;
#endif
	GMainLoop *main_loop;
	
	bool* broken_test_ena;
	int* broken_test_cnt;
	
	std::shared_ptr<nvr::usb> usb;	
	std::shared_ptr<nvr::eeprom> eeprom;	
};

namespace fs = std::filesystem;
static const char *g_interrupt_name = "nrs-video-recorder-interrupted";
static const char *g_quit_name = "nrs-video-recorder-quit";



int count_btn_down = 0;
int count_rec_off = 0;
int count_sd_none = 0;

int count_video_lost = 0;
int count_video_found = 0;

int count_sd_access_err = 0;

bool system_error = false;
bool formatting = false;

bool broken_test_ena = false;
int broken_test_cnt = 0;

int  file_idx = 0;
int  file_max = 0;


std::shared_ptr<nvr::usb> _usb;

bool usb_sd_access = false;

unsigned char recchekflg = 0;
unsigned char chkexeflg = 0;
char last_save_path[2048];







/***********************************************************
***********************************************************/
unsigned char exif[380] =
{
	0xFF, 0xD8, 0xFF, 0xE1, 0x01, 0x78, 0x45, 0x78, 0x69, 0x66, 0x00, 0x00,
	0x49, 0x49,
	0x2A, 0x00,
	0x08, 0x00, 0x00, 0x00,
	
	0x05, 0x00,
	0x0F, 0x01, 0x02, 0x00, 0x14, 0x00, 0x00, 0x00, 0x4A, 0x00, 0x00, 0x00,		// メーカー（74-20）
	0x10, 0x01, 0x02, 0x00, 0x20, 0x00, 0x00, 0x00, 0x5E, 0x00, 0x00, 0x00,		// モデル（94-32）
	0x31, 0x01, 0x02, 0x00, 0x1B, 0x00, 0x00, 0x00, 0x7E, 0x00, 0x00, 0x00,		// ソフトウェア（126-27）
	0x32, 0x01, 0x02, 0x00, 0x15, 0x00, 0x00, 0x00, 0x99, 0x00, 0x00, 0x00,		// ファイル更新日時（153-21）
	0x69, 0x87, 0x04, 0x00, 0x01, 0x00, 0x00, 0x00, 0xAE, 0x00, 0x00, 0x00,		// Exif IFDへのポインタ （176-4）
	
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,	// データ部 : データなし
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,								// データ部 : データなし
	0x32, 0x30, 0x32, 0x35, 0x2F, 0x30, 0x34, 0x2F, 0x33, 0x30, 0x20, 0x31, 0x33, 0x3A, 0x32, 0x39, 0x3A, 0x34, 0x36, 0x00, 0x00,																	// データ部 : ファイル更新日時 「2025/04/30 13:29:46」
	0x00, 0x00, 0x00, 0x00,																																											// データ部 : データなし
	
	0x04, 0x00,
	0x00, 0x90, 0x07, 0x00, 0x04, 0x00, 0x00, 0x00, 0x30, 0x32, 0x31, 0x30,		// Exif バージョン （Ver2.1）
	0x03, 0x90, 0x02, 0x00, 0x14, 0x00, 0x00, 0x00, 0xE4, 0x00, 0x00, 0x00,		// 現画像データの生成日 228-20
	0x04, 0x90, 0x02, 0x00, 0x14, 0x00, 0x00, 0x00, 0xF8, 0x00, 0x00, 0x00,		// デジタルデータの生成日時 248-20
	0x86, 0x92, 0x02, 0x00, 0x64, 0x00, 0x00, 0x00, 0x0C, 0x01, 0x00, 0x00,		// ユーザーコメント 268-100
	
	0x00, 0x00, 0x00, 0x00,																										// データ部 : データなし
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,		// データ部 : 現画像データの生成日
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x19,		// データ部 : デジタルデータの生成日時
	
	//--- データ部 : ユーザーコメント
	0x64, 0x61, 0x74, 0x65, 0x20, 0x3A, 0x20, 0x32, 0x30, 0x32, 0x35, 0x2F, 0x30, 0x34, 0x2F, 0x33, 0x30, 0x20, 0x31, 0x33, 0x3A, 0x32, 0x39, 0x3A, 0x34, 0x36, 0x2E, 0x30, 0x00, 0x00,			// 「date : 2025/04/30 13:29:46.0」
	0x73, 0x65, 0x72, 0x69, 0x61, 0x6C, 0x20, 0x3A, 0x20, 0x48, 0x31, 0x32, 0x35, 0x34, 0x33, 0x20, 0x20, 0x00, 0x00, 0x00,																		// 「serial : H12543」
	0x63, 0x79, 0x63, 0x6C, 0x65, 0x20, 0x3A, 0x20, 0x30, 0x31, 0x30, 0x30, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 																	// 「ctcke : 0100」
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};


/***********************************************************
***********************************************************/
/*----------------------------------------------------------
----------------------------------------------------------*/
void compressYUYVtoJPEG( const std::vector<uint8_t>& input, const int width, const int height, int quality, std::vector<uint8_t>& output )
{
	struct jpeg_compress_struct cinfo;
	struct jpeg_error_mgr jerr;
	JSAMPROW row_ptr[1];
	int row_stride;

	uint8_t* outbuffer = NULL;
	uint64_t outlen = 0;

//SPDLOG_INFO("compressYUYVtoJPEG 1");

	cinfo.err = jpeg_std_error(&jerr);
	jpeg_create_compress(&cinfo);
	jpeg_mem_dest(&cinfo, &outbuffer, &outlen);

//SPDLOG_INFO("compressYUYVtoJPEG 2");
	
	// jrow is a libjpeg row of samples array of 1 row pointer
	cinfo.image_width = width & -1;
	cinfo.image_height = height & -1;
	cinfo.input_components = 3;
	cinfo.in_color_space = JCS_YCbCr; //libJPEG expects YUV 3bytes, 24bit

//SPDLOG_INFO("compressYUYVtoJPEG 3");

	jpeg_set_defaults(&cinfo);
	jpeg_set_quality(&cinfo, quality, TRUE);
	jpeg_start_compress(&cinfo, TRUE);

//SPDLOG_INFO("compressYUYVtoJPEG 4");

	std::vector<uint8_t> tmprowbuf(width * 3);

	JSAMPROW row_pointer[1];
	row_pointer[0] = &tmprowbuf[0];
	
//SPDLOG_INFO("compressYUYVtoJPEG 5");
	
	while (cinfo.next_scanline < cinfo.image_height) {
		unsigned i, j;
		unsigned offset = cinfo.next_scanline * cinfo.image_width * 2; //offset to the correct row
		for (i = 0, j = 0; i < cinfo.image_width * 2; i += 4, j += 6) { //input strides by 4 bytes, output strides by 6 (2 pixels)
#if 0
			tmprowbuf[j + 0] = input[offset + i + 0]; // Y (unique to this pixel)
			tmprowbuf[j + 1] = input[offset + i + 1]; // U (shared between pixels)
			tmprowbuf[j + 2] = input[offset + i + 3]; // V (shared between pixels)
			tmprowbuf[j + 3] = input[offset + i + 2]; // Y (unique to this pixel)
			tmprowbuf[j + 4] = input[offset + i + 1]; // U (shared between pixels)
			tmprowbuf[j + 5] = input[offset + i + 3]; // V (shared between pixels)
#else
			tmprowbuf[j + 0] = input[offset + i + 1]; // Y (unique to this pixel)
			tmprowbuf[j + 1] = input[offset + i + 0]; // U (shared between pixels)
			tmprowbuf[j + 2] = input[offset + i + 2]; // V (shared between pixels)
			tmprowbuf[j + 3] = input[offset + i + 3]; // Y (unique to this pixel)
			tmprowbuf[j + 4] = input[offset + i + 0]; // U (shared between pixels)
			tmprowbuf[j + 5] = input[offset + i + 2]; // V (shared between pixels)
#endif
		}
		jpeg_write_scanlines(&cinfo, row_pointer, 1);
	}
																				
//SPDLOG_INFO("compressYUYVtoJPEG 6");
	
	jpeg_finish_compress(&cinfo);
	jpeg_destroy_compress(&cinfo);

	std::cout << "libjpeg produced " << outlen << " bytes" << std::endl;

	output = std::vector<uint8_t>(outbuffer, outbuffer + outlen);
}


/*----------------------------------------------------------
----------------------------------------------------------*/
int _do_reboot() noexcept
{
	pid_t pid;
	int status;
	int rc;
	
	
	//---プロセスの複製
	//
	pid = fork();
	if (pid < 0)
	{
		SPDLOG_ERROR("Failed to fork process: {}", strerror(errno));
		return -1;
	} 
	else
	if(pid == 0) 
	{
		//---shutdownプロセス実行
		//
		execl("/sbin/shutdown", "/sbin/shutdown", "-r", "now", nullptr);
		SPDLOG_ERROR("Failed to exec reboot.");
		exit(-1);
	}
	
	
	//---shutdownプロセス完了までwait
	//
	waitpid(pid, &status, 0);
	
	if (!WIFEXITED(status)) {
		return -1;
	}
	
	rc = WEXITSTATUS(status);
	
	return rc;
}

/*----------------------------------------------------------
----------------------------------------------------------*/
int overwriteByteInJPEG( callback_data_t* data, const std::string& filePath )
{
	char fs_time[21];
	char fs_time2[23];
	std::time_t t = std::time(nullptr);
	const std::tm *tm = std::localtime(&t);


	int year = tm->tm_year +1900;
	int mon = tm->tm_mon + 1;
	int day = tm->tm_mday;
	int hou = tm->tm_hour;
	int min = tm->tm_min;
	int sec = tm->tm_sec;

	fs_time[ 0] = ((year / 1000) % 10) + 0x30;
	fs_time[ 1] = ((year / 100) % 10) + 0x30;
	fs_time[ 2] = ((year / 10) % 10) + 0x30;
	fs_time[ 3] = (year % 10) + 0x30;
	fs_time[ 4] = 0x2F;
	fs_time[ 5] = ((mon / 10) % 10) + 0x30;
	fs_time[ 6] = (mon % 10) + 0x30;
	fs_time[ 7] = 0x2F;
	fs_time[ 8] = ((day / 10) % 10) + 0x30;
	fs_time[ 9] = (day % 10) + 0x30;
	fs_time[10] = 0x20;
	fs_time[11] = ((hou / 10) % 10) + 0x30;
	fs_time[12] = (hou % 10) + 0x30;
	fs_time[13] = 0x3A;
	fs_time[14] = ((min / 10) % 10) + 0x30;
	fs_time[15] = (min % 10) + 0x30;
	fs_time[16] = 0x3A;
	fs_time[17] = ((sec / 10) % 10) + 0x30;
	fs_time[18] = (sec % 10) + 0x30;
	fs_time[19] = 0x00;
	fs_time[20] = 0x00;


	fs_time2[ 0] = ((year / 1000) % 10) + 0x30;
	fs_time2[ 1] = ((year / 100) % 10) + 0x30;
	fs_time2[ 2] = ((year / 10) % 10) + 0x30;
	fs_time2[ 3] = (year % 10) + 0x30;
	fs_time2[ 4] = 0x2F;
	fs_time2[ 5] = ((mon / 10) % 10) + 0x30;
	fs_time2[ 6] = (mon % 10) + 0x30;
	fs_time2[ 7] = 0x2F;
	fs_time2[ 8] = ((day / 10) % 10) + 0x30;
	fs_time2[ 9] = (day % 10) + 0x30;
	fs_time2[10] = 0x20;
	fs_time2[11] = ((hou / 10) % 10) + 0x30;
	fs_time2[12] = (hou % 10) + 0x30;
	fs_time2[13] = 0x3A;
	fs_time2[14] = ((min / 10) % 10) + 0x30;
	fs_time2[15] = (min % 10) + 0x30;
	fs_time2[16] = 0x3A;
	fs_time2[17] = ((sec / 10) % 10) + 0x30;
	fs_time2[18] = (sec % 10) + 0x30;
	fs_time2[19] = 0x2E;
	fs_time2[20] = 0x30;
	fs_time2[21] = 0x00;
	fs_time2[22] = 0x00;
	
	
	//---年月時分秒
	//
	memcpy( &exif[161], fs_time, sizeof(fs_time) );
	
	
	//---年月時分秒
	//
	memcpy( &exif[287], fs_time2, sizeof(fs_time2) );
	
	
	//---シリアル
	//
	char serial_no[8] = { '1','9','9','1','0','3','0','6' };
	data->eeprom->Read_SNo( (unsigned char*)&serial_no[0] );
	std::string serial = std::string( "serial : " ) + serial_no;
	memcpy( &exif[310], serial.c_str(), serial.size() ); 
	
	
	//---周期
	//
	unsigned int com_rec_cyc = 100;
	unsigned int rec_cyc;
	data->eeprom->Read_RecordCyc( &rec_cyc, &com_rec_cyc );
	char numStr[16];
	sprintf( numStr, "%04d", com_rec_cyc );
	std::string cycle = std::string( "cycle : " ) + numStr;
	memcpy( &exif[330], cycle.c_str(), cycle.size() );

	
	
	std::ifstream src( "/tmp/_video.jpg", std::ios::binary );
	if( !src )
	{
		std::cerr << "Failed to open source file\n";
		return 1;
	}

	src.seekg(0, std::ios::end);
	size_t fileSize = src.tellg();

	SPDLOG_INFO("src size = {}",fileSize);


#if 0
	// EXIFをスキップ
	const std::streampos pos = 20;
	src.seekg(pos);
	if (!src) {
		std::cerr << "Failed to seek in source file\n";
		return 1;
	}
	
	std::ofstream dst(filePath.c_str(), std::ios::binary);
	if (!dst) {
		std::cerr << "Failed to open destination file\n";
		return 1;
	}
	
	dst.write(reinterpret_cast<const char*>(exif), sizeof(exif));
	
	char buffer[4096];
	
	while (src.read(buffer, sizeof(buffer)) || src.gcount() > 0) {
		SPDLOG_INFO("dst write");
		dst.write(buffer, src.gcount());
	}
	
	
#else
	src.seekg(0);

	std::ofstream dst(filePath.c_str(), std::ios::binary);
	if( !dst )
	{
		std::cerr << "Failed to open destination file\n";
		return 1;
	}
	
	SPDLOG_INFO( "dst open()" );
	
	// copies all data into buffer
	std::vector<uint8_t> inbuf( (std::istreambuf_iterator<char>( src )), (std::istreambuf_iterator<char>()) );
	
	std::vector<uint8_t> output;
	int quality = 90;
	
RETRY:
	SPDLOG_INFO( "call compressYUYVtoJPEG() start" );
	try
	{
		compressYUYVtoJPEG( inbuf, 640, 480, quality, output );
	}
	catch(const std::exception& e)
	{
		std::cerr << e.what() << '\n';
	}
	SPDLOG_INFO( "call compressYUYVtoJPEG() end" );
	
	SPDLOG_INFO( "output.size() {}", output.size() );
	
	int img_offset = 20;
	int img_size = output.size() - img_offset;
	int output_size = img_size + sizeof( exif );
	
	unsigned char sz_kb;
	data->eeprom->Read_FSize( &sz_kb );
	
	
	if( output_size > (sz_kb * 1024) )
	{
		if( quality >= 90 )
		{
			quality = 50;
			goto RETRY;
		}
	} 
	else
	{
 		dst.write( reinterpret_cast<const char*>(exif), sizeof(exif) );
		dst.write( (const char*) &output[img_offset], img_size );
		
		int cnt = (sz_kb * 1024) - output_size;
		char dmy = 0;
		for( int i = 0; i < cnt; i++ )
		{
			dst.write( (const char*) &dmy, 1 );
		}
	}
	
	
#endif

	src.close();
	dst.close();
	
	return 0;
}


/*----------------------------------------------------------
----------------------------------------------------------*/
bool is_btn(callback_data_t *data)
{
	guchar value;
	data->gpio_stop_btn->read_value(&value);
	if (value == 1){
		return false;
	}

	return true;
}

/*----------------------------------------------------------
----------------------------------------------------------*/
bool is_video( callback_data_t *data )
{
	std::string path = "/sys/devices/platform/soc/10058000.i2c/i2c-0/0-0020/status3";
	
	try
	{
		std::ifstream file(path);
		std::string line;
		
		std::getline(file, line);
//		SPDLOG_INFO("check_video = {}",line);
//		std::cout << "長さ: " << line.length() << std::endl;
		if( line.length() == 2 )
		{
			int value = std::stoi(line, nullptr, 16);
//			std::cout << "suuti: " << value << std::endl;
			
			if( ( value  & 0x10 ) == 0 )
			{
//				SPDLOG_INFO("is VIDEO ");
				return true;
			}
		}
	}
	catch (std::exception &ex)
	{
//		SPDLOG_ERROR("Failed to check mout point: {}.", ex.what());
	}
	
	return false;
}

/*----------------------------------------------------------
----------------------------------------------------------*/
bool is_usb_connenct()
{
	if( _usb->connected )
	{
		return true;
	}
	
	return false;
}


const std::filesystem::path inf1 = "/mnt/sd/EVC/REC_INF1.dat";
const std::filesystem::path inf2 = "/mnt/sd/EVC/REC_INF2.dat";

/*----------------------------------------------------------
----------------------------------------------------------*/
bool ReadIndex( const char* path, int* index )
{
	char buf[256];
	memset( buf, 0, sizeof(buf) );
	
	int rc;
	int ret = nvr::file_read( path, buf, 255, &rc );
	
	if( ret < 0 )
	{
		SPDLOG_INFO( "{}読み込みerr", path );
		return false;
	}
	
	printf( "%s : %s\n", path, buf );
	
	*index = 0;
	
	for( int i = 0; i < rc; i++ )
	{
		char c = buf[i];
		
		if( (c >= 0x30) && (c <= 0x39) )
		{
			*index *= 10;
			*index += c - 0x30; 
		}
		else
		{
			break;
		}
	}
	
	printf( "index=%d\n", *index );
	
	return true;
}

/*----------------------------------------------------------
----------------------------------------------------------*/
bool ReadRecMax( int* file_max )
{
	bool ret = ReadIndex( inf1.c_str(), file_max );
	
	printf( "ReadIndex() %s RecMax=%d\n", inf1.c_str(), file_max );
	
	return ret;
}

/*----------------------------------------------------------
----------------------------------------------------------*/
bool ReadRecIndex( int* file_idx )
{
	bool ret = ReadIndex( inf2.c_str(), file_idx );
	
	printf( "ReadIndex() %s RecIndex=%d\n", inf2.c_str(), file_idx );
	
	return ret;
}

/*----------------------------------------------------------
----------------------------------------------------------*/
bool WriteRecIndex( int file_idx, int file_max )
{
	char buf[256];
	memset( buf, 0, sizeof(buf) );
	
	printf( "file_idx=%d file_max=%d \n", file_idx, file_max );
	
	if( file_max <= 999999 )
	{
		sprintf( buf, "%06u", file_idx );
	}
	else
	{
		sprintf( buf, "%08u", file_idx );
	}
	
	int len = strlen( buf );
	int rc;
	if( nvr::file_write( inf2.c_str(), buf, len, &rc ) < 0 )
	{
		printf( "file_write() false\n" );
		return false;
	}
	
	printf( "%s : %s\n", inf2.c_str(), buf );

	return true;
}





/*----------------------------------------------------------
----------------------------------------------------------*/
bool copy_jpeg( callback_data_t* data )
{
	std::filesystem::path save_path;
	char num_dir1[256];
	char num_dir2[256];
	char num_dir3[256];
	char s_dir1[256];
	char s_dir2[256];
	char s_dir3[256];
	char s_file[256];
	int d1;
	int d2;
	int d3;
	int file;
	fs::path dir1;
	fs::path dir2;
	fs::path dir3;

	std::error_code ec;

	//---ファイルをリネームして確保
	//
	if (std::rename("/tmp/video.jpg","/tmp/_video.jpg") == -1)
	{
		SPDLOG_ERROR("Failed to rename {} to {}: {}", "/tmp/video.jpg",
					"/tmp/_video.jpg", std::strerror(errno));
		
		return false;
	}
	else
	{
		SPDLOG_DEBUG("Rename {} to {}", "/tmp/video.jpg", "/tmp/_video.jpg");
	}


	SPDLOG_INFO("file_idx = {}",file_idx);
	
	//保存先をファイルのidxから計算
	//
	/************debug************ */
	int sd_extr = 0;
	/***************************** */

	if(sd_extr) //dir3
	{
		d1 = (file_idx / 1000000) % 100;   // 百万単位
		d2 = (file_idx / 10000) % 100;     // 万単位
		d3 = (file_idx / 100) % 100;       // 百単位
		file = file_idx % 100;


		sprintf(num_dir1, "%02d", d1);
		sprintf(num_dir2, "%02d", d2);
		sprintf(num_dir3, "%02d", d3);

		strcpy( s_dir1, "DIR1_" );
		strcat( s_dir1, num_dir1);

		strcpy( s_dir2, "DIR2_" );
		strcat( s_dir2, num_dir2);

		strcpy( s_dir3, "DIR3_" );
		strcat( s_dir3, num_dir3);

		dir1 = fs::path(s_dir1);
		dir2 = fs::path(s_dir2);
		dir3 = fs::path(s_dir3);

		save_path = fs::path("/mnt/sd/EVC") / dir1 / dir2 / dir3 ;
	}
	else		//dir2
	{
		d1 = (file_idx / 10000) % 100;     // 万単位
		d2 = (file_idx / 100) % 100;       // 百単位
		file = file_idx % 100;

		sprintf(num_dir1, "%02d", d1);
		sprintf(num_dir2, "%02d", d2);

		strcpy( s_dir1, "DIR1_" );
		strcat( s_dir1, num_dir1);

		strcpy( s_dir2, "DIR2_" );
		strcat( s_dir2, num_dir2);


		dir1 = fs::path(s_dir1);
		dir2 = fs::path(s_dir2);
		save_path = fs::path("/mnt/sd/EVC") / dir1 / dir2;
	}
		

	//---保存するディレクトリの存在確認
	//
	if (!fs::exists(save_path, ec))
	{
		//---ディレクトリがないので作成
		//
		if (!fs::create_directories(save_path, ec))
		{
			SPDLOG_ERROR("Failed to make directory: {}.", save_path.c_str());	
			return false;
		}
		SPDLOG_INFO("MAKE JPEG dir");
	}
	
	
	//---ヘッダーの変更
	//
	
	
	
	//---ファイル保存
	//
	try 
	{
		save_path = save_path / g_strdup_printf( "JP%06u.DAT",file_idx);
										
		SPDLOG_INFO("path = {}",save_path.c_str());
		overwriteByteInJPEG( data, save_path );	
		//fs::copy_file("/tmp/_video.jpg", save_path, fs::copy_options::overwrite_existing);
		SPDLOG_INFO("jpeg save");
	}
	catch (const fs::filesystem_error& e) 
	{
		std::cerr << "画像コピー失敗: " << e.what() << '\n';
		return false;
	}


	//---ファイルの同期
	//
	try 
	{
		int fd = ::open(save_path.c_str(), O_WRONLY);
		if (fd != -1)
		{
			::fsync(fd);                
			::close(fd);
		}
	}
	catch (const fs::filesystem_error& e) 
	{
		std::cerr << "sync失敗: " << e.what() << '\n';
		return false;
	}
	
	strcpy( last_save_path, save_path.c_str() );


	//---避けたファイルを削除
	//
	fs::remove( fs::path("/tmp/_video.jpg"));

	
	//---inf2に対して書き込みファイル数を上書き
	//
	WriteRecIndex( file_idx, file_max );
	
	
	file_idx++;
	
	if( file_idx >= file_max )
	{
		file_idx = 0;
	}
	
	
	return true;
}


/*----------------------------------------------------------
	一定日録画チェック
----------------------------------------------------------*/
bool chk_rec_dt( bool* no_rec_err )
{
	time_t tme = time( NULL );
	struct tm* _tm = localtime( &tme ); 

	unsigned char rtc_Y = _tm->tm_year - 100;
	unsigned char rtc_M = _tm->tm_mon + 1;
	unsigned char rtc_D = _tm->tm_mday;
	unsigned char rtc_h = _tm->tm_hour;
	unsigned char rtc_m = _tm->tm_min;

	if( (rtc_h == 10) && (rtc_m == 0) && (chkexeflg == 0) )
	{	
		//if( (USB_status == 0x00) && (rec_status == 0x02) && ((Err_status & 0xDF) == 0) )
		{
			 recchekflg = 1;
		}
		
		if( (rtc_h != 10) && (rtc_m != 0) && (chkexeflg == 1) )
		{	
			chkexeflg = 0;
		}
	}
	
	if( recchekflg == 1 )
	{
		recchekflg = 0;
		chkexeflg = 1;
		
		
		int size = 0;
		char* dat = NULL;
		try
		{
			// JPEG fileからリード
			std::ifstream ifs( (const char*)last_save_path, std::ios::binary );
			
			ifs.seekg( 0, std::ios::end );
			size = ifs.tellg();
			ifs.seekg( 0 );

			dat = new char[size];
			ifs.read( dat, size );
		}
		catch( const std::exception& e )
		{
			return false;
		}
		
		unsigned char fs[19];
		
		if( size >= 380 )
		{
			memcpy( fs, &dat[287], 19 );
		}
		
		delete dat;
		
		if( size < 380 )
		{
			return false;
		}
		
		unsigned char Y = nvr::CCToByte( &fs[ 2] );
		unsigned char M = nvr::CCToByte( &fs[ 5] );
		unsigned char D = nvr::CCToByte( &fs[ 8] );
		unsigned char h = nvr::CCToByte( &fs[11] );
		unsigned char m = nvr::CCToByte( &fs[14] );
		
		if( (rtc_Y != Y) ||
			(rtc_M != M) ||
			(rtc_D != D) ||
			(rtc_h != h) ||
			(rtc_m != m) )
		{
			*no_rec_err = true;
		}
		else
		{
			*no_rec_err = true;
		}
	}
	
	return true;
}

/*----------------------------------------------------------
----------------------------------------------------------*/
void set_broken_output( bool on )
{
	broken_test_ena = on;
	
	if( on )
	{
		//broken_test_cnt = 30 * 60 * 10;		// 30分(100ms単位)
		broken_test_cnt = 5 * 60 * 10;
		printf( "broken_test_cnt = %d\n", broken_test_cnt );
	}
	else
	{
		broken_test_cnt = 0;
	}
	
	printf( "broken_test_cnt = %d\n", broken_test_cnt );
}



/*----------------------------------------------------------
	100 ms Timer
----------------------------------------------------------*/
static gboolean on_timer( gpointer udata )
{
	callback_data_t *data = static_cast<callback_data_t *>(udata);
	static int count_btn_down = 0;
	static int count_rec_off = 0;
	static int count_sd_none = 0;
	static int count_copy_interval = 0;
	static bool tgl_alrm = false;
	static int alrm_cnt = 0;
	
	static bool err_sd_none = false;		// SDカードなし
	static bool sd_access_err = false;		// SDアクセス異常
	static bool no_rec_err = false;			// 一定日録画なし
	static bool system_error = false;		// システム異常
	
	static int chk_video_cnt = 0;
	
	static bool is_v = false;
	
	bool* p_ena = data->broken_test_ena;
	int* p_cnt = data->broken_test_cnt;
	
	static int wait_cnt = 0;
	
///	SPDLOG_INFO("on_timer");



	if( *p_cnt > 0 )
	{
		(*p_cnt)--;
	}

	
	if( is_usb_connenct )
	{
		gst_element_set_state( static_cast<GstElement*>(*data->pipeline), GST_STATE_PAUSED );
	}
	else
	{
		gst_element_set_state( static_cast<GstElement*>(*data->pipeline), GST_STATE_PLAYING );
	}

	
	//--- システムエラー状態
	//
	if( system_error == true )
	{
		data->led->set_r( data->led->blink );
		data->led->set_g( data->led->off );
		data->led->set_y( data->led->off );
		
		return G_SOURCE_CONTINUE;
	}
	
	
	chk_video_cnt++;
	if( chk_video_cnt >= 10 )
	{
		chk_video_cnt = 0;
		
		//--- 映像入力異常 監視 ( 60秒映像断 )
		//
		is_v = is_video(data);
		if( is_v == false )
		{
			if( count_video_lost > 60 )
			{
				count_video_found = 3;
			}
			else
			{
				count_video_lost++;
			}
		}
		else
		//--- 映像信号安定待ち ( 3秒連続受信 ）
		//
		if( count_video_found != 0 )
		{
			count_video_found--;
		}
	}
	
	if( count_video_found == 0 )
	{
		if( is_v )
		{
			count_video_lost = 0;
		}
			
		//--- sd card フォーマット中
		//
		if( formatting == true )
		{
			data->led->set_r( data->low_battry  ? data->led->blink : data->led->off );
			data->led->set_g( is_usb_connenct() ?  : data->led->two );

			int res = data->sd_manager->is_formatting();
			if (res == 0) 	//---フォーマット中
			{
			//なにもしない
			}
			else
			if (res == 1)	//---フォーマット完了
			{
				formatting = false;
				data->led->set_y( data->led->off );
			}
			else
			if (res == 2)	//---フォーマットerr
			{
				//ERR
			}
		}
		//--- sd card あり
		//
		else
		if( count_sd_none == 0 )
		{
			//--- USB接続中
			//
			if( is_usb_connenct() )
			{
				SPDLOG_INFO( "connencted" );
				
				count_btn_down = 0;
				count_rec_off = 0;
				count_sd_none = 0;
				count_copy_interval = 0;
				//alrm_cnt = 0;
				//tgl_alrm = false;
			
				data->led->set_r( data->low_battry  ? data->led->blink : data->led->off );
				data->led->set_g( data->led->blink );
				
				if( usb_sd_access )
				{
					data->led->set_y( data->led->on );
					usb_sd_access = false;
				}
				else
				{
					data->led->set_y( data->led->off );
				}
			}
			else
			
			//--- 録画中
			//
			if( count_rec_off == 0 )
			{
				SPDLOG_INFO( "not connenct" );

	///			SPDLOG_INFO("on-timer 1");
				data->led->set_r( data->low_battry  ? data->led->blink : data->led->off );
				data->led->set_g( data->led->on );
				
				//--- SDカードチェック
				//
				if( data->sd_manager->is_sd_card() == false )
				{
	///				SPDLOG_INFO("on-timer 2");
					//--- SDカードの未挿入時間を計測開始
					//
					SPDLOG_INFO("いきなり抜かれた");
					count_sd_none = 600;  //60s
					data->sd_manager->unmount_sd();
				}
				
				
				//--- 録画停止ボタンチェック
				//
	///			SPDLOG_INFO("on-timer 3");
				if( is_btn(data) == true )
				{
	///				SPDLOG_INFO("on-timer 3-1");
					count_btn_down++;
					if( count_btn_down == 30 )
					{
	///					SPDLOG_INFO("on-timer 3-2");
						//--- 録画停止中へ移行
						//
	///					SPDLOG_INFO("録画停止");
						count_rec_off = 300; //30S
						data->sd_manager->unmount_sd();

					}
				}
				else
				{
	///				SPDLOG_INFO("on-timer 3-3");
					count_btn_down = 0;
				}

				
				//--- JPEGファイルコピー
				//
	///			SPDLOG_INFO("on-timer 4");			
				count_copy_interval++;
				if( count_copy_interval >= data->copy_interval )
				{
	///				SPDLOG_INFO("on-timer 4-1");
					count_copy_interval = 0;

					data->led->set_y( data->led->on );
					//copy();
					if( !copy_jpeg( data ) )
					{
						if( count_sd_access_err < 600 )
						{
							count_sd_access_err++;
						}
						else
						{
							sd_access_err = true;
						}
					}
					
					if( chk_rec_dt( &no_rec_err ) )
					{
						if( no_rec_err )
						{
							// 一定日録画なし
							//Err_status |= 0x02;
							//logoutcode = 10;
							//Log_out();
						}
					}
				}
				else
				{
	///				SPDLOG_INFO("on-timer 4-2");
					data->led->set_y( data->led->off );
				}
				
				
			}
			//--- 録画停止中
			//
			else
			{
				SPDLOG_INFO( "not connenct" );
				
	///			SPDLOG_INFO("on-timer 5");
				data->led->set_r( (count_video_found != 0) ? data->led->blink : 
								  data->low_battry         ? data->led->blink : data->led->off );
								  
				data->led->set_g( (count_video_found != 0) ? data->led->off   : data->led->one );
				
				data->led->set_y( data->led->off );
				
				count_btn_down = 0;
				count_copy_interval = 0;
				
				if( data->sd_manager->is_sd_card() == false )
				{
	///				SPDLOG_INFO("on-timer 5-1");
					//--- SDカードの未挿入時間を計測開始
					//
					//count_sd_none = 72000;		// 2時間
					count_sd_none = 3000;
				}
				else
				{
	///				SPDLOG_INFO("on-timer 5-2");
					//--- 30秒経過で録画中に戻る
					//
					count_rec_off--;
					if(count_rec_off == 0)
					{
						data->sd_manager->mount_sd();
					}
				}
			}
		}
		//--- sd card なし
		//
		else
		{
	///		SPDLOG_INFO("on-timer 6");
			//data->led->set_r( data->led->blink );
			//data->led->set_g( data->led->off );
			data->led->set_y( data->led->off );
			
			count_rec_off = 0;
			count_btn_down = 0;
			count_copy_interval = 0;
         
			//--- sdカードが挿入された
			//
			if( data->sd_manager->is_device_file_exists() == true )
			{
				if( wait_cnt < 10 )
				{
					wait_cnt++;
					return G_SOURCE_CONTINUE;
				}
				
	///			SPDLOG_INFO("on-timer 6-1");
				count_sd_none = 0;		  //60s
				err_sd_none = false;	
				
				count_rec_off = 300; //30S
				SPDLOG_INFO("SD挿入");
				

				//---SDカードがマウントされているか
				//
				if ( data->sd_manager->check_proc_mounts() == false )
				{
					data->sd_manager->mount_sd();
					if( data->sd_manager->check_proc_mounts() == false )
					{
						SPDLOG_INFO("check_proc_mounts false");
					}
				}
					
					
				//---sd card チェック
				//
				if( data->sd_manager->is_root_file_exists()  == false )
				{
					// なんならフォーマット
					//フォーマット中はどうする？
	///				SPDLOG_INFO("on-timer 6-2");
	///				SPDLOG_INFO("format start");				
					data->sd_manager->start_format();
					data->led->set_y( data->led->on );
					formatting = true;
				}
				
				// REC_INF
				ReadRecMax( &file_max );
				ReadRecIndex( &file_idx );
			}
			else
			{
				wait_cnt = 0;
				
				//--- 一定時間SDカードが未挿入
				//
				if( count_sd_none == 1 )
				{
		///			SPDLOG_INFO("on-timer 7");
					err_sd_none = true;
					data->led->set_r( data->led->blink );
					data->led->set_g( data->led->off );
				}
				
				//--- SDカード未挿入時間カウント中
				//
				else
				{
		///			SPDLOG_INFO("on-timer 8");
					count_sd_none--;
				}
			}
			
		}
	}
	
	data->led->update_led();
	
	
#if 1
	
	// 1秒毎に出力を更新
	alrm_cnt++;
	if( alrm_cnt >= 10 )
	{
		alrm_cnt = 0;
		tgl_alrm = !tgl_alrm;
	}
	
	//printf( "test %d %d \n", *p_ena, *p_cnt );
	
	if( (count_video_lost > 60) ||										// 映像入力異常
		err_sd_none || 													// SDカードなし
		sd_access_err || 												// SDアクセス異常
		no_rec_err || 													// 一定日録画なし
		system_error ||										 			// システム異常
		(*p_ena && *p_cnt) )		// 試験発報
	{
		// ijou joutai or broken test
		data->led->set_r( data->led->blink );
		
		if( alrm_cnt == 0 )
		{
			data->gpio_alrm_a->write_value( false );
			data->gpio_alrm_b->write_value( false );
		}
	}
	else
	{
		// normal
		data->led->set_r( data->led->off );
		
		
		if( alrm_cnt == 0 )
		{
			data->gpio_alrm_a->write_value( tgl_alrm );
			data->gpio_alrm_b->write_value( true );
		}
	}
	
#endif
	
	return G_SOURCE_CONTINUE;
}


/*----------------------------------------------------------
----------------------------------------------------------*/
static gboolean callback_bus_watch_cb(GstBus * /* bus */, GstMessage *message, gpointer udata)
{
	struct callback_data_t *data = static_cast<callback_data_t *>(udata);
	
	switch (GST_MESSAGE_TYPE(message))
	{
		case GST_MESSAGE_ERROR:
		{
			GError *err;
			gchar *debug;
			
			//---エラー処理
			//
			gst_message_parse_error(message, &err, &debug);
			if (err) {
				g_error_free(err);
			}
			if (debug){
				g_free(debug);
			}
			
			if (data->interrupted.load(std::memory_order_relaxed)) {
				g_main_loop_quit(data->main_loop);
			} 
			else 
			{
				if (!data->pipeline->stop()) {
					g_main_loop_quit(data->main_loop);
				}
				SPDLOG_INFO("pipeline->start()");
				if (!data->pipeline->start()) 
				{
					g_main_loop_quit(data->main_loop);
				}
			}
			break;
		}
		
		case GST_MESSAGE_EOS:
			/* end-of-stream */
			SPDLOG_DEBUG("End of stream.");
			if (data->interrupted.load(std::memory_order_relaxed)) {
				g_main_loop_quit(data->main_loop);
			}
			else
			{
				if (!data->pipeline->stop()) {
					g_main_loop_quit(data->main_loop);
				}
				SPDLOG_INFO("test_8");
				if (!data->pipeline->start()) {
					g_main_loop_quit(data->main_loop);
				}
			}
			break;
			
		case GST_MESSAGE_ELEMENT:
		{
			const GstStructure *s = gst_message_get_structure(message);
			
			if (s && gst_structure_has_name(s, "GstMultiFileSink"))
			{
				//---受信したデータをjpegにして保存
				//
				const gchar *filename = gst_structure_get_string(s, "filename");
				
				if (filename)
				{
					std::time_t jpeg_time;
					
					//リネームでファイル移動させる
					//
					if (std::rename(filename, data->jpeg_file) == -1)
					{
						SPDLOG_ERROR("Failed to rename {} to {}: {}", filename, data->jpeg_file, std::strerror(errno));
					}
					else
					{
						SPDLOG_DEBUG("Rename {} to {}", filename, data->jpeg_file);
					}
					jpeg_time = time(nullptr);
					data->jpeg_time.store(jpeg_time, std::memory_order_relaxed);
				}
			}
			break;
		}
		
		case GST_MESSAGE_APPLICATION:
		{
			const GstStructure *s = gst_message_get_structure(message);
			
			if (gst_structure_has_name(s, g_interrupt_name))
			{
				SPDLOG_DEBUG("Interrupted: Stopping pipeline.");
				if (data->pipeline->send_eos_event()) {
					SPDLOG_DEBUG("EOS message sent.");
				}
				else
				{
					SPDLOG_DEBUG("Failed to send eos event.");
				}
				
				if (data->pipeline->post_application_message(
					gst_structure_new(g_quit_name, "message", G_TYPE_STRING, "Quit", nullptr)))
				{
					SPDLOG_DEBUG("Quit message sent.");
				}
				else
				{
					SPDLOG_DEBUG("Failed to send start message.");
				}
			}
			else
			if(gst_structure_has_name(s, g_quit_name)) 
			{
				SPDLOG_DEBUG("Quit message.");
				g_main_loop_quit(data->main_loop);
			}
			break;
		}
		default:
		/* unhandled message */
		break;
	}
		
	return G_SOURCE_CONTINUE;
}


/*----------------------------------------------------------
----------------------------------------------------------*/
gboolean video_send_message(GstElement *pipeline) 
{
	//---pipelineのチェック
	//
	if (!pipeline) {
		return FALSE;
	}
	
	/* post an application specific message */
	gboolean ret = gst_element_post_message(GST_ELEMENT(pipeline),
											gst_message_new_application(GST_OBJECT(pipeline),
											gst_structure_new(g_interrupt_name, 
											"message", G_TYPE_STRING,
											"Pipeline interrupted", nullptr)));
	
	if (ret) {
		SPDLOG_DEBUG("Interrupt message sent.");
	}
	
	return ret;
}




/***********************************************************
***********************************************************/

/*----------------------------------------------------------
----------------------------------------------------------*/
static nvr::video_src *video_src(const nvr::config& config)
{
	return new nvr::v4l2_src(config.video_width(), config.video_height());
}


/*----------------------------------------------------------
----------------------------------------------------------*/
static nvr::jpeg_sink *video_jpeg_sink(const char *tmp_dir, const nvr::config& /* config */)
{
	nvr::jpeg_sink *ret = nullptr;
	std::filesystem::path path(tmp_dir);
	path /= "jpeg";
	
	std::error_code ec;
	
	//---/tmp/nvrのディレクトリがあるかの存在確認
	//
	if (!std::filesystem::is_directory(path, ec))
	{
		//ディレクトリがないので作成
		//
		if (!std::filesystem::create_directories(path, ec)){
			SPDLOG_ERROR("Failed to make directory: {}.", path.c_str());
			return nullptr;
		}
	}
	
	
	//--- jpegの保存場所のpathを返す
	//
	path /= "image%d.jpg";
	SPDLOG_DEBUG("Jpeg location is {}.", path.c_str());
	
	return new nvr::jpeg_sink(path.c_str());
}


/*----------------------------------------------------------
----------------------------------------------------------*/
static std::filesystem::path video_done_directory(const char *tmp_dir)
{
	std::filesystem::path path(tmp_dir);
	path /= "done";
	std::error_code ec;
	
	//---ディレクトリの存在確認
	//
	if (!std::filesystem::is_directory(path, ec))
	{
		//---ディレクトリがないので作成
		//
		if (!std::filesystem::create_directories(path, ec)){
			SPDLOG_ERROR("Failed to make directory: {}.", path.c_str());
			return std::filesystem::path();
		}
	}
	
	try 
	{
		for (const std::filesystem::directory_entry &entry : std::filesystem::directory_iterator(path))
		{
			//---通常ファイルだったらリンク解除
			//
			/**********************************************************/
			if (entry.is_regular_file())
			{
				unlink(entry.path().c_str());
			}
		}
	} 
	catch (const std::exception &err) 
	{
		// Do nothing
	}
	
	return path;
}

/*----------------------------------------------------------
----------------------------------------------------------*/
static guint video_add_bus_watch(std::shared_ptr<nvr::pipeline> pipeline, callback_data_t *data)
{
	//---gstreamerのcb作成
	//
	guint ret = 0;
	GstBus *bus = pipeline->bus();
	ret = gst_bus_add_watch(bus, (GstBusFunc)callback_bus_watch_cb, data);
	gst_object_unref(bus);
	return ret;
}



/***********************************************************
***********************************************************/
/*------------------------------------------------------
------------------------------------------------------*/
int GetDownDt( const char* path )
{
	int ret;
	
	return 0;
} 

/***********************************************************
***********************************************************/
/*----------------------------------------------------------
----------------------------------------------------------*/
int main(int argc, char **argv)
{
	const char *driver = USB_RAW_GADGET_DRIVER_DEFAULT;
	const char *device = USB_RAW_GADGET_DEVICE_DEFAULT;
	
	SPDLOG_INFO("main start");
	
	//---init
	//
	std::shared_ptr<nvr::pipeline> pipeline;
	spdlog::set_level(spdlog::level::debug);
	
	callback_data_t data{};
	SPDLOG_INFO("ver0.1.4");
	
	//GPIO 初期化
	//
	std::shared_ptr<nvr::led_manager> led         = std::make_shared<nvr::led_manager>();
//	std::shared_ptr<nvr::gpio_out>	rst_decoder   = std::make_shared<nvr::gpio_out>("168", "P6_0");
//	std::shared_ptr<nvr::gpio_out>	pwd_decoder   = std::make_shared<nvr::gpio_out>("169", "P6_1");
	std::shared_ptr<nvr::gpio_in>	cminsig       = std::make_shared<nvr::gpio_in >("225", "P13_1");
	std::shared_ptr<nvr::gpio_in>	gpio_battery  = std::make_shared<nvr::gpio_in >("201", "P10_1");
	std::shared_ptr<nvr::gpio_out>	gpio_alrm_a   = std::make_shared<nvr::gpio_out>("216", "P12_0");
	std::shared_ptr<nvr::gpio_out>	gpio_alrm_b   = std::make_shared<nvr::gpio_out>("217", "P12_1");
	std::shared_ptr<nvr::gpio_in>   gpio_stop_btn = std::make_shared<nvr::gpio_in >("241", "P15_1");
	std::shared_ptr<nvr::eeprom>	eeprom        = std::make_shared<nvr::eeprom  >("/etc/nvr/eeprom.dat");
	std::shared_ptr<nvr::logger>	logger        = std::make_shared<nvr::logger  >(eeprom);
	
	//---SD周りの初期化
	//
	std::shared_ptr<nvr::sd_manager> sd_manager = std::make_shared<nvr::sd_manager>(
		"/dev/mmcblk1p1",
		"/mnt/sd",
		"/usr/bin/nvr",
		logger
	);
	
	
	//--- callback_data_tにポインターセット
	//
	data.led				= led.get();
	data.sd_manager			= sd_manager.get();
	data.pipeline			= pipeline;
	data.logger				= logger;
	data.gpio_stop_btn		= gpio_stop_btn.get();
	data.gpio_battery		= gpio_battery.get();
	data.cminsig			= cminsig.get();
	data.copy_interval		= 10;
	data.broken_test_ena	= &broken_test_ena;
	data.broken_test_cnt	= &broken_test_cnt;
	data.gpio_alrm_a		= gpio_alrm_a;
	data.gpio_alrm_b		= gpio_alrm_b;
	data.usb				= _usb;
	data.eeprom				= eeprom;

	
	
	logger->LogOut( 1 );
	
	/*******************************************************************************/
	ReadRecMax( &file_max );
	ReadRecIndex( &file_idx );
		
	
	/*
	if (pwd_decoder->open(true)) {
		SPDLOG_ERROR("Failed to open pwd_decoder."));
		exit(-1);
	}
	
	if (rst_decoder->open(true)) {
		SPDLOG_ERROR("Failed to open rst_decoder.");
		exit(-1);
	}
	*/
	
	if (cminsig->open()) {
		SPDLOG_ERROR("Failed to open cminsig.");
		exit(-1);
	}
	
	if (gpio_battery->open(nullptr)) {
		SPDLOG_ERROR("Failed to open gpio_battery.");
		exit(-1);
	}
	
	//---機器異常出力初期化
	//
	if (gpio_alrm_a->open()) {
		SPDLOG_ERROR("Failed to open gpio_alrm_a.");
		exit(-1);
	}
	
	if (gpio_alrm_b->open()) {
		SPDLOG_ERROR("Failed to open gpio_alrm_b.");
		exit(-1);
	}
	
	//---録画停止ボタン初期化
	//
	if (gpio_stop_btn->open()) {
		SPDLOG_ERROR("Failed to open gpio_stop_btn.");
		exit(-1);
	}
	
	
	_usb = std::make_shared<nvr::usb>( eeprom );
	
	
	SPDLOG_INFO("3");
	
	//---gstreamer初期化
	//
	gst_init(&argc, &argv);
	
	
	//--- pipeline初期化
	//
	const char *config_file = "/etc/nvr/nvr.json";
	const char *factoryset_file = "/etc/nvr/factoryset.json";
	const char *tmp_dir = "/tmp/nvr";
	const char *jpeg_file = "/tmp/video.jpg";
	pipeline = std::make_shared<nvr::pipeline>();
	
	auto config = nvr::config(config_file, factoryset_file);
	pipeline->set_video_src(video_src(config));
	pipeline->set_jpeg_sink(video_jpeg_sink(tmp_dir, config));
	
	data.jpeg_file = jpeg_file;
	data.done_dir = video_done_directory(tmp_dir);
	if (data.done_dir.empty()){
		std::exit(-1);
	}
	
	


	//--- ルートファイルシステム 再マウント
	//
	if (mount(nullptr, "/", nullptr, MS_REMOUNT, nullptr)) {
		SPDLOG_ERROR("Failed to remount /.");
		exit(-1);
	}
	
	sleep(5);
	
	
	
	led->set_g(led->off);
	led->set_r(led->off);
	
	

	//---ボタン電池の電圧確認
	//
	#if 0
	guchar bat;
	GIOStatus rc = data.gpio_battery->read_value( &bat );
	if( (rc != G_IO_STATUS_NORMAL) || (bat == 0) )
	{
		data.low_battry = true;
	}
	else
	{
		data.low_battry = false;
	}
#else
		data.low_battry = false;
#endif	
	
	std::thread thread_usb;
	thread_usb = std::thread( _usb->main_proc, _usb );
	
	//--- 優先度を上げる。
	//
	struct sched_param param;
	param.sched_priority = std::max( sched_get_priority_max(SCHED_FIFO) / 3, sched_get_priority_min(SCHED_FIFO) );
	if( pthread_setschedparam( thread_usb.native_handle(), SCHED_FIFO, &param ) != 0 )
	{
		SPDLOG_WARN( "Failed to thread_usb scheduler." );
	}

	
	
	
	
	//--- callback bus
	//
	data.bus_watch_id = video_add_bus_watch(pipeline, &data);
	if (!data.bus_watch_id) {
		SPDLOG_ERROR("Failed to wach bus.");
		exit(-1);
	}
	
	
	//--- timer1 start
	//
	SPDLOG_INFO("timer set");
	data.timer1_id = g_timeout_add_full( G_PRIORITY_HIGH,
										100,
										G_SOURCE_FUNC(on_timer),
										&data,
										nullptr );
	if (!data.timer1_id){
		SPDLOG_ERROR("Failed to add timer1.");
		exit(-1);
	}
	
	
	
	
	//---録画スレッドの作成
	//
	SPDLOG_DEBUG("Start video writer.");
	led->set_g(led->on);
	
	
	//---pipelineを有効可
	//
	SPDLOG_DEBUG("Start pipeline.");
	if (pipeline->start()) 
	{
		data.main_loop = g_main_loop_new(nullptr, FALSE);
		SPDLOG_INFO("Start main loop.");
		
		time_t t = time(nullptr);
		data.jpeg_time.store(t, std::memory_order_relaxed);
		
		
		//---メインループ開始
		//
		g_main_loop_run(data.main_loop);
		SPDLOG_INFO("Main loop is stopped.");
		g_main_loop_unref(data.main_loop);
		pipeline->stop();
	}
	
END:
	//---終了処理
	//
	data.interrupted.store(true, std::memory_order_relaxed);
	
	_usb->th_end = 1;
	if( thread_usb.joinable() )
	{
		thread_usb.join();
	}
	
	
	if( data.bus_watch_id )
	{
		g_source_remove( data.bus_watch_id );
	}
	
	if (data.timer1_id)
	{
		g_source_remove(data.timer1_id);
	}
	
	
	
	std::exit(0);
}

/***********************************************************
	end of file
***********************************************************/



