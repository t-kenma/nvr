#include "usb.hpp"

#include <cstdio>
#include <cstdlib>
#include <cerrno>
#include <atomic>
#include <getopt.h>
#include <filesystem>
#include <sys/file.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>
#include <pthread.h>
#include <iostream>
#include <fstream>
#include <stdexcept>

#ifdef G_OS_UNIX
#include <glib-unix.h>
#endif

#include "logging.hpp"

#include <signal.h>


#define STR_VER		"1.0"



//std::thread *thread_bulk_in = nullptr;
//std::thread *thread_bulk_out = nullptr;

//ring_buffer<char> usb_tx_buffer(524288);
//std::atomic<bool> connected(false);

//constexpr size_t BUF_SIZE = 512;



struct usb_device_descriptor me56ps2_device_descriptor = {
    .bLength            = USB_DT_DEVICE_SIZE,
    .bDescriptorType    = USB_DT_DEVICE,
    .bcdUSB             = __constant_cpu_to_le16(BCD_USB),
    .bDeviceClass       = 0xFF,
    .bDeviceSubClass    = 0xFF,
    .bDeviceProtocol    = 0xFF,
    .bMaxPacketSize0    = MAX_PACKET_SIZE_CONTROL,
    .idVendor           = __constant_cpu_to_le16(USB_VENDOR),
    .idProduct          = __constant_cpu_to_le16(USB_PRODUCT),
    .bcdDevice          = __constant_cpu_to_le16(BCD_DEVICE),
    .iManufacturer      = 0,
    .iProduct           = 0,
    .iSerialNumber      = 0,
    .bNumConfigurations = 1,
};

struct usb_config_descriptors me56ps2_config_descriptors = {
    .config = {
        .bLength             = USB_DT_CONFIG_SIZE,
        .bDescriptorType     = USB_DT_CONFIG,
        .wTotalLength        = __cpu_to_le16(sizeof(me56ps2_config_descriptors)),
        .bNumInterfaces      = 1,
        .bConfigurationValue = 1,
        .iConfiguration      = 0,
        .bmAttributes        = 0xC0,
        .bMaxPower           = 0xFA, // 500mA
    },
    .interface = {
        .bLength             = USB_DT_INTERFACE_SIZE,
        .bDescriptorType     = USB_DT_INTERFACE,
        .bInterfaceNumber    = 0,
        .bAlternateSetting   = 0,
        .bNumEndpoints       = 4,
        .bInterfaceClass     = 0xff, // Vendor Specific class
        .bInterfaceSubClass  = 0xff,
        .bInterfaceProtocol  = 0xff,
        .iInterface          = 0,
    },
    
/*
    .endpoint_bulk_in = {
        .bLength             = USB_DT_ENDPOINT_SIZE,
        .bDescriptorType     = USB_DT_ENDPOINT,
        .bEndpointAddress    = USB_DIR_IN | ENDPOINT_ADDR_BULK,
        .bmAttributes        = USB_ENDPOINT_XFER_BULK,
        .wMaxPacketSize      = MAX_PACKET_SIZE_BULK,
        .bInterval           = 0,
    },
    .endpoint_bulk_out = {
        .bLength             = USB_DT_ENDPOINT_SIZE,
        .bDescriptorType     = USB_DT_ENDPOINT,
        .bEndpointAddress    = USB_DIR_OUT | ENDPOINT_ADDR_BULK,
        .bmAttributes        = USB_ENDPOINT_XFER_BULK,
        .wMaxPacketSize      = MAX_PACKET_SIZE_BULK,
        .bInterval           = 0,
    }
*/

#if 1
    .endpoint_bulk_out = {
        .bLength             = USB_DT_ENDPOINT_SIZE,
        .bDescriptorType     = USB_DT_ENDPOINT,
        .bEndpointAddress    = 0x01,	//USB_DIR_OUT | ENDPOINT_ADDR_BULK,
        .bmAttributes        = USB_ENDPOINT_XFER_BULK,
        .wMaxPacketSize      = MAX_PACKET_SIZE_BULK,
        .bInterval           = 1,
    },
    .endpoint_bulk_in = {
        .bLength             = USB_DT_ENDPOINT_SIZE,
        .bDescriptorType     = USB_DT_ENDPOINT,
        .bEndpointAddress    = 0x82,	//USB_DIR_IN | ENDPOINT_ADDR_BULK,
        .bmAttributes        = USB_ENDPOINT_XFER_BULK,
        .wMaxPacketSize      = MAX_PACKET_SIZE_BULK,
        .bInterval           = 1,
    },
#endif

#if 1
    .endpoint_bulk_out2 = {
        .bLength             = USB_DT_ENDPOINT_SIZE,
        .bDescriptorType     = USB_DT_ENDPOINT,
        .bEndpointAddress    = 0x03,	//USB_DIR_OUT | ENDPOINT_ADDR_BULK,
        .bmAttributes        = USB_ENDPOINT_XFER_BULK,
        .wMaxPacketSize      = MAX_PACKET_SIZE_BULK,
        .bInterval           = 1,
    },
    .endpoint_bulk_in2 = {
        .bLength             = USB_DT_ENDPOINT_SIZE,
        .bDescriptorType     = USB_DT_ENDPOINT,
        .bEndpointAddress    = 0x84,	//USB_DIR_IN | ENDPOINT_ADDR_BULK,
        .bmAttributes        = USB_ENDPOINT_XFER_BULK,
        .wMaxPacketSize      = MAX_PACKET_SIZE_BULK,
        .bInterval           = 1,
    }
#endif
    
};

