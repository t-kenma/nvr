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

//#define JCONFIG_INCLUDED
//#include <jpeglib.h>

#include "config.hpp"
#include "pipeline.hpp"

#include "gpio.hpp"
#include "sd_manager.hpp"
#include "led_manager.hpp"
#include "util.hpp"
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
	nvr::gpio_out *pwd_decoder;
	nvr::gpio_out *gpio_alrm_a;
	nvr::gpio_out *gpio_alrm_b;
	nvr::gpio_in *gpio_battery;
	nvr::gpio_in *pgood;
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
	
	
	
};

namespace fs = std::filesystem;
static const char *g_interrupt_name = "nrs-video-recorder-interrupted";
static const char *g_quit_name = "nrs-video-recorder-quit";

//int flag = 0;




int count_btn_down = 0;
int count_rec_off = 0;
int count_sd_none = 0;

int count_video_lost = 0;
int count_video_found = 0;
bool system_error = false;
bool formatting = false;


bool err_sd_none = false;
bool broken_test = false;
bool tgl_alrm = false;
int alrm_cnt = 0;

/*-------後で設定ファイルに持っていく-------*/
int copy_interval = 100; 
uint8_t count_err = 0;
char serial_no[8] = { '1','9','9','1','0','3','0','6' };

/*---------------------------------------*/
uint64_t test_idx = 0;

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

