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
#include <sys/wait.h>

#ifdef G_OS_UNIX
#include <glib-unix.h>
#endif

#include "logging.hpp"

#include <signal.h>

#include "common.hpp"

#include <math.h>



#define STR_VER		"0.3"



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
extern void set_broken_output( bool on );

namespace fs = std::filesystem;

namespace nvr
{



	/*----------------------------------------------------------
	----------------------------------------------------------*/
	usb::usb( std::shared_ptr<nvr::eeprom> eeprom, std::shared_ptr<nvr::logger> logger )
		: eeprom_( eeprom )
	{
//		printf( "usb::usb()" );
		
		thread_args.handle_out1 = -1;
		thread_args.handle_in1  = -1;
		thread_args.handle_out2 = -1;
		thread_args.handle_in2  = -1;
		
//		SPDLOG_INFO("usb_raw_gadget");
		g_usb = new usb_raw_gadget("/dev/raw-gadget");
		//usb->set_debug_level(debug_level);
//		SPDLOG_INFO("init");
		g_usb->init(USB_SPEED_HIGH, driver, device);
//		SPDLOG_INFO("run");
		connected = false;
		
		logger_ = logger;
		g_usb->run();
	}
	
	/*----------------------------------------------------------
	----------------------------------------------------------*/
	usb::~usb()
	{
		
	}
	
	/*----------------------------------------------------------
	----------------------------------------------------------*/
    void usb::main_proc( std::shared_ptr<nvr::usb> arg )
    {
    	std::shared_ptr<nvr::usb> u = arg;
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
		int ret = 0;
		int send_len = 0;
		char* p = data;
				
		while( len > 0 )
		{
			struct usb_raw_ep_io_data io_data;
			
			io_data.inner.ep    = ep_handle;
			io_data.inner.flags = 0;
			io_data.inner.length = len;
			
			if( io_data.inner.length > sizeof( io_data.data ) )
			{
				io_data.inner.length = sizeof( io_data.data );
			}
			
			memcpy( &io_data.data[0], p, io_data.inner.length );
			
			ret = p_thread_args->usb->ep_write( (struct usb_raw_ep_io*)&io_data );
			
			if( ret < 0 )
			{
				break;
			}
			
			p += ret;
			len -= ret;
			send_len += ret;
		}
			
		return send_len;
	}

	/*----------------------------------------------------------
	----------------------------------------------------------*/
	int usb::recv_bulk( struct io_thread_args* p_thread_args, int ep_handle, char* data, int len )
	{
		return recv_bulk_ex( p_thread_args, ep_handle, data, len, false );
	}
	/*----------------------------------------------------------
	----------------------------------------------------------*/
	int usb::recv_bulk_ex( struct io_thread_args* p_thread_args, int ep_handle, char* data, int len, bool one )
	{
		char* p = data;
		int ret = 0;
		int recv_len = 0;
		
//		printf( "### recv_bulk() ep_handle %d ### in\n", ep_handle );
		
		while( len > 0 )
		{
			struct usb_raw_ep_io_data io_data;
			
			io_data.inner.ep    = ep_handle;
			io_data.inner.flags = 0;
			io_data.inner.length = len;
			
			if( io_data.inner.length > sizeof( io_data.data ) )
			{
				io_data.inner.length = sizeof( io_data.data );
			}
			
//			printf( "recv_bulk() ep_read() ep_handle=%d len=%d\n", ep_handle, len );
			ret = p_thread_args->usb->ep_read( (struct usb_raw_ep_io*)&io_data );
//			printf( "recv_bulk() ep_read() ep_handle=%d ret=%d\n", ep_handle, ret );
			
			if( ret < 0 )
			{
				return ret;
			}
						
			memcpy( p, &io_data.data[0], ret );
			
			p += ret;
			len -= ret;
			recv_len += ret;
			
			if( one )
			{
				break;
			}
		}
		
//		printf( "### recv_bulk() ep_handle %d ### out\n", ep_handle );
		
		return recv_len;
	}
	
	/*----------------------------------------------------------
	----------------------------------------------------------*/
	void usb::bulk_send_thread( void* arg )
	{
//		printf( "### bulk_send_thread() ### in\n" );
		
		nvr::usb::bulk_send_arg_t* p_bulk_send_arg = (nvr::usb::bulk_send_arg_t*)arg;
		
		
		p_bulk_send_arg->p_usb->send_bulk( p_bulk_send_arg->p_thread_args, 
										 p_bulk_send_arg->ep_handle, 
										 p_bulk_send_arg->data, 
										 p_bulk_send_arg->len );
										 
										 
		
//		printf( "### bulk_send_thread() ### out\n" );
	}
	
	
	/*----------------------------------------------------------
	----------------------------------------------------------*/
	void usb::on_cmd_system_reset( struct io_thread_args* p_thread_args, int rcv_len )
	{		
		// 300ms wait
		std::this_thread::sleep_for( std::chrono::milliseconds( 300 ) );
		
		// reset
		_do_reboot();
	}
	