const void *me56ps2_string_descriptors[STRING_DESCRIPTORS_NUM] = {
    &me56ps2_string_descriptor_0,
    &me56ps2_string_descriptor_1,
    &me56ps2_string_descriptor_2,
    &me56ps2_string_descriptor_3,
};


extern int _do_reboot();
extern void set_broken_output( char on );

namespace fs = std::filesystem;

namespace nvr
{



	/*----------------------------------------------------------
	----------------------------------------------------------*/
	usb::usb()
	{
		printf( "usb::usb()" );
		
		thread_args.handle_out1 = -1;
		thread_args.handle_in1  = -1;
		thread_args.handle_out2 = -1;
		thread_args.handle_in2  = -1;
		
		SPDLOG_INFO("usb_raw_gadget");
		g_usb = new usb_raw_gadget("/dev/raw-gadget");
		//usb->set_debug_level(debug_level);
		SPDLOG_INFO("init");
		g_usb->init(USB_SPEED_HIGH, driver, device);
		g_usb->reset_eps();
		SPDLOG_INFO("run");
		g_usb->run();
	}
	
	/*----------------------------------------------------------
	----------------------------------------------------------*/
	usb::~usb()
	{
		
	}
	
	/*----------------------------------------------------------
	----------------------------------------------------------*/
    void usb::main_proc( void* arg )
    {
    	usb* u = (usb*)arg;
		u->event_usb_control_loop();
    }

	/*----------------------------------------------------------
	----------------------------------------------------------*/
	void usb::debug_dump_data( const char* data, int length )
	{
		int i;
		
		for( i = 0; i < length; i++ )
		{
			printf( "%02x ", data[i] & 0xff );
			
			if( i % 16 == 15 )
			{
				printf( "\n" );
			}
		}
		
		if( i % 16 != 0 )
		{
			printf( "\n" );
		}
	}

	/*----------------------------------------------------------
	----------------------------------------------------------*/
	int usb::send_bulk( struct io_thread_args* p_thread_args, int ep_handle, char* data, int len )
	{
		printf( "### send_bulk() ep_handle %d ###\n", ep_handle );
		
		int ret = 0;
		
		signal( SIGCHLD, SIG_IGN );
		
		
		// プロセスを複製し子プロセスを作成
		pid_t pid = fork();
		SPDLOG_INFO( " pid = {}", pid );
		
		if( pid < 0 )
		{
			// プロセスの複製失敗
			//
			//SPDLOG_ERROR( "Failed to fork process: {}", strerror( errno ) );
			SPDLOG_INFO( "Failed to fork process: {}", strerror( errno ) );
			return -1;
		} 
		else
		if( pid == 0 )
		{
			// 子プロセス処理
			//
			SPDLOG_INFO( " child pid = {}", pid );
			
			int size = len;
			char* p = data;
			
			while( size > 0 )
			{
				len = size;
			
				struct usb_raw_ep_io_data io_data;
				
				io_data.inner.ep    = ep_handle;
				io_data.inner.flags = 0;
				
				if( len > sizeof( io_data.data ) )
				{
				 	len = sizeof( io_data.data );
				}
				
				io_data.inner.length = len;
				memcpy( &io_data.data[0], p, len );
				
				ret = p_thread_args->usb->ep_write( (struct usb_raw_ep_io*)&io_data );
				printf( "send_bulk() ep_write() ret=%d\n", ret );
				
				if( ret < 0 )
				{
					break;
				}
				
				size -= ret;
				p += ret;
			}
			
			//子プロセスの終了
			exit( -1 );
		}
		
		
		return ret;
	}

	/*----------------------------------------------------------
	----------------------------------------------------------*/
	int usb::recv_bulk( struct io_thread_args* p_thread_args, int ep_handle, char* data, int len )
	{
		printf( "### recv_bulk() ep_handle %d ###\n", ep_handle );
		
		struct usb_raw_ep_io_data io_data;
		int ret = 0;
		
		io_data.inner.ep    = ep_handle;
		io_data.inner.flags = 0;
		
		if( len > sizeof( io_data.data ) )
		{
			len = sizeof( io_data.data );
		}
		io_data.inner.length = len;
		
		printf( "call recv_bulk() \n" );
		
		ret = p_thread_args->usb->ep_read( (struct usb_raw_ep_io*)&io_data );
		
		printf( "recv_bulk() ep_read(as) ret=%d\n", ret );
		debug_dump_data( &io_data.data[0], io_data.inner.length );
		
		memcpy( data, &io_data.data[0], ret );
		
		return ret;
	}
	
	/*----------------------------------------------------------
	----------------------------------------------------------*/
	void usb::on_cmd_system_reset( struct io_thread_args* p_thread_args, int rcv_len )
	{
		int len = 0;
		char sts[5];
		sts[0] = 4;						// 4:OK
		memcpy( &sts[1], &len, 4 );		// 応答データ長
		
		// ステータス応答
		send_bulk( p_thread_args, p_thread_args->handle_in1, sts, 5 );
	
	
		// 300ms wait
		std::this_thread::sleep_for( std::chrono::milliseconds( 300 ) );
		
		// reset
		_do_reboot();
	}
	