/*
void overwriteByteInJPEG(const std::string& filePath) {
    std::fstream file(filePath, std::ios::in | std::ios::out | std::ios::binary);
    
    if (!file) {
        std::cerr << "ファイルを開けませんでした: " << filePath << std::endl;
        return;
    }

    // ファイルサイズチェック
    file.seekg(0, std::ios::end);
    size_t fileSize = file.tellg();

	std::time_t t = std::time(nullptr);
	const std::tm *tm = std::localtime(&t);

	char fs_yea[4];
	char fs_mon[2];
	char fs_day[2];
	char fs_hou[2];
	char fs_min[2];
	char fs_sec[2];
	int year = tm->tm_year +1900;
	int mon = tm->tm_mon + 1;
	int day = tm->tm_mday;
	int hou = tm->tm_hour;
	int min = tm->tm_min;
	int sec = tm->tm_sec;

	fs_yea[0] = ((year / 1000) % 10) + 0x30;
    fs_yea[1] = ((year / 100) % 10) + 0x30;
    fs_yea[2] = ((year / 10) % 10) + 0x30;
    fs_yea[3] = (year % 10) + 0x30;

    fs_mon[0]= ((mon / 10) % 10) + 0x30;
    fs_mon[1] = (mon % 10) + 0x30;

    fs_day[0] = ((day / 10) % 10) + 0x30;
    fs_day[1] = (day % 10) + 0x30;

    fs_hou[0] = ((hou / 10) % 10) + 0x30;
    fs_hou[1] = (hou % 10) + 0x30;

    fs_min[0] = ((min / 10) % 10) + 0x30;
    fs_min[1] = (min % 10) + 0x30;

    fs_sec[0] = ((sec / 10) % 10) + 0x30;
    fs_sec[1] = (sec % 10) + 0x30;


	file.seekp(165, std::ios::beg);
    file.put(static_cast<char>(fs_yea[0]));
	file.seekp(166, std::ios::beg);
    file.put(static_cast<char>(fs_yea[1]));
    file.seekp(167, std::ios::beg);
    file.put(static_cast<char>(fs_yea[2]));
	file.seekp(168, std::ios::beg);
    file.put(static_cast<char>(fs_yea[3]));

	file.seekp(169, std::ios::beg);
    file.put(static_cast<char>(0x2F));

	file.seekp(170, std::ios::beg);
    file.put(static_cast<char>(fs_mon[0]));
	file.seekp(171, std::ios::beg);
    file.put(static_cast<char>(fs_mon[1]));

	file.seekp(172, std::ios::beg);
    file.put(static_cast<char>(0x2F));

	file.seekp(173, std::ios::beg);
    file.put(static_cast<char>(fs_day[0]));
	file.seekp(174, std::ios::beg);
    file.put(static_cast<char>(fs_day[1]));

	file.seekp(175, std::ios::beg);
    file.put(static_cast<char>(0x20));

	file.seekp(176, std::ios::beg);
    file.put(static_cast<char>(fs_hou[0]));
	file.seekp(177, std::ios::beg);
    file.put(static_cast<char>(fs_hou[1]));

	file.seekp(178, std::ios::beg);
    file.put(static_cast<char>(0x3A));

	file.seekp(179, std::ios::beg);
    file.put(static_cast<char>(fs_min[0]));
	file.seekp(180, std::ios::beg);
    file.put(static_cast<char>(fs_min[1]));

	file.seekp(181, std::ios::beg);
    file.put(static_cast<char>(0x3A));

	file.seekp(182, std::ios::beg);
    file.put(static_cast<char>(fs_sec[0]));
	file.seekp(183, std::ios::beg);
    file.put(static_cast<char>(fs_sec[1]));


	//---  dtae :
	//
	file.seekp(279, std::ios::beg);
    file.put(static_cast<char>(0x19));
	file.seekp(280, std::ios::beg);
    file.put(static_cast<char>(0x64));
    file.seekp(281, std::ios::beg);
    file.put(static_cast<char>(0x61));
	file.seekp(282, std::ios::beg);
    file.put(static_cast<char>(0x74));
	file.seekp(283, std::ios::beg);
    file.put(static_cast<char>(0x65));
	file.seekp(284, std::ios::beg);
    file.put(static_cast<char>(0x20));
    file.seekp(285, std::ios::beg);
    file.put(static_cast<char>(0x3A));
	file.seekp(286, std::ios::beg);
    file.put(static_cast<char>(0x20));



	file.seekp(287, std::ios::beg);
    file.put(static_cast<char>(fs_yea[0]));
	file.seekp(288, std::ios::beg);
    file.put(static_cast<char>(fs_yea[1]));
    file.seekp(289, std::ios::beg);
    file.put(static_cast<char>(fs_yea[2]));
	file.seekp(290, std::ios::beg);
    file.put(static_cast<char>(fs_yea[3]));

	file.seekp(291, std::ios::beg);
    file.put(static_cast<char>(0x2F));

	file.seekp(292, std::ios::beg);
    file.put(static_cast<char>(fs_mon[0]));
	file.seekp(293, std::ios::beg);
    file.put(static_cast<char>(fs_mon[1]));

	file.seekp(294, std::ios::beg);
    file.put(static_cast<char>(0x2F));

	file.seekp(295, std::ios::beg);
    file.put(static_cast<char>(fs_day[0]));
	file.seekp(296, std::ios::beg);
    file.put(static_cast<char>(fs_day[1]));

	file.seekp(297, std::ios::beg);
    file.put(static_cast<char>(0x20));

	file.seekp(298, std::ios::beg);
    file.put(static_cast<char>(fs_hou[0]));
	file.seekp(299, std::ios::beg);
    file.put(static_cast<char>(fs_hou[1]));

	file.seekp(300, std::ios::beg);
    file.put(static_cast<char>(0x3A));

	file.seekp(301, std::ios::beg);
    file.put(static_cast<char>(fs_min[0]));
	file.seekp(302, std::ios::beg);
    file.put(static_cast<char>(fs_min[1]));

	file.seekp(303, std::ios::beg);
    file.put(static_cast<char>(0x3A));

	file.seekp(304, std::ios::beg);
    file.put(static_cast<char>(fs_sec[0]));
	file.seekp(305, std::ios::beg);
    file.put(static_cast<char>(fs_sec[1]));

	file.seekp(306, std::ios::beg);
    file.put(static_cast<char>(0x2E));
	file.seekp(307, std::ios::beg);
    file.put(static_cast<char>(0x30));
	file.seekp(308, std::ios::beg);
    file.put(static_cast<char>(0x00));
	file.seekp(309, std::ios::beg);
    file.put(static_cast<char>(0x00));
	file.seekp(310, std::ios::beg);
    file.put(static_cast<char>(0x73));
	file.seekp(311, std::ios::beg);
    file.put(static_cast<char>(0x65));
	file.seekp(312, std::ios::beg);
    file.put(static_cast<char>(0x72));
	file.seekp(313, std::ios::beg);
    file.put(static_cast<char>(0x69));
	file.seekp(314, std::ios::beg);
    file.put(static_cast<char>(0x61));
	file.seekp(315, std::ios::beg);
    file.put(static_cast<char>(0x20));
	file.seekp(316, std::ios::beg);
    file.put(static_cast<char>(0x3A));
	file.seekp(317, std::ios::beg);
    file.put(static_cast<char>(0x20));
	
	//serial
	file.seekp(318, std::ios::beg);
    file.put(static_cast<char>(serial_no[0]));
	file.seekp(319, std::ios::beg);
    file.put(static_cast<char>(serial_no[1]));
	file.seekp(320, std::ios::beg);
    file.put(static_cast<char>(serial_no[2]));
	file.seekp(321, std::ios::beg);
    file.put(static_cast<char>(serial_no[3]));
	file.seekp(322, std::ios::beg);
    file.put(static_cast<char>(serial_no[4]));
	file.seekp(323, std::ios::beg);
    file.put(static_cast<char>(serial_no[5]));
	file.seekp(324, std::ios::beg);
    file.put(static_cast<char>(serial_no[6]));
	file.seekp(325, std::ios::beg);
    file.put(static_cast<char>(serial_no[7]));

	// cycle
	file.seekp(330, std::ios::beg);
    file.put(static_cast<char>(0x63));
	file.seekp(331, std::ios::beg);
    file.put(static_cast<char>(0x79));
	file.seekp(332, std::ios::beg);
    file.put(static_cast<char>(0x63));
	file.seekp(333, std::ios::beg);
    file.put(static_cast<char>(0x6C));
	file.seekp(334, std::ios::beg);
    file.put(static_cast<char>(0x65));
	file.seekp(335, std::ios::beg);
    file.put(static_cast<char>(0x20));
	file.seekp(336, std::ios::beg);
    file.put(static_cast<char>(0x3A));
	file.seekp(337, std::ios::beg);
    file.put(static_cast<char>(0x20));

	//録画周期 仮固定
	file.seekp(338, std::ios::beg);
    file.put(static_cast<char>(0x30));  //固定
	file.seekp(339, std::ios::beg);
    file.put(static_cast<char>(0x31));
	file.seekp(340, std::ios::beg);
    file.put(static_cast<char>(0x30));
	file.seekp(341, std::ios::beg);
    file.put(static_cast<char>(0x30));



	file.seekp(380, std::ios::beg);
    file.put(static_cast<char>(0x00));
	file.seekp(381, std::ios::beg);
    file.put(static_cast<char>(0x00));
	file.seekp(382, std::ios::beg);
    file.put(static_cast<char>(0xFF));
	file.seekp(382, std::ios::beg);
    file.put(static_cast<char>(0xDB));

    file.close();
}
*/