	/*----------------------------------------------------------
	----------------------------------------------------------*/
	void usb::on_cmd_load_info( struct io_thread_args* p_thread_args, int rcv_len )
	{
		
		recv_bulk( p_thread_args, p_thread_args->handle_out2, req_buf, rcv_len );
		
		int file_no = req_buf[3]<<24 |req_buf[2]<<16 |req_buf[1]<<8 | req_buf[0];

		
		char path[2048];
		sprintf( path, "/mnt/sd/EVC/REC_INF%d.dat", file_no ); 
		
		int len = 0;
		char ret = 4;			// 4:OK, 3:NG
		char *dat = NULL;
		try
		{
			usb_sd_access = true;
			
			// REC_INF.DATからリード
			std::ifstream ifs( (const char*)path, std::ios::binary );

			ifs.seekg( 0, std::ios::end );
			long long int size = ifs.tellg();
			ifs.seekg( 0 );
			
			dat = new char[size];
			ifs.read( dat, size );
						
			len = (size < 64) ? size : 64;
			
			len = size;
		}
		catch( const std::exception& e )
		{
			ret = 3;		// 3:NG
		}
		
		char* res = dat;
		// 送信準備
		std::thread send_th;
		if( len > 0 )
		{
			
			bulk_send_arg_t bulk_send_arg;
			bulk_send_arg.p_usb = this;
			bulk_send_arg.p_thread_args = p_thread_args;
			bulk_send_arg.ep_handle = p_thread_args->handle_in2;
			bulk_send_arg.data = res;
			bulk_send_arg.len = len;
			
			send_th = std::thread( bulk_send_thread, &bulk_send_arg );
		}
		
		
		
		
		char sts[5];
		sts[0] = ret;					// ret
		memcpy( &sts[1], &len, 4 );		// 応答データ長
		
		/*
		printf( "res = %02X %02X %02X %02X %02X \n", 
				sts[0], sts[1], sts[2], sts[3], sts[4] );
		
		*/
		
		// ステータス応答
		send_bulk( p_thread_args, p_thread_args->handle_in1, sts, 5 );
		
		if( len > 0 )
		{
			if( send_th.joinable() )
			{
				send_th.join();
			}
		}
		
		delete dat;
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
			usb_sd_access = true;

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
				
		int count = atoi(dat);
				
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
			int dir1 = (file_no / 1000000) % 100; 
			int dir2 = (file_no / 10000  ) % 100; 
			int dir3 = (file_no / 100    ) % 100; 
			
			sprintf( path, "/mnt/sd/EVC/DIR1_%02d/DIR2_%02d/DIR3_%02d/JP%08d.DAT", 
					 dir1, dir2, dir3, file_no ); 
		}
		else
		{
			int dir1 = (file_no / 10000) % 100;    
			int dir2 = (file_no / 100  ) % 100;   
			
			sprintf( path, "/mnt/sd/EVC/DIR1_%02d/DIR2_%02d/JP%06d.DAT", 
					 dir1, dir2, file_no ); 
		}
		
		SPDLOG_INFO("count = {}",count);
		SPDLOG_INFO("get_rec_data_path = {}",path);
		