	/*----------------------------------------------------------
	----------------------------------------------------------*/
	void usb::on_cmd_load_info( struct io_thread_args* p_thread_args, int rcv_len )
	{
		char req[rcv_len];
		recv_bulk( p_thread_args, p_thread_args->handle_out2, req, rcv_len );
		
		int file_no;
		memcpy( &file_no, &req[0], 4 );
		
		char path[2048];
		sprintf( path, "/mnt/sd/EVC/REC_INF%d.dat", file_no ); 

		int len = 0;
		char ret = 4;			// 4:OK, 3:NG
		char *dat = NULL;
		try
		{
			// REC_INF.DATからリード
			std::ifstream ifs( (const char*)path, std::ios::binary );

			ifs.seekg( 0, std::ios::end );
			long long int size = ifs.tellg();
			ifs.seekg( 0 );

			dat = new char[size];
			ifs.read( dat, size );
			
			len = size;
		}
		catch( const std::exception& e )
		{
			ret = 3;		// 3:NG
		}
		
		char* res = dat;
		
		// 送信準備
		if( len > 0 )
		{
			send_bulk( p_thread_args, p_thread_args->handle_in2, res, len );
		}
		
		delete dat;
		
		
		char sts[5];
		sts[0] = ret;					// ret
		memcpy( &sts[1], &len, 4 );		// 応答データ長
		
		// ステータス応答
		send_bulk( p_thread_args, p_thread_args->handle_in1, sts, 5 );
	}
	
	/*----------------------------------------------------------
	----------------------------------------------------------*/
	int usb::read_inf1_count()
	{
		char path[2048];
		strcpy( path, "/mnt/sd/EVC/REC_INF1.dat" ); 

		char* dat = NULL;
		long long int size = 0;
		
		try
		{
			// REC_INF.DATからリード
			std::ifstream ifs( (const char*)path, std::ios::binary );

			ifs.seekg( 0, std::ios::end );
			size = ifs.tellg();
			ifs.seekg( 0 );

			dat = new char[size];
			ifs.read( dat, size );
		}
		catch( const std::exception& e )
		{
			return -1;
		}
		
		int count = 0;
		
		for( int i = 0; i < size; i++ )
		{
			char c = dat[i];
			
			if( (c >= '0') && (c <= '9') )
			{
				count << 8;
				count |= (c - 0x30);
			}
		}
		
		delete dat;
		
		return count;
	}
	
	/*----------------------------------------------------------
	----------------------------------------------------------*/
	int usb::get_rec_data_path( int file_no, char* path )
	{
		int count = read_inf1_count();
		
		if( count <= 0 )
		{
			return -1;
		}
		
		if( count > 999999 )
		{
			int dir1 = file_no / 1000000;
			int dir2 = file_no / 10000;
			int dir3 = file_no / 100;
			
			sprintf( path, "/mnt/sd/EVC/DIR1_%02d/DIR2_%02d/DIR3_%02d/JP%06d.DAT", 
					 dir1, dir2, dir3, file_no ); 
		}
		else
		{
			int dir1 = file_no / 10000;
			int dir2 = file_no / 100;
			
			sprintf( path, "/mnt/sd/EVC/DIR1_%02d/DIR2_%02d/JP%06d.DAT", 
					 dir1, dir2, file_no ); 
		}
		
		return strlen( path );
	}
	
	/*----------------------------------------------------------
	----------------------------------------------------------*/
	void usb::on_cmd_load_rec_data( struct io_thread_args* p_thread_args, int rcv_len )
	{
		char req[rcv_len];
		recv_bulk( p_thread_args, p_thread_args->handle_out2, req, rcv_len );
		
		int file_no;
		memcpy( &file_no, &req[0], 4 );
		
		char ret = 4;			// 4:OK, 3:NG
		int len = 0;
		
		char path[2048];
		if( get_rec_data_path( file_no, path ) > 0 )
		{
			char *dat = NULL;
			try
			{
				// JPEG fileからリード
				std::ifstream ifs( (const char*)path, std::ios::binary );

				ifs.seekg( 0, std::ios::end );
				long long int size = ifs.tellg();
				ifs.seekg( 0 );

				dat = new char[size];
				ifs.read( dat, size );
				
				len = size;
			}
			catch( const std::exception& e )
			{
				ret = 3;		// 3:NG
			}
			
			char* res = dat;
		
			
			// 送信準備
			if( len > 0 )
			{
				send_bulk( p_thread_args, p_thread_args->handle_in2, res, len );
			}
			
			delete dat;
		}
		else
		{
			ret = 3;		// 3:NG
		}
		
		
		char sts[5];
		sts[0] = ret;					// 4:OK, 3:NG
		memcpy( &sts[1], &len, 4 );		// 応答データ長
		
		// ステータス応答
		send_bulk( p_thread_args, p_thread_args->handle_in1, sts, 5 );
	}
	
	/*----------------------------------------------------------
	----------------------------------------------------------*/
	void usb::on_cmd_load_rec_date( struct io_thread_args* p_thread_args, int rcv_len )
	{
		char req[rcv_len];
		recv_bulk( p_thread_args, p_thread_args->handle_out2, req, rcv_len );
		
		int file_no;
		memcpy( &file_no, &req[0], 4 );
		
		char ret = 4;			// 4:OK, 3:NG
		int len = 0;
		
		char path[2048];
		long long int size;
		if( get_rec_data_path( file_no, path ) > 0 )
		{
			char *dat = NULL;
			try
			{
				// JPEG fileからリード
				std::ifstream ifs( (const char*)path, std::ios::binary );

				ifs.seekg( 0, std::ios::end );
				size = ifs.tellg();
				ifs.seekg( 0 );

				dat = new char[size];
				ifs.read( dat, size );
			}
			catch( const std::exception& e )
			{
				ret = 3;		// 3:NG
			}
			
			if( size > 380 )
			{
				len = 19;
				char res[19];
				
				memcpy( res, &dat[287], 19 );
				
				delete dat;
				
				// 送信準備
				send_bulk( p_thread_args, p_thread_args->handle_in2, res, len );
			}
		}
		else
		{
			ret = 3;		// 3:NG
		}
		
		
		char sts[5];
		sts[0] = ret;					// 4:OK, 3:NG
		memcpy( &sts[1], &len, 4 );		// 応答データ長
		
		// ステータス応答
		send_bulk( p_thread_args, p_thread_args->handle_in1, sts, 5 );
	}
	