#if 0
result_t write_jpeg_stream( FILE *fp, image_t *img )
{
	result_t result = FAILURE;
	int x, y;
	struct jpeg_compress_struct jpegc;
	my_error_mgr myerr;
	image_t *to_free = NULL;
	JSAMPROW buffer = NULL;
	JSAMPROW row;
	
	if( img == NULL )
	{
		return FAILURE;
	}
	if( buffer = malloc(sizeof(JSAMPLE) * 3 * img->width)) == NULL)
	{
		return FAILURE;
	}
	
	if( img->color_type != COLOR_TYPE_RGB )
	{
		// 画像形式がRGBでない場合はRGBに変換して出力
		to_free = clone_image(img);
		img = image_to_rgb(to_free);
	}
	
	jpegc.err = jpeg_std_error(&myerr.jerr);
	myerr.jerr.error_exit = error_exit;
	if( setjmp(myerr.jmpbuf) )
	{
		goto error;
	}
	
	jpeg_create_compress(&jpegc);
	jpeg_stdio_dest(&jpegc, fp);
	jpegc.image_width = img->width;
	jpegc.image_height = img->height;
	jpegc.input_components = 3;
	jpegc.in_color_space = JCS_RGB;
	jpeg_set_defaults(&jpegc);
	jpeg_set_quality(&jpegc, 75, TRUE);
	jpeg_start_compress(&jpegc, TRUE);
	
	for( y = 0; y < img->height; y++ )
	{
		row = buffer;
		for( x = 0; x < img->width; x++ )
		{
			*row++ = img->map[y][x].c.r;
			*row++ = img->map[y][x].c.g;
			*row++ = img->map[y][x].c.b;
		}
		jpeg_write_scanlines(&jpegc, &buffer, 1);
	}
	
	jpeg_finish_compress(&jpegc);
	result = SUCCESS;