		return strlen( path );
	}
	
	/*----------------------------------------------------------
	----------------------------------------------------------*/
	void usb::on_cmd_load_rec_data( struct io_thread_args* p_thread_args, int rcv_len )
	{
		recv_bulk( p_thread_args, p_thread_args->handle_out2, req_buf, rcv_len );
		
		int file_no = req_buf[3]<<24 |req_buf[2]<<16 |req_buf[1]<<8 | req_buf[0];

		
		char ret = 4;			// 4:OK, 3:NG
		int len = 0;
		
		char path[2048];
		std::thread send_th;
		char *dat = NULL;
		
		if( get_rec_data_path( file_no, path ) > 0 )
		{
			try
			{
				usb_sd_access = true;

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
				bulk_send_arg_t bulk_send_arg;
				bulk_send_arg.p_usb = this;
				bulk_send_arg.p_thread_args = p_thread_args;
				bulk_send_arg.ep_handle = p_thread_args->handle_in2;
				bulk_send_arg.data = res;
				bulk_send_arg.len = len;
				
				send_th = std::thread( bulk_send_thread, &bulk_send_arg );
			}			
		}
		else
		{
			ret = 3;		// 3:NG
		}
		
		
		// ステータス応答
		char sts[5];
		sts[0] = ret;					// 4:OK, 3:NG
		memcpy( &sts[1], &len, 4 );		// 応答データ長
		
		send_bulk( p_thread_args, p_thread_args->handle_in1, sts, 5 );
		
		if( len > 0 )
		{
			if( send_th.joinable() )
			{
				send_th.join();
			}
		}
		delete dat;
	}
	
	/*----------------------------------------------------------
	----------------------------------------------------------*/
	void usb::on_cmd_load_rec_date( struct io_thread_args* p_thread_args, int rcv_len )
	{
		recv_bulk( p_thread_args, p_thread_args->handle_out2, req_buf, rcv_len );
		
		int file_no = req_buf[3]<<24 |req_buf[2]<<16 |req_buf[1]<<8 | req_buf[0];
		
		char ret = 4;			// 4:OK, 3:NG
		int len = 0;
		
		char path[2048];
		long long int size;
		std::thread send_th;
		char *dat = NULL;
		if( get_rec_data_path( file_no, path ) > 0 )
		{
			try
			{
				usb_sd_access = true;
				
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
				
				// 送信準備
				bulk_send_arg_t bulk_send_arg;
				bulk_send_arg.p_usb = this;
				bulk_send_arg.p_thread_args = p_thread_args;
				bulk_send_arg.ep_handle = p_thread_args->handle_in2;
				bulk_send_arg.data = res;
				bulk_send_arg.len = len;
		
				send_th = std::thread( bulk_send_thread, &bulk_send_arg );				
			}
		}
		else
		{
			ret = 3;		// 3:NG
		}
		
		
		// ステータス応答
		char sts[5];
		sts[0] = ret;					// 4:OK, 3:NG
		memcpy( &sts[1], &len, 4 );		// 応答データ長
		
		send_bulk( p_thread_args, p_thread_args->handle_in1, sts, 5 );
		
		
		if( len > 0 )
		{
			if( send_th.joinable() )
			{
				send_th.join();
			}
		}
		delete dat;
	}
	
	/*----------------------------------------------------------
	----------------------------------------------------------*/
	void usb::on_cmd_load_rec_interval( struct io_thread_args* p_thread_args, int rcv_len )
	{
		recv_bulk( p_thread_args, p_thread_args->handle_out2, req_buf, rcv_len );
		
		int file_no = req_buf[3]<<24 |req_buf[2]<<16 |req_buf[1]<<8 | req_buf[0];
		
		char ret = 4;			// 4:OK, 3:NG
		int len = 0;
		
		char path[2048];
		long long int size;
		std::thread send_th;
		char *dat = NULL;
		if( get_rec_data_path( file_no, path ) > 0 )
		{
			try
			{
				usb_sd_access = true;
				
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
				
				
				// 送信準備
				bulk_send_arg_t bulk_send_arg;
				bulk_send_arg.p_usb = this;
				bulk_send_arg.p_thread_args = p_thread_args;
				bulk_send_arg.ep_handle = p_thread_args->handle_in2;
				bulk_send_arg.data = res;
				bulk_send_arg.len = len;
				
				send_th = std::thread( bulk_send_thread, &bulk_send_arg );
			}
		}
		else
		{
			ret = 3;		// 3:NG
		}
		
		
		// ステータス応答
		char sts[5];
		sts[0] = ret;					// 4:OK, 3:NG
		memcpy( &sts[1], &len, 4 );		// 応答データ長
		
		send_bulk( p_thread_args, p_thread_args->handle_in1, sts, 5 );
		
		if( len > 0 )
		{
			if( send_th.joinable() )
			{
				send_th.join();
			}
		}
		
		delete dat;
	}
	
	/*----------------------------------------------------------
	----------------------------------------------------------*/
	void usb::on_cmd_check_folder_dir1( struct io_thread_args* p_thread_args, int rcv_len )
	{
		recv_bulk( p_thread_args, p_thread_args->handle_out2, req_buf, rcv_len );
		
		int dir1 = req_buf[1]<<8 | req_buf[0];
		
		usb_sd_access = true;
		
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
				
		// ステータス応答
		char sts[5];
		sts[0] = ret;					// ret
		memcpy( &sts[1], &len, 4 );		// 応答データ長
		
		send_bulk( p_thread_args, p_thread_args->handle_in1, sts, 5 );
	}
	
	/*----------------------------------------------------------
	----------------------------------------------------------*/
	void usb::on_cmd_check_folder_dir12( struct io_thread_args* p_thread_args, int rcv_len )
	{
		recv_bulk( p_thread_args, p_thread_args->handle_out2, req_buf, rcv_len );
		
		usb_sd_access = true;
		
		int dir1 = req_buf[1]<<8 | req_buf[0];
		int dir2 = req_buf[3]<<8 | req_buf[2];
				
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
		
		// ステータス応答
		char sts[5];
		sts[0] = ret;					// ret
		memcpy( &sts[1], &len, 4 );		// 応答データ長
		
		send_bulk( p_thread_args, p_thread_args->handle_in1, sts, 5 );
	}
	
	/*----------------------------------------------------------
	----------------------------------------------------------*/
	void usb::on_cmd_check_folder_dir123( struct io_thread_args* p_thread_args, int rcv_len )
	{
		recv_bulk( p_thread_args, p_thread_args->handle_out2, req_buf, rcv_len );
		
		usb_sd_access = true;
		
		int dir1 = req_buf[1]<<8 | req_buf[0];
		int dir2 = req_buf[3]<<8 | req_buf[2];
		int dir3 = req_buf[5]<<8 | req_buf[4];
		
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
				
		// ステータス応答
		char sts[5];
		sts[0] = ret;					// ret
		memcpy( &sts[1], &len, 4 );		// 応答データ長
		
		send_bulk( p_thread_args, p_thread_args->handle_in1, sts, 5 );
	}
	
	/*----------------------------------------------------------
	----------------------------------------------------------*/
	void usb::on_cmd_set_rtc( struct io_thread_args* p_thread_args, int rcv_len )
	{
		recv_bulk( p_thread_args, p_thread_args->handle_out2, req_buf, rcv_len );
		
		// BCD
		int YY = req_buf[0];
		int MM = req_buf[1]; 
		int DD = req_buf[2];
		int hh = req_buf[3];
		int mm = req_buf[4];
		int ss = req_buf[5];
		
		int len = 0;
		char ret;
		
		char buf[32];
		
		//---調時（旧時刻） ログ
		//
		logger_->LogOut( 5 );
		
		
		sprintf( buf, "date -s \"20%02X/%02X/%02X %02X:%02X:%02X\"", 
				 YY, MM, DD, hh, mm, ss );
		
		// set DATE
		signal(SIGCHLD, SIG_DFL);
		int rt = system( buf );
		
		time_t tme = time( NULL );
		struct tm* _tm = localtime( &tme ); 
		
		unsigned char Y = ::nvr::BcdToByte( YY ); 
		unsigned char M = ::nvr::BcdToByte( MM );
		unsigned char D = ::nvr::BcdToByte( DD );
		unsigned char h = ::nvr::BcdToByte( hh );
		unsigned char m = ::nvr::BcdToByte( mm );
		unsigned char s = ::nvr::BcdToByte( ss );
		
		//if( rt >= 0 )
		if( (Y == _tm->tm_year % 100) && 	// 年 [1900からの経過年数]
			(M == _tm->tm_mon + 1)    && 	// 月 [0-11] 0から始まることに注意
			(D == _tm->tm_mday)		  && 	// 日 [1-31]
			(h == _tm->tm_hour)		  && 	// 時 [0-23]
			(m == _tm->tm_min)		  && 	// 分 [0-59]
			(s == _tm->tm_sec) )			// 秒 [0-61] 最大2秒までのうるう秒を考慮
		{
			signal(SIGCHLD, SIG_DFL);
			rt = system( "hwclock --systohc" );
//			printf( "system( \"hwclock --systohc\" ) rt=%d \n", rt );
			if( rt >= 0 )
			{
				//---調時（新時刻） ログ
				//
				logger_->LogOut( 6 );
			
				ret = 4;	// 4:OK
			}
			else
			{
				//---RTCライトエラー ログ
				//
				logger_->LogOut( 17 );
				
				ret = 3;	// 3:NG
			}
		}
		else
		{
			//---RTCライトエラー ログ
			//
			logger_->LogOut( 17 );

			ret = 3;	// 3:NG
		}
		
		
		// ステータス応答
		char sts[5];
		sts[0] = ret;					// ret
		memcpy( &sts[1], &len, 4 );		// 応答データ長
		
		send_bulk( p_thread_args, p_thread_args->handle_in1, sts, 5 );
	}
	
	/*----------------------------------------------------------
	----------------------------------------------------------*/
	void usb::on_cmd_get_rtc( struct io_thread_args* p_thread_args, int rcv_len )
	{
		time_t tme = time( NULL );
		struct tm* _tm = localtime( &tme ); 

		unsigned char Y = _tm->tm_year % 100;	// 年 [1900からの経過年数] : (Y + 1900) % 100 = Y
		unsigned char M = _tm->tm_mon + 1;		// 月 [0-11] 0から始まることに注意
		unsigned char D = _tm->tm_mday;			// 日 [1-31]
		unsigned char h = _tm->tm_hour;			// 時 [0-23]
		unsigned char m = _tm->tm_min;			// 分 [0-59]
		unsigned char s = _tm->tm_sec;			// 秒 [0-61] 最大2秒までのうるう秒を考慮

		int len = 6;
		char res[6];
		res[0] = ::nvr::ByteToBcd( Y );	// YY(BCD)
		res[1] = ::nvr::ByteToBcd( M );	// MM(BCD)
		res[2] = ::nvr::ByteToBcd( D );	// DD(BCD)
		res[3] = ::nvr::ByteToBcd( h );	// hh(BCD)
		res[4] = ::nvr::ByteToBcd( m );	// mm(BCD)
		res[5] = ::nvr::ByteToBcd( s );	// ss(BCD)
		

		// 日時の送信準備
		bulk_send_arg_t bulk_send_arg;
		bulk_send_arg.p_usb = this;
		bulk_send_arg.p_thread_args = p_thread_args;
		bulk_send_arg.ep_handle = p_thread_args->handle_in2;
		bulk_send_arg.data = res;
		bulk_send_arg.len = len;
		
		std::thread send_th = std::thread( bulk_send_thread, &bulk_send_arg );
		
		
		// ステータス応答
		char sts[5];
		sts[0] = 4;						// 4:OK
		memcpy( &sts[1], &len, 4 );		// 応答データ長
		
		send_bulk( p_thread_args, p_thread_args->handle_in1, sts, 5 );
		
		
		if( send_th.joinable() )
		{
			send_th.join();
		}
	}
	
	/*----------------------------------------------------------
	----------------------------------------------------------*/
	void usb::on_cmd_set_eeprom( struct io_thread_args* p_thread_args, int rcv_len )
	{
		recv_bulk( p_thread_args, p_thread_args->handle_out2, req_buf, rcv_len );
		
		int addr = ((int)req_buf[1] << 8) | req_buf[0];
		int size = ((int)req_buf[3] << 8) | req_buf[2];
		char* dat = &req_buf[4];
//		printf( "set_eeprom() addr=0x%x size=%d \n", addr, size );
		if( addr == 0x21 )
		{
			size = rcv_len - 4;
//			printf( "  rcv_len=%d - 4 = size=%d \n", rcv_len, size );
		}
		rcv_len -= 4;
		
		int ret_eep = 0;
		
		if( rcv_len > 0 )
		{
			ret_eep = eeprom_->Write( addr, rcv_len, (unsigned char*)dat );
			
			addr += rcv_len;
			size -= rcv_len;
		}
		
		
		while( size > 0 )
		{
			rcv_len = size;
			if( rcv_len	> 512 )
			{
				rcv_len = 512;
			}
			
			int rcved = recv_bulk( p_thread_args, p_thread_args->handle_out2, req_buf, rcv_len );
			if( rcved < 0 )
			{
				ret_eep = -1;
				break;
			}
			
			dat = req_buf;
			
			if( ret_eep == 0 )
			{
				ret_eep = eeprom_->Write( addr, rcved, (unsigned char*)dat );
			}
			
			addr += rcved;
			size -= rcved;
		}
		
		// save EEPROM
		char ret;
		if( ret_eep >= 0 )
		{
			ret = 4;
		}
		else
		{
			ret = 3;
		}
		
		
		// ステータス応答
		int len = 0;
		char sts[5];
		sts[0] = ret;					// 4:OK
		memcpy( &sts[1], &len, 4 );		// 応答データ長
	
		send_bulk( p_thread_args, p_thread_args->handle_in1, sts, 5 );
	}
	
	/*----------------------------------------------------------
	----------------------------------------------------------*/
	void usb::bulk_send_eeprom_thread( void* arg )
	{		
		nvr::usb::bulk_send_eeprom_arg_t* p_bulk_send_arg = (nvr::usb::bulk_send_eeprom_arg_t*)arg;
		
		int addr = p_bulk_send_arg->addr;
		int size = p_bulk_send_arg->size;
		
		int send_len = 0;
		int ret_eep = 0;
		int len = size;
		
		int rd_len;
		while( size > 0 )
		{
			rd_len = size;
			if( rd_len > USB_MAX_PACKET_SIZE )
			{
				rd_len = USB_MAX_PACKET_SIZE;
			}
			
			struct usb_raw_ep_io_data io_data;
			
			// EEPROMからリード
			ret_eep = p_bulk_send_arg->p_eeprom->Read( addr, rd_len, (unsigned char*)io_data.data );
						
			// eepromの内容の送信準備
			
			io_data.inner.ep    = p_bulk_send_arg->p_thread_args->handle_in2;
			io_data.inner.flags = 0;
			io_data.inner.length = rd_len;
			
			if( io_data.inner.length > sizeof( io_data.data ) )
			{
				io_data.inner.length = sizeof( io_data.data );
			}
						
			int snded = p_bulk_send_arg->p_thread_args->usb->ep_write( (struct usb_raw_ep_io*)&io_data );
			
			if( snded < 0 )
			{
				break;
			}
			
			send_len += snded;
			addr += snded;
			size -= snded;
		}
	}
	/*----------------------------------------------------------
	----------------------------------------------------------*/
	void usb::on_cmd_get_eeprom( struct io_thread_args* p_thread_args, int rcv_len, unsigned short* last_addr )
	{
		recv_bulk( p_thread_args, p_thread_args->handle_out2, req_buf, rcv_len );
		
		int addr = ((int)req_buf[1] << 8) | req_buf[0];
		int size = ((int)req_buf[3] << 8) | req_buf[2];
		
		*last_addr = addr;
		 		
		bulk_send_eeprom_arg_t bulk_send_eeprom_arg;
		bulk_send_eeprom_arg.p_usb = this;
		bulk_send_eeprom_arg.p_thread_args = p_thread_args;
		bulk_send_eeprom_arg.ep_handle = p_thread_args->handle_in2;
		bulk_send_eeprom_arg.p_eeprom = eeprom_;
		bulk_send_eeprom_arg.addr = addr;
		bulk_send_eeprom_arg.size = size;
		
		std::thread send_th = std::thread( bulk_send_eeprom_thread, &bulk_send_eeprom_arg );
		
		
		usleep( 5000 );
		
		
		// ステータス応答
		int len = size;
		char sts[5];
		sts[0] = 4;					// 4:OK
		memcpy( &sts[1], &len, 4 );		// 応答データ長
				
		send_bulk( p_thread_args, p_thread_args->handle_in1, sts, 5 );
		
		
		if( send_th.joinable() )
		{
			send_th.join();
		}
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
		bulk_send_arg_t bulk_send_arg;
		bulk_send_arg.p_usb = this;
		bulk_send_arg.p_thread_args = p_thread_args;
		bulk_send_arg.ep_handle = p_thread_args->handle_in2;
		bulk_send_arg.data = res;
		bulk_send_arg.len = len;
		
		std::thread send_th = std::thread( bulk_send_thread, &bulk_send_arg );
		
		
		// ステータス応答
		char sts[5];
		sts[0] = 4;						// 4:OK
		memcpy( &sts[1], &len, 4 );		// 応答データ長
		
		send_bulk( p_thread_args, p_thread_args->handle_in1, sts, 5 );
		
		
		if( send_th.joinable() )
		{
			send_th.join();
		}
	}
	
	/*----------------------------------------------------------
	----------------------------------------------------------*/
	void usb::on_cmd_update_fw( struct io_thread_args* p_thread_args, int rcv_len )
	{
		recv_bulk( p_thread_args, p_thread_args->handle_out2, req_buf, rcv_len );
		
		char* dat = &req_buf[4];
		
		
		// ステータス応答
		int len = 0;
		char sts[5];
		sts[0] = 3;						// 3:NG
		memcpy( &sts[1], &len, 4 );		// 応答データ長
		
		send_bulk( p_thread_args, p_thread_args->handle_in1, sts, 5 );
	}
	
	/*----------------------------------------------------------
	----------------------------------------------------------*/
	void usb::on_cmd_broken_output_test( struct io_thread_args* p_thread_args, int rcv_len )
	{
		recv_bulk( p_thread_args, p_thread_args->handle_out2, req_buf, rcv_len );
		
		char dat = req_buf[0];
		
		// set Broken outout
		set_broken_output( dat != 0 );		// 0:normal, 1:broken Test
		
		int len = 0;
		char sts[5];
		sts[0] = 4;							// 4:OK
		memcpy( &sts[1], &len, 4 );			// 応答データ長
		
		// ステータス応答
		//send_bulk( p_thread_args, p_thread_args->handle_in1, sts, 5 );
	}
	
	/*----------------------------------------------------------
	----------------------------------------------------------*/
	void usb::on_cmd_unknown( struct io_thread_args* p_thread_args, int rcv_len )
	{
		recv_bulk( p_thread_args, p_thread_args->handle_out2, req_buf, rcv_len );
		
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
		connected = true;
		
		
		//---USBｹｰﾌﾞﾙ挿入 ログ
		//
		logger_->LogOut( 4 );
		
		struct io_thread_args* p_thread_args = (struct io_thread_args*)&thread_args;
		
		unsigned char buf[128];
		int len;
		int ret;
		
		unsigned char last_cmd = 0xFF;
		unsigned short last_addr = 0x0000;
		
		while( 1 )
		{
			ret = recv_bulk_ex( p_thread_args, p_thread_args->handle_out1, (char*)buf, 128, true );
			
			if( ret < 0 )
			{
				break;
			}
			
			
			unsigned char* p = buf;
			
			while( ret >= 5 )
			{
				unsigned char cmd = p[0];
				int rcv_len;
				memcpy( &rcv_len, &p[1], 4 );
				
				if( (cmd == 11) && 
					((last_cmd != 10) || (last_addr != 0x21)) )
				{
					break;
				}
				
				switch( cmd )
				{
				case  0:	on_cmd_system_reset( p_thread_args, rcv_len );				break;
				case  1:	on_cmd_load_info( p_thread_args, rcv_len );					break;
				case  2:	on_cmd_load_rec_data( p_thread_args, rcv_len );				break;
				case  3:	on_cmd_load_rec_date( p_thread_args, rcv_len );				break;
				case  4:	on_cmd_load_rec_interval( p_thread_args, rcv_len );			break;
				case  5:	on_cmd_check_folder_dir1( p_thread_args, rcv_len );			break;
				case  6:	on_cmd_check_folder_dir12( p_thread_args, rcv_len );		break;
				case 13:	on_cmd_check_folder_dir123( p_thread_args, rcv_len );		break;
				case  7:	on_cmd_set_rtc( p_thread_args, rcv_len );					break;
				case  8:	on_cmd_get_rtc( p_thread_args, rcv_len );					break;
				case  9:	on_cmd_set_eeprom( p_thread_args, rcv_len );				break;
				case 10:	on_cmd_get_eeprom( p_thread_args, rcv_len, &last_addr );	break;
				case 11:	on_cmd_get_ver( p_thread_args, rcv_len );					break;
				case 12:	on_cmd_update_fw( p_thread_args, rcv_len );					break;
				case 14:	on_cmd_broken_output_test( p_thread_args, rcv_len );		break;
				default:	on_cmd_unknown( p_thread_args, rcv_len );					break;
				}
				
				last_cmd = cmd;
				waitpid( -1, NULL, 0 );
				p += 5;
				ret -= 5;
			}
		}
		
		connected = false;
		
		
		//---USB取り外し ログ
		//
		logger_->LogOut( 13 );
		
		//set_broken_output( false );		// 0:normal, 1:broken Test
		
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
			}
			
			if( descriptor_type == USB_DT_BOS )
			{
				pkt->header.length = 0;			//descs->bos_len;
				return true;
				
				//*pkt->data         = 0x00;	//descs->bos;
				//pkt->header.length = 1;		//descs->bos_len;
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
			
			
//			printf( "descriptor_type=%d\n ", descriptor_type );
		}
		
		if( e->is_event(USB_TYPE_STANDARD, USB_REQ_SET_CONFIGURATION) )
		{
			usb->vbus_draw(me56ps2_config_descriptors.config.bMaxPower);
			usb->configure();
			
//			SPDLOG_INFO("ep_enable");
				
			if( thread_args.handle_in1 >= 0 )
			{
				usb->ep_disable( thread_args.handle_in1 );
				thread_args.handle_in1 = -1;
			}
			{
//				printf( "\n\n\n\n*** call usb->ep_enable() in1 ***\n\n\n\n" );
				int ep_num_bulk_in1 = usb->ep_enable( reinterpret_cast<struct usb_endpoint_descriptor *>(&me56ps2_config_descriptors.endpoint_bulk_in) );
				if( ep_num_bulk_in1 >= 0 )
				{
//					printf( "\n\n\n\n*** enable ep_num_bulk_in1 ***\n\n\n\n" );
					thread_args.handle_in1 = ep_num_bulk_in1;
				}
				else
				{
//					printf( "\n\n\n\n***   failed usb->ep_enable() in1  ret=%d  errno=%d ***\n\n\n\n", ep_num_bulk_in1, errno );
				}
			}
			
			if( thread_args.handle_out1 >= 0 )
			{
				usb->ep_disable( thread_args.handle_out1 );
				thread_args.handle_out1 = -1;
			}
			{
//				printf( "\n\n\n\n*** call usb->ep_enable() out1 ***\n\n\n\n" );
				int ep_num_bulk_out1 = usb->ep_enable( reinterpret_cast<struct usb_endpoint_descriptor *>(&me56ps2_config_descriptors.endpoint_bulk_out) );
				if( ep_num_bulk_out1 >= 0 )
				{
//					printf( "\n\n\n\n*** enable ep_num_bulk_out1 ***\n\n\n\n" );
					thread_args.handle_out1 = ep_num_bulk_out1;
				}
				else
				{
//					printf( "\n\n\n\n***   failed usb->ep_enable() out1  vret=%d  errno=%d ***\n\n\n\n", ep_num_bulk_out1, errno );
				}
			}
				
			if( thread_args.handle_in2 >= 0 )
			{
				usb->ep_disable( thread_args.handle_in2 );
				thread_args.handle_in2 = -1;
			}
			{
//				printf( "\n\n\n\n*** call usb->ep_enable() in2 ***\n\n\n\n" );
				int ep_num_bulk_in2 = usb->ep_enable( reinterpret_cast<struct usb_endpoint_descriptor *>(&me56ps2_config_descriptors.endpoint_bulk_in2) );
				if( ep_num_bulk_in2 >= 0 )
				{
//					printf( "\n\n\n\n*** enable ep_num_bulk_in2 ***\n\n\n\n" );
					thread_args.handle_in2 = ep_num_bulk_in2;
				}
				else
				{
//					printf( "\n\n\n\n***   failed usb->ep_enable() in2  ret=%d  errno=%d ***\n\n\n\n", ep_num_bulk_in2, errno );
				}
			}
			
			if( thread_args.handle_out2 >= 0 )
			{
				usb->ep_disable( thread_args.handle_out2 );
				thread_args.handle_out2 = -1;
			}
			{
//				printf( "\n\n\n\n*** call usb->ep_enable() out2 ***\n\n\n\n" );
				int ep_num_bulk_out2 = usb->ep_enable( reinterpret_cast<struct usb_endpoint_descriptor *>(&me56ps2_config_descriptors.endpoint_bulk_out2) );
				if( ep_num_bulk_out2 >= 0 )
				{
//					printf( "\n\n\n\n*** enable ep_num_bulk_out2 ***\n\n\n\n" );
					thread_args.handle_out2 = ep_num_bulk_out2;
				}
				else
				{
//					printf( "\n\n\n\n***   failed usb->ep_enable() out2  ret=%d  errno=%d ***\n\n\n\n", ep_num_bulk_out2, errno );
				}
			}
			
			
			if( (thread_args.handle_in1 >= 0) && 
				(thread_args.handle_out1 >= 0) && 
				(thread_args.handle_in2 >= 0) && 
				(thread_args.handle_out2 >= 0) )
			{
				thread_args.usb = usb;
		        pthread_create( &threadr, NULL, bulk_thread, this );
			}
			
//			printf("USB configurated.\n");
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
//			SPDLOG_INFO("USB_TYPE_VENDOR001");
			return true;
		}
		
		if( e->is_event(USB_TYPE_VENDOR) )
		{
			pkt->header.length = 0;
//			SPDLOG_INFO("USB_TYPE_VENDOR");
			return true;
		}
		
//		printf("process_control_packet() end. type=%d req=%d \n", e->get_request_type(), e->get_request() );
		
		pkt->header.length = 0;
		return true;
		//return false;
	}

	/*----------------------------------------------------------
	----------------------------------------------------------*/
	/*----------------------------------------------------------
	guint signal_user2_id;

	static gboolean signal_user2_cb(gpointer udata)
	{
		SPDLOG_INFO("USB:SIGUSER2 receiverd.");
		guint* data = static_cast<guint*>(udata);
		
	///	nvr::do_systemctl("restart", "systemd-networkd");
		
		return G_SOURCE_CONTINUE;
	}
	----------------------------------------------------------*/
	
	bool usb::event_usb_control_loop()
	{
///		signal_user2_id = g_unix_signal_add( SIGUSR2, G_SOURCE_FUNC(signal_user2_cb), &signal_user2_id );
		
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
			
			SPDLOG_INFO("event_fetch() in");
			g_usb->event_fetch(&e.event);
			SPDLOG_INFO("event_fetch() out");
			
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
		
		th_end = 2;

//		if( signal_user2_id )
//		{
//			g_source_remove( signal_user2_id );
//		}
		
		return true;
	}
}