	/*----------------------------------------------------------
	----------------------------------------------------------*/
	void usb::on_cmd_load_rec_interval( struct io_thread_args* p_thread_args, int rcv_len )
	{
		char req[rcv_len];
		recv_bulk( p_thread_args, p_thread_args->handle_out2, req, rcv_len );
		
		int file_no;
		memcpy( &file_no, &req[0], 4 );
		
		char ret = 4;			// 4:OK, 3:NG
		int len = 0;
		
		char path[2048];
		long long int size;
		if( get_rec_data_path( file_no, path ) > 0 )
		{
			char *dat = NULL;
			try
			{
				// JPEG fileからリード
				std::ifstream ifs( (const char*)path, std::ios::binary );

				ifs.seekg( 0, std::ios::end );
				size = ifs.tellg();
				ifs.seekg( 0 );

				dat = new char[size];
				ifs.read( dat, size );
			}
			catch( const std::exception& e )
			{
				ret = 3;		// 3:NG
			}
			
			if( size > 380 )
			{
				// JPEG file rec interval(338B~4B)からリード
				len = 4;
				char res[4];
				
				// exif[338]~[341] ("0000" 100ms ASCII)
				memcpy( res, &dat[338], 4 );
				
				delete dat;
				
				// 送信準備
				send_bulk( p_thread_args, p_thread_args->handle_in2, res, len );
			}
		}
		else
		{
			ret = 3;		// 3:NG
		}
		
		
		char sts[5];
		sts[0] = ret;					// 4:OK, 3:NG
		memcpy( &sts[1], &len, 4 );		// 応答データ長
		
		// ステータス応答
		send_bulk( p_thread_args, p_thread_args->handle_in1, sts, 5 );
	}
	
	/*----------------------------------------------------------
	----------------------------------------------------------*/
	void usb::on_cmd_check_folder_dir1( struct io_thread_args* p_thread_args, int rcv_len )
	{
		char req[rcv_len];
		recv_bulk( p_thread_args, p_thread_args->handle_out2, req, rcv_len );
		
		int dir1;
		memcpy( &dir1, &req[0], 2 );
		
		// exist dir1
		int len = 0;
		char ret;

		char path[2048];
		sprintf( path, "/mnt/sd/EVC/DIR1_%02d/", dir1 ); 
		fs::path fs_path( path );
		std::error_code ec;

		// ディレクトリの存在確認
		if( fs::exists( fs_path, ec ) )
		{
			ret = 4;	// 4:OK
		}
		else
		{
			ret = 3;	// 3:NG
		}
		
		char sts[5];
		sts[0] = ret;					// ret
		memcpy( &sts[1], &len, 4 );		// 応答データ長
		
		// ステータス応答
		send_bulk( p_thread_args, p_thread_args->handle_in1, sts, 5 );
	}
	
	/*----------------------------------------------------------
	----------------------------------------------------------*/
	void usb::on_cmd_check_folder_dir12( struct io_thread_args* p_thread_args, int rcv_len )
	{
		char req[rcv_len];
		recv_bulk( p_thread_args, p_thread_args->handle_out2, req, rcv_len );
		
		int dir1;
		memcpy( &dir1, &req[0], 2 );
		
		int dir2;
		memcpy( &dir2, &req[2], 2 );
		
		// exist dir1 & dir2
		int len = 0;
		char ret;

		char path[2048];
		sprintf( path, "/mnt/sd/EVC/DIR1_%02d/DIR2_%02d/", dir1, dir2 ); 
		fs::path fs_path( path );
		std::error_code ec;

		// ディレクトリの存在確認
		if( fs::exists( fs_path, ec ) )
		{
			ret = 4;	// 4:OK
		}
		else
		{
			ret = 3;	// 3:NG
		}
		
		char sts[5];
		sts[0] = ret;					// ret
		memcpy( &sts[1], &len, 4 );		// 応答データ長
		
		// ステータス応答
		send_bulk( p_thread_args, p_thread_args->handle_in1, sts, 5 );
	}
	
	/*----------------------------------------------------------
	----------------------------------------------------------*/
	void usb::on_cmd_check_folder_dir123( struct io_thread_args* p_thread_args, int rcv_len )
	{
		char req[rcv_len];
		recv_bulk( p_thread_args, p_thread_args->handle_out2, req, rcv_len );
		
		int dir1;
		memcpy( &dir1, &req[0], 2 );
		
		int dir2;
		memcpy( &dir2, &req[2], 2 );
		
		int dir3;
		memcpy( &dir3, &req[4], 2 );
		
		// exist dir1 & dir2 & dir3
		int len = 0;
		char ret;

		char path[2048];
		sprintf( path, "/mnt/sd/EVC/DIR1_%02d/DIR2_%02d/DIR3_%02d/", dir1, dir2, dir3 ); 
		fs::path fs_path( path );
		std::error_code ec;

		// ディレクトリの存在確認
		if( fs::exists( fs_path, ec ) )
		{
			ret = 4;	// 4:OK
		}
		else
		{
			ret = 3;	// 3:NG
		}
		
		char sts[5];
		sts[0] = ret;					// ret
		memcpy( &sts[1], &len, 4 );		// 応答データ長
		
		// ステータス応答
		send_bulk( p_thread_args, p_thread_args->handle_in1, sts, 5 );
	}
	