error:
	jpeg_destroy_compress(&jpegc);
	free(buffer);
	free_image(to_free);
	return result;
}

result_t write_jpeg_file( const char *filename, image_t *img )
{
	result_t result = FAILURE;
	FILE *fp;
	
	if( img == NULL )
	{
		return result;
	}
	
	if( (fp = fopen(filename, "wb")) == NULL )
	{
		perror(filename);
		return result;
	}
	
	result = write_jpeg_stream(fp, img);
	fclose(fp);
	return result;
}
#endif

#if 0
void compressYUYVtoJPEG( const vector<uint8_t>& input, const int width, const int height, int quality, vector<uint8_t>& output )
{
    struct jpeg_compress_struct cinfo;
    struct jpeg_error_mgr jerr;
    JSAMPROW row_ptr[1];
    int row_stride;

    uint8_t* outbuffer = NULL;
    uint64_t outlen = 0;

    cinfo.err = jpeg_std_error(&jerr);
    jpeg_create_compress(&cinfo);
    jpeg_mem_dest(&cinfo, &outbuffer, &outlen);

    // jrow is a libjpeg row of samples array of 1 row pointer
    cinfo.image_width = width & -1;
    cinfo.image_height = height & -1;
    cinfo.input_components = 3;
    cinfo.in_color_space = JCS_YCbCr; //libJPEG expects YUV 3bytes, 24bit

    jpeg_set_defaults(&cinfo);
    jpeg_set_quality(&cinfo, quality, TRUE);
    jpeg_start_compress(&cinfo, TRUE);

    vector<uint8_t> tmprowbuf(width * 3);

    JSAMPROW row_pointer[1];
    row_pointer[0] = &tmprowbuf[0];
    while (cinfo.next_scanline < cinfo.image_height) {
        unsigned i, j;
        unsigned offset = cinfo.next_scanline * cinfo.image_width * 2; //offset to the correct row
        for (i = 0, j = 0; i < cinfo.image_width * 2; i += 4, j += 6) { //input strides by 4 bytes, output strides by 6 (2 pixels)
            tmprowbuf[j + 0] = input[offset + i + 0]; // Y (unique to this pixel)
            tmprowbuf[j + 1] = input[offset + i + 1]; // U (shared between pixels)
            tmprowbuf[j + 2] = input[offset + i + 3]; // V (shared between pixels)
            tmprowbuf[j + 3] = input[offset + i + 2]; // Y (unique to this pixel)
            tmprowbuf[j + 4] = input[offset + i + 1]; // U (shared between pixels)
            tmprowbuf[j + 5] = input[offset + i + 3]; // V (shared between pixels)
        }
        jpeg_write_scanlines(&cinfo, row_pointer, 1);
    }

    jpeg_finish_compress(&cinfo);
    jpeg_destroy_compress(&cinfo);

    cout << "libjpeg produced " << outlen << " bytes" << endl;

    output = vector<uint8_t>(outbuffer, outbuffer + outlen);
}
#endif