	/*----------------------------------------------------------
	----------------------------------------------------------*/
	void usb::on_cmd_set_rtc( struct io_thread_args* p_thread_args, int rcv_len )
	{
		char req[rcv_len];
		recv_bulk( p_thread_args, p_thread_args->handle_out2, req, rcv_len );
		
		// BCD
		int YY = req[0];
		int MM = req[1];
		int DD = req[2];
		int hh = req[3];
		int mm = req[4];
		int ss = req[5];
		
		int len = 0;
		char ret;
		
		char buf[32];
		sprintf( buf, "date -s \"20%d%d-%d%d-%d%d %d%d:%d%d:%d%d\"", 
				 (YY >> 4) & 0x0F, YY & 0x0F, 
				 (MM >> 4) & 0x0F, MM & 0x0F, 
				 (DD >> 4) & 0x0F, DD & 0x0F, 
				 (hh >> 4) & 0x0F, hh & 0x0F, 
				 (mm >> 4) & 0x0F, mm & 0x0F, 
				 (ss >> 4) & 0x0F, ss & 0x0F );
		
		// set DATE
		int rt = system( buf );
		printf( "system( %s ) rt=%d \n", buf, rt );
		if( rt >= 0 )
		{
			rt = system( "hwclock --hctosys" );
			printf( "system( \"hwclock --hctosys\" ) rt=%d \n", rt );
			if( rt >= 0 )
			{
				ret = 4;	// 4:OK
			}
			else
			{
				ret = 3;	// 3:NG
			}
		}
		else
		{
			ret = 3;	// 3:NG
		}
		
		char sts[5];
		sts[0] = ret;					// ret
		memcpy( &sts[1], &len, 4 );		// 応答データ長
		
		// ステータス応答
		send_bulk( p_thread_args, p_thread_args->handle_in1, sts, 5 );
	}
	
	/*----------------------------------------------------------
	----------------------------------------------------------*/
	void usb::on_cmd_get_rtc( struct io_thread_args* p_thread_args, int rcv_len )
	{
		auto tp = std::chrono::system_clock::now();
		auto duration = tp.time_since_epoch();
		auto msec = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
		long int sec1 = msec / 1000;
		struct tm* lt = localtime( &sec1 );
		int Y = lt->tm_year % 100;	// 年 [1900からの経過年数] : (Y + 1900) % 100 = Y
		int M = lt->tm_mon + 1;		// 月 [0-11] 0から始まることに注意
		int D = lt->tm_mday;		// 日 [1-31]
		int h = lt->tm_hour;		// 時 [0-23]
		int m = lt->tm_min;			// 分 [0-59]
		int s = lt->tm_sec;			// 秒 [0-61] 最大2秒までのうるう秒を考慮

		int len = 6;
		char res[6];
		res[0] = ((Y / 10) << 4) | (Y % 10);	// YY(BCD)
		res[1] = ((M / 10) << 4) | (M % 10);	// MM(BCD)
		res[2] = ((D / 10) << 4) | (D % 10);	// DD(BCD)
		res[3] = ((h / 10) << 4) | (h % 10);	// hh(BCD)
		res[4] = ((m / 10) << 4) | (m % 10);	// mm(BCD)
		res[5] = ((s / 10) << 4) | (s % 10);	// ss(BCD)
		

		// 日時の送信準備
		send_bulk( p_thread_args, p_thread_args->handle_in2, res, len );
		
		
		char sts[5];
		sts[0] = 4;						// 4:OK
		memcpy( &sts[1], &len, 4 );		// 応答データ長
		
		// ステータス応答
		send_bulk( p_thread_args, p_thread_args->handle_in1, sts, 5 );
	}
	
	/*----------------------------------------------------------
	----------------------------------------------------------*/
	void usb::on_cmd_set_eeprom( struct io_thread_args* p_thread_args, int rcv_len )
	{
		char req[rcv_len];
		recv_bulk( p_thread_args, p_thread_args->handle_out2, req, rcv_len );
		
		int addr = ((int)req[1] << 8) | req[0];
		int size = ((int)req[3] << 8) | req[2];
		char* dat = &req[4];
		
		/// todo ////////////////////////////////
		// save EEPROM
		
		int len = 0;
		char sts[5];
		sts[0] = 4;						// 4:OK
		memcpy( &sts[1], &len, 4 );		// 応答データ長
		
		// ステータス応答
		send_bulk( p_thread_args, p_thread_args->handle_in1, sts, 5 );
	}
	
	/*----------------------------------------------------------
	----------------------------------------------------------*/
	void usb::on_cmd_get_eeprom( struct io_thread_args* p_thread_args, int rcv_len )
	{
		char req[rcv_len];
		recv_bulk( p_thread_args, p_thread_args->handle_out2, req, rcv_len );
		
		int addr = ((int)req[1] << 8) | req[0];
		int size = ((int)req[3] << 8) | req[2];;
		
		
		// EEPROMからリード
		char dat[size];
		memset( dat, 0, size );
		
		
		
		int len = size;
		char* res = dat;
		
		// eepromの内容の送信準備
		send_bulk( p_thread_args, p_thread_args->handle_in2, res, len );
		
		
		char sts[5];
		sts[0] = 4;						// 4:OK
		memcpy( &sts[1], &len, 4 );		// 応答データ長
		
		// ステータス応答
		send_bulk( p_thread_args, p_thread_args->handle_in1, sts, 5 );
	}

	/*----------------------------------------------------------
	----------------------------------------------------------*/
	void usb::on_cmd_get_ver( struct io_thread_args* p_thread_args, int rcv_len )
	{
		int len = 3;
		char res[3];
		//res[0] = '1';	// X(ASCII)
		//res[1] = '.';	// .(ASCII)
		//res[2] = '0';	// X(ASCII)
		
		memcpy( res, STR_VER, 3 );
		
		// 日時の送信準備
		send_bulk( p_thread_args, p_thread_args->handle_in2, res, len );
		
		
		char sts[5];
		sts[0] = 4;						// 4:OK
		memcpy( &sts[1], &len, 4 );		// 応答データ長
		
		// ステータス応答
		send_bulk( p_thread_args, p_thread_args->handle_in1, sts, 5 );
	}
	
	/*----------------------------------------------------------
	----------------------------------------------------------*/
	void usb::on_cmd_update_fw( struct io_thread_args* p_thread_args, int rcv_len )
	{
		char req[rcv_len];
		recv_bulk( p_thread_args, p_thread_args->handle_out2, req, rcv_len );
		
		char* dat = &req[4];
		
		/// todo ////////////////////////////////
		// save EEPROM
		
		int len = 0;
		char sts[5];
		sts[0] = 3;						// 3:NG
		memcpy( &sts[1], &len, 4 );		// 応答データ長
		
		// ステータス応答
		send_bulk( p_thread_args, p_thread_args->handle_in1, sts, 5 );
	}
	
	/*----------------------------------------------------------
	----------------------------------------------------------*/
	void usb::on_cmd_broken_output_test( struct io_thread_args* p_thread_args, int rcv_len )
	{
		char req[rcv_len];
		recv_bulk( p_thread_args, p_thread_args->handle_out2, req, rcv_len );
		
		char dat = req[0];
		
		// set Broken outout
		set_broken_output( dat );		// 0:normal, 1:broken Test
		
		int len = 0;
		char sts[5];
		sts[0] = 4;						// 4:OK
		memcpy( &sts[1], &len, 4 );		// 応答データ長
		
		// ステータス応答
		send_bulk( p_thread_args, p_thread_args->handle_in1, sts, 5 );
	}
	
	/*----------------------------------------------------------
	----------------------------------------------------------*/
	void usb::on_cmd_unknown( struct io_thread_args* p_thread_args, int rcv_len )
	{
		char req[rcv_len];
		recv_bulk( p_thread_args, p_thread_args->handle_out2, req, rcv_len );
		
		int len = 0;
		char sts[5];
		sts[0] = 0;						// 0:unknown cmd, frame err
		memcpy( &sts[1], &len, 4 );		// 応答データ長
		
		// ステータス応答
		send_bulk( p_thread_args, p_thread_args->handle_in1, sts, 5 );
	}
	
	/*----------------------------------------------------------
	----------------------------------------------------------*/
	void* usb::bulk_thread( void* arg )
	{
		usb* u = (usb*)arg; 
		return u->_bulk_thread();
	}
	
	/*----------------------------------------------------------
	----------------------------------------------------------*/
	void* usb::_bulk_thread()
	{
		printf( "\n\n\n\n\n***  bulk_thread() start**\n" );
		
		
		struct io_thread_args* p_thread_args = (struct io_thread_args*)&thread_args;
		
		char buf[128];
		int len;
		struct usb_raw_ep_io_data io_data;
		int ret;
		
		
		while( 1 )
		{
			printf( "  call recv_bulk() start\n" );
		
			ret = recv_bulk( p_thread_args, p_thread_args->handle_out1, buf, 5 );
			
			printf( "  call recv_bulk() end ret=%d\n", ret );
			
			if( ret < 0 )
			{
				break;
			}
			
			if( ret != 5 )
			{
				continue;
			}
			
			int cmd = buf[0];
			int rcv_len;
			memcpy( &rcv_len, &buf[1], 4 );
			
			switch( cmd )
			{
			case  0:	on_cmd_system_reset( p_thread_args, rcv_len );			break;
			case  1:	on_cmd_load_info( p_thread_args, rcv_len );				break;
			case  2:	on_cmd_load_rec_data( p_thread_args, rcv_len );			break;
			case  3:	on_cmd_load_rec_date( p_thread_args, rcv_len );			break;
			case  4:	on_cmd_load_rec_interval( p_thread_args, rcv_len );		break;
			case  5:	on_cmd_check_folder_dir1( p_thread_args, rcv_len );		break;
			case  6:	on_cmd_check_folder_dir12( p_thread_args, rcv_len );	break;
			case 13:	on_cmd_check_folder_dir123( p_thread_args, rcv_len );	break;
			case  7:	on_cmd_set_rtc( p_thread_args, rcv_len );				break;
			case  8:	on_cmd_get_rtc( p_thread_args, rcv_len );				break;
			case  9:	on_cmd_set_eeprom( p_thread_args, rcv_len );			break;
			case 10:	on_cmd_get_eeprom( p_thread_args, rcv_len );			break;
			case 11:	on_cmd_get_ver( p_thread_args, rcv_len );				break;
			case 12:	on_cmd_update_fw( p_thread_args, rcv_len );				break;
			case 14:	on_cmd_broken_output_test( p_thread_args, rcv_len );	break;
			default:	on_cmd_unknown( p_thread_args, rcv_len );				break;
			}
		}
		
		printf( "\n***  bulk_thread() end ***\n" );
		set_broken_output( 0 );		// 0:normal, 1:broken Test
		
		return NULL;
	}