int overwriteByteInJPEG(const std::string& filePath) {
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
	memcpy(&exif[161], fs_time, sizeof(fs_time));


	//---年月時分秒
	//
	memcpy(&exif[287],fs_time2,sizeof(fs_time2));
	

	//---シリアル
	//
	std::string serial = std::string("serial : ") + serial_no;
	memcpy(&exif[310],serial.c_str(), serial.size()); 

	//---周期
	//
	char numStr[16];
	sprintf(numStr, "%04d", copy_interval);
	std::string cycle = std::string("cycle : ") + numStr;
	memcpy(&exif[330], cycle.c_str(), cycle.size());

	std::ifstream src("/tmp/_video.jpg", std::ios::binary);
    if (!src) {
        std::cerr << "Failed to open source file\n";
        return 1;
    }

	src.seekg(0, std::ios::end);
    size_t fileSize = src.tellg();

	SPDLOG_INFO("src size = {}",fileSize);


#if 1
	// 開始位置にシーク
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
    if (!dst) {
        std::cerr << "Failed to open destination file\n";
        return 1;
    }
    
    // copies all data into buffer
    vector <uint8_t> inbuf( (istreambuf_iterator<char>( src )), (istreambuf_iterator<char>()) );

    vector<uint8_t> output;
    int quality = 90;
    
RETRY:    
    compressYUYVtoJPEG( inbuf, 640, 480, quality, output );
	
	if( output.size() > (64 * 1024) )
	{
		if( quality >= 90 )
		{
			quality = 50;
			goto RETRY;
		}
		else
		{
			goto END;
		}
	} 
	
    std::ofstream dst(filePath.c_str(), std::ios::binary);
    dst.write( (const char*) &output[0], output.size() );
    
	if( output.size() < (64 * 1024) )
    {
    	int] cnt = (64 * 1024) - output.size();
    	
    	for( int i = 0; i < cnt; i++ )
    	{
    		char dmy = 0;
			dst.write( (const char*) &dmy, 1 );
		}
    }
    
    
END:    
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
bool is_video(callback_data_t *data)
{
	//---ここに確認方法を書く
	//
	return false;
}

/*----------------------------------------------------------
----------------------------------------------------------*/
bool is_usb_connenct()
{

	//---ここに確認方法を書く
	//
	return false;
}


/*----------------------------------------------------------
----------------------------------------------------------*/
int copy_jpeg( uint64_t file_idx )
//int copy_jpeg(gpointer udata)
{
	const std::filesystem::path inf2 = "/mnt/sd/EVC/REC_INF2.dat";
	std::filesystem::path save_path;
	char num_dir1[256];
	char num_dir2[256];
	char num_dir3[256];
	char num_file[256];
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
	}
	else
	{
		SPDLOG_DEBUG("Rename {} to {}", "/tmp/video.jpg",
			"/tmp/_video.jpg");
	}


	SPDLOG_INFO("test_idx = {}",test_idx);
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
		if (!fs::create_directories(save_path, ec)) {
			SPDLOG_ERROR("Failed to make directory: {}.", save_path.c_str());	
		}
		SPDLOG_INFO("MAKE JPEG dir");
	}


	//---ヘッダーの変更
	//
	

	
	//---ファイル保存
	//
	try 
	{
		save_path = save_path / g_strdup_printf( "JP%06lu.DAT",file_idx);
										
		SPDLOG_INFO("path = {}",save_path.c_str());
		overwriteByteInJPEG(save_path);	
		//fs::copy_file("/tmp/_video.jpg", save_path, fs::copy_options::overwrite_existing);
		SPDLOG_INFO("jpeg save");
	}	
	catch (const fs::filesystem_error& e) 
	{
		std::cerr << "画像コピー失敗: " << e.what() << '\n';
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
	}


	//---避けたファイルを削除
	//
	fs::remove( fs::path("/tmp/_video.jpg"));

	
	//---inf2に対して書き込みファイル数を上書き
	//
	sprintf(num_file, "%08lu", file_idx);
	int len = strlen(num_file);
	SPDLOG_INFO("inf2 ={}",num_file);
    if( nvr::file_write(inf2.c_str(), num_file, len) != 0 ){
		//---書き込みerr
		//
		SPDLOG_INFO("inf2書き込みerr");
	}


	//---ファイルの同期
	//
	try 
	{
		int fd = ::open(inf2.c_str(), O_WRONLY);
		if (fd != -1)
		{
			::fsync(fd);
			::close(fd);
		}
	}
	catch (const fs::filesystem_error& e) 
	{
		std::cerr << "sync失敗: " << e.what() << '\n';
	}

	return true;
}

/*----------------------------------------------------------
	100 ms Timer
----------------------------------------------------------*/
static gboolean on_timer(gpointer udata)
{
	callback_data_t *data = static_cast<callback_data_t *>(udata);
	static int count_btn_down = 0;
	static int count_rec_off = 0;
	static int count_sd_none = 0;
	static int count_copy_interval = 0;
	static int count_video = 0;
	
	
///	SPDLOG_INFO("on_timer");

	
	//--- システムエラー状態
	//
	if( system_error == true )
	{
		data->led->set_r( data->led->blink );
		data->led->set_g( data->led->off );
		data->led->set_y( data->led->off );
		
		return G_SOURCE_CONTINUE;
	}
	
	
	//--- 映像入力異常 監視 ( 60秒映像断 )
	//
	if( is_video(data) != false )
	{
		if( count_video_lost > 600 )
		{
			count_video = 30;
		}
		else
		{
			count_video_lost++;
		}
	}
	
	
	//--- 映像信号安定待ち ( 3秒連続受信 ）
	//
	if( count_video_found != 0 )
	{
		data->led->set_r( data->led->blink );
		data->led->set_g( data->led->off );
		data->led->set_y( data->led->off );
		
		count_video_lost = 0;
		count_video_found--;
	}
	//--- sd card フォーマット中
	//
	else
	if( formatting == true )
	{
		data->led->set_r( data->low_battry  ? data->led->blink : data->led->off );
		data->led->set_g( is_usb_connenct() ? data->led->blink : data->led->two );

		int res = data->sd_manager->is_formatting();
        if (res == 0) {	//---フォーマット中
		   //なにもしない
		}
		else
		if (res == 1){	//---フォーマット完了
			formatting = false;
			data->led->set_y( data->led->off );
		}
		else
		if (res == 2){	//---フォーマットerr
			//ERR
		}
	}
	//--- sd card あり
	//
	else
	if( count_sd_none == 0 )
	{
		//--- 録画中
		//
		if( count_rec_off == 0 )
		{
///			SPDLOG_INFO("on-timer 1");
			data->led->set_r( data->low_battry  ? data->led->blink : data->led->off );
			data->led->set_g( is_usb_connenct() ? data->led->blink : data->led->on );
			
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
				copy_jpeg(test_idx);
				test_idx++;
				//---まだ映像遮断検出が出来ていない
				//
				/*
				if( is_low_voltage() == false )
				{

				}
				else
				{
					count_err++;
				}
				*/
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
///			SPDLOG_INFO("on-timer 5");
			data->led->set_r( data->low_battry  ? data->led->blink : data->led->off );
			data->led->set_g( is_usb_connenct() ? data->led->blink : data->led->one );
			data->led->set_y( data->led->off );
			
			count_btn_down = 0;
			count_copy_interval = 0;
			
			if( data->sd_manager->is_sd_card() == false )
			{
///				SPDLOG_INFO("on-timer 5-1");
				//--- SDカードの未挿入時間を計測開始
				//
				count_sd_none = 72000;
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
		data->led->set_r( data->led->blink );
		data->led->set_g( data->led->off );
		data->led->set_y( data->led->off );
		
		count_rec_off = 0;
		count_btn_down = 0;
		count_copy_interval = 0;

		//--- sdカードが挿入された
		//
		if ( data->sd_manager->is_device_file_exists() == true )
		{
///			SPDLOG_INFO("on-timer 6-1");
			count_sd_none = 0;
			err_sd_none = 0;
			SPDLOG_INFO("SD挿入");
			

			//---SDカードがマウントされているか
			//
			if ( data->sd_manager->check_proc_mounts() == false ){
				data->sd_manager->mount_sd();
				if(data->sd_manager->check_proc_mounts() == false){
					SPDLOG_INFO("check_proc_mounts false");
				}
			}
				
				
			//---sd card チェック
			//
			if( data->sd_manager->is_root_file_exists()  == false) {
				// なんならフォーマット
				//フォーマット中はどうする？
///				SPDLOG_INFO("on-timer 6-2");
///				SPDLOG_INFO("format start");				
				data->sd_manager->start_format();
				data->led->set_y( data->led->on );
				formatting = true;
			}			
		}
		
		//--- 一定時間SDカードが未挿入
		//
		else
		if( count_sd_none == 1 )
		{
///			SPDLOG_INFO("on-timer 7");
			err_sd_none = 1;
		}
		
		//--- SDカード未挿入時間カウント中
		//
		else
		{
///			SPDLOG_INFO("on-timer 8");
			count_sd_none--;
		}
	}
	
	data->led->update_led();
	
	
	/*
	if( err_sd_none || broken_test )
	{
		// ijou joutai or broken test
		data->gpio_alrm_a->write_value( false );
		data->gpio_alrm_b->write_value( false );
	}
	else
	{
		// normal
		alrm_cnt++;
		if( alrm_cnt >= 10 )
		{
			alrm_cnt = 0;
			tgl_alrm = !tgl_alrm;
		}
		
		data->gpio_alrm_a->write_value( tgl_alrm );
		data->gpio_alrm_b->write_value( true );
	}
	*/
	
	return G_SOURCE_CONTINUE;
}


/*----------------------------------------------------------
----------------------------------------------------------*/
void set_broken_output( char on )
{
	broken_test = on;

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
//	std::shared_ptr<nvr::gpio_out>	rst_decoder   = std::make_shared<nvr::gpio_out>("168", "P6_0");
//	std::shared_ptr<nvr::gpio_out>	pwd_decoder   = std::make_shared<nvr::gpio_out>("169", "P6_1");
	std::shared_ptr<nvr::gpio_in>	cminsig       = std::make_shared<nvr::gpio_in >("225", "P13_1");
	std::shared_ptr<nvr::gpio_in>	gpio_battery  = std::make_shared<nvr::gpio_in >("201", "P10_1");
	std::shared_ptr<nvr::gpio_out>	gpio_alrm_a   = std::make_shared<nvr::gpio_out>("216", "P12_0");
	std::shared_ptr<nvr::gpio_out>	gpio_alrm_b   = std::make_shared<nvr::gpio_out>("217", "P12_1");
	std::shared_ptr<nvr::gpio_in>   gpio_stop_btn = std::make_shared<nvr::gpio_in >("241", "P15_1");
	std::shared_ptr<nvr::logger>	logger        = std::make_shared<nvr::logger  >("/etc/nvr/video-recorder.log");
	
	
	/*******************************************************************************/
	
	
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
		SPDLOG_ERROR("Failed to open rst_decoder.");
		exit(-1);
	}
	
	if (gpio_battery->open(nullptr)) {
		SPDLOG_ERROR("Failed to open gpio_stop_btn.");
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
	
	
	nvr::usb* _usb = new nvr::usb();
	std::thread thread_usb;
	thread_usb = std::thread( _usb->main_proc, _usb );
	
	
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
	
	
	std::shared_ptr<nvr::led_manager> led = std::make_shared<nvr::led_manager>();
	
	led->set_g(led->off);
	led->set_r(led->off);
	
	
	//---ボタン電池の電圧確認
	//
	guchar bat;
	//gpio_battery->read_value(&bat);
	if(	gpio_battery->read_value(&bat))
	{
		data.low_battry = false;
	}
	else
	{
		data.low_battry = true;
	}
	
	
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
	data.led			= led.get();
	data.sd_manager		= sd_manager.get();
	data.pipeline		= pipeline;
	data.logger			= logger;
	data.gpio_stop_btn	= gpio_stop_btn.get();
	data.cminsig		= cminsig.get();
	data.copy_interval	= 10;
	
	
	
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