	/***********************************************************
	***********************************************************/
	/*----------------------------------------------------------
	----------------------------------------------------------*/
	bool usb::process_control_packet(usb_raw_gadget *usb, usb_raw_control_event *e, struct usb_packet_control *pkt)
	{
		if( e->is_event( USB_TYPE_STANDARD, USB_REQ_GET_DESCRIPTOR ) )
		{
			const auto descriptor_type = e->get_descriptor_type();
			
			if( descriptor_type == USB_DT_DEVICE )
			{
				memcpy(pkt->data, &me56ps2_device_descriptor, sizeof(me56ps2_device_descriptor));
				pkt->header.length = sizeof(me56ps2_device_descriptor);
				return true;
			}
			
			if( descriptor_type == USB_DT_CONFIG )
			{
				memcpy(pkt->data, &me56ps2_config_descriptors, sizeof(me56ps2_config_descriptors));
				pkt->header.length = sizeof(me56ps2_config_descriptors);
				return true;
			}
			
			if( descriptor_type == USB_DT_OTHER_SPEED_CONFIG )
			{
				pkt->header.length = 0;
				return true;
				
				//pkt->data[0] = 0x09;
				//pkt->data[1] = 0x04;
				//pkt->data[2] = 0x00;
				//pkt->data[3] = 0x00;
				//pkt->data[4] = 0x04;
				//pkt->data[5] = 0x00;
				//pkt->data[6] = 0x00;
				//pkt->data[7] = 0x00;
				//pkt->data[8] = 0x00;
				//pkt->data[9] = 0x09; 
				//pkt->header.length = 10;
				//return true;
			}
			
			
			if( descriptor_type == USB_DT_STRING )
			{
				/*
				const auto id = e->ctrl.wValue & 0x00ff;
				if (id >= STRING_DESCRIPTORS_NUM) {return false;} // invalid string id
				const auto len = reinterpret_cast<const struct _usb_string_descriptor<1> *>(me56ps2_string_descriptors[id])->bLength;
				memcpy(pkt->data, me56ps2_string_descriptors[id], len);
				pkt->header.length = len;
				return true;
				*/
				
				pkt->header.length = 0;
				return true;
				
				//pkt->data[0] = 0xFF;
				//pkt->data[1] = 0x00;
				//pkt->data[2] = 0x07;
				//pkt->data[3] = 0x05;
				//pkt->data[4] = 0x01;
				//pkt->data[5] = 0x02;
				//pkt->data[6] = 0x40;
				//pkt->data[7] = 0x00;
				//pkt->data[8] = 0xFF;
				//pkt->data[9] = 0x07;
				//pkt->header.length = 10;
				//return true;
			}
			
			if( descriptor_type == USB_DT_BOS )
			{
				pkt->header.length = 0;		//descs->bos_len;
				return true;
				
				//*pkt->data         = 0x00;	//descs->bos;
				//pkt->header.length = 1;		//descs->bos_len;
				//return true;
				
				//pkt->data[0] = 0xFF;
				//pkt->data[1] = 0x00;
				//pkt->data[2] = 0x07;
				//pkt->data[3] = 0x05;
				//pkt->data[4] = 0x01;
				//pkt->data[5] = 0x02;
				//pkt->data[6] = 0x00;
				//pkt->data[7] = 0x02;
				//pkt->data[8] = 0x01;
				//pkt->data[9] = 0x07;
				//pkt->header.length = 10;
				//return true;
			}
			
			if( descriptor_type == USB_DT_DEVICE_QUALIFIER )
			{
				//if( !descs->qual )
				{
					// Fill in DEVICE_QUALIFIER based on DEVICE if not provided.
					struct usb_qualifier_descriptor* qual = (struct usb_qualifier_descriptor*)pkt->data;
					struct usb_device_descriptor* dev = &me56ps2_device_descriptor;
					qual->bLength            = sizeof(*qual);
					qual->bDescriptorType    = USB_DT_DEVICE_QUALIFIER;
					qual->bcdUSB             = dev->bcdUSB;
					qual->bDeviceClass       = dev->bDeviceClass;
					qual->bDeviceSubClass    = dev->bDeviceSubClass;
					qual->bDeviceProtocol    = dev->bDeviceProtocol;
					qual->bMaxPacketSize0    = dev->bMaxPacketSize0;
					qual->bNumConfigurations = dev->bNumConfigurations;
					qual->bRESERVED          = 0;
					
					pkt->header.length = sizeof(*qual);
					return true;
				}
			}
			
			
			printf( "descriptor_type=%d\n ", descriptor_type );
		}
		
		if( e->is_event(USB_TYPE_STANDARD, USB_REQ_SET_CONFIGURATION) )
		{
			usb->vbus_draw(me56ps2_config_descriptors.config.bMaxPower);
			usb->configure();
			
			SPDLOG_INFO("ep_enable");
			usb->reset_eps();
				
			//for( int i = 0; i < 3; i++ )
			//if( flag == 0 )
			{
				//flag = 1;
				
				//usb->ep_disable(  );
				
				
				//SPDLOG_INFO("ep_enable skip");
				
				///*
				if( thread_args.handle_in1 >= 0 )
				{
					usb->ep_disable( thread_args.handle_in1 );
					thread_args.handle_in1 = -1;
				}
				{
					printf( "\n\n\n\n*** call usb->ep_enable() in1 ***\n\n\n\n" );
					int ep_num_bulk_in1 = usb->ep_enable( reinterpret_cast<struct usb_endpoint_descriptor *>(&me56ps2_config_descriptors.endpoint_bulk_in) );
					if( ep_num_bulk_in1 >= 0 )
					{
						printf( "\n\n\n\n*** enable ep_num_bulk_in1 ***\n\n\n\n" );
						thread_args.handle_in1 = ep_num_bulk_in1;
					}
					else
					{
						printf( "\n\n\n\n***   failed usb->ep_enable() in1  ret=%d  errno=%d ***\n\n\n\n", ep_num_bulk_in1, errno );
					}
				}
				
				if( thread_args.handle_out1 >= 0 )
				{
					usb->ep_disable( thread_args.handle_out1 );
					thread_args.handle_out1 = -1;
				}
				{
					printf( "\n\n\n\n*** call usb->ep_enable() out1 ***\n\n\n\n" );
					int ep_num_bulk_out1 = usb->ep_enable( reinterpret_cast<struct usb_endpoint_descriptor *>(&me56ps2_config_descriptors.endpoint_bulk_out) );
					if( ep_num_bulk_out1 >= 0 )
					{
						printf( "\n\n\n\n*** enable ep_num_bulk_out1 ***\n\n\n\n" );
						thread_args.handle_out1 = ep_num_bulk_out1;
					}
					else
					{
						printf( "\n\n\n\n***   failed usb->ep_enable() out1  vret=%d  errno=%d ***\n\n\n\n", ep_num_bulk_out1, errno );
					}
				}
				 
				if( thread_args.handle_in2 >= 0 )
				{
					usb->ep_disable( thread_args.handle_in2 );
					thread_args.handle_in2 = -1;
				}
				{
					printf( "\n\n\n\n*** call usb->ep_enable() in2 ***\n\n\n\n" );
					int ep_num_bulk_in2 = usb->ep_enable( reinterpret_cast<struct usb_endpoint_descriptor *>(&me56ps2_config_descriptors.endpoint_bulk_in2) );
					if( ep_num_bulk_in2 >= 0 )
					{
						printf( "\n\n\n\n*** enable ep_num_bulk_in2 ***\n\n\n\n" );
						thread_args.handle_in2 = ep_num_bulk_in2;
					}
					else
					{
						printf( "\n\n\n\n***   failed usb->ep_enable() in2  ret=%d  errno=%d ***\n\n\n\n", ep_num_bulk_in2, errno );
					}
				}
				
				if( thread_args.handle_out2 >= 0 )
				{
					usb->ep_disable( thread_args.handle_out2 );
					thread_args.handle_out2 = -1;
				}
				{
					printf( "\n\n\n\n*** call usb->ep_enable() out2 ***\n\n\n\n" );
					int ep_num_bulk_out2 = usb->ep_enable( reinterpret_cast<struct usb_endpoint_descriptor *>(&me56ps2_config_descriptors.endpoint_bulk_out2) );
					if( ep_num_bulk_out2 >= 0 )
					{
						printf( "\n\n\n\n*** enable ep_num_bulk_out2 ***\n\n\n\n" );
						thread_args.handle_out2 = ep_num_bulk_out2;
					}
					else
					{
						printf( "\n\n\n\n***   failed usb->ep_enable() out2  ret=%d  errno=%d ***\n\n\n\n", ep_num_bulk_out2, errno );
					}
				}
				//*/
				
				
				
			}
			
			
			if( (thread_args.handle_in1 >= 0) && 
				(thread_args.handle_out1 >= 0) && 
				(thread_args.handle_in2 >= 0) && 
				(thread_args.handle_out2 >= 0) )
			{
				thread_args.usb = usb;
		        //pthread_create( &threadr, NULL, bulk_thread, &thread_args );
		        pthread_create( &threadr, NULL, bulk_thread, this );
			}
			
			printf("USB configurated.\n");
			pkt->header.length = 0;
			return true;
		}
		
		if( e->is_event(USB_TYPE_STANDARD, USB_REQ_SET_INTERFACE) )
		{
			pkt->header.length = 0;
			return true;
		}
		
		if( e->is_event(USB_TYPE_VENDOR, 0x01) )
		{
			pkt->header.length = 0;
			SPDLOG_INFO("USB_TYPE_VENDOR001");
			return true;
		}
		
		if( e->is_event(USB_TYPE_VENDOR) )
		{
			pkt->header.length = 0;
			SPDLOG_INFO("USB_TYPE_VENDOR");
			return true;
		}
		
		printf("process_control_packet() end. type=%d req=%d \n", e->get_request_type(), e->get_request() );
		
		pkt->header.length = 0;
		return true;
		//return false;
	}

	/*----------------------------------------------------------
	----------------------------------------------------------*/
	bool usb::event_usb_control_loop()
	{
		SPDLOG_INFO("while Start");
		while( !th_end )
		{
			usb_raw_control_event e;
			e.event.type = 0;
			e.event.length = sizeof(e.ctrl);

			struct usb_packet_control pkt;
			pkt.header.ep = 0;
			pkt.header.flags = 0;
			pkt.header.length = 0;

			g_usb->event_fetch(&e.event);
		 
			switch(e.event.type) {
				case USB_RAW_EVENT_CONNECT:
				    break;
				case USB_RAW_EVENT_CONTROL:
				    if (!process_control_packet(g_usb, &e, &pkt)) {
				        g_usb->ep0_stall();
				        break;
				    }

				    pkt.header.length = std::min(pkt.header.length, static_cast<unsigned int>(e.ctrl.wLength));
				    if (e.ctrl.bRequestType & USB_DIR_IN) {
				        g_usb->ep0_write(reinterpret_cast<struct usb_raw_ep_io *>(&pkt));
				    } else {
				        g_usb->ep0_read(reinterpret_cast<struct usb_raw_ep_io *>(&pkt));
				    }
				    break;
				default:
				    break;
			}
		}
		SPDLOG_INFO("while end");

		return true;
	}


}
