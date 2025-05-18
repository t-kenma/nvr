#ifndef __NVR_USB_HPP__
#define __NVR_USB_HPP__

#include <linux/usb/raw_gadget.h>
#include "usb_raw_gadget.hpp"
#include "usb_raw_control_event.hpp"
#include "usb_evc.hpp"
#include "eeprom.hpp"

struct io_thread_args
{
	unsigned stop;
	usb_raw_gadget* usb;
	int handle_in1;
	int handle_out1;
	int handle_in2;
	int handle_out2;
};

namespace nvr
{
    class usb
    {
    public:
        usb( std::shared_ptr<nvr::eeprom> eeprom );
        ~usb();
        
        static void main_proc( std::shared_ptr<nvr::usb> arg );
		bool event_usb_control_loop();

		static void* bulk_thread( void* arg );
		void* _bulk_thread();

		int th_end = 0;
		
		bool connected = false;
    
    private:
		void debug_dump_data( const char* data, int length );
		int send_bulk( struct io_thread_args* p_thread_args, int ep_handle, char* data, int len );
		int recv_bulk( struct io_thread_args* p_thread_args, int ep_handle, char* data, int len );
		void on_cmd_load_info( struct io_thread_args* p_thread_args, int rcv_len );
		void on_cmd_system_reset( struct io_thread_args* p_thread_args, int rcv_len );
		int read_inf1_count();
		int get_rec_data_path( int file_no, char* path );
		void on_cmd_load_rec_data( struct io_thread_args* p_thread_args, int rcv_len );
		void on_cmd_load_rec_date( struct io_thread_args* p_thread_args, int rcv_len );
		void on_cmd_load_rec_interval( struct io_thread_args* p_thread_args, int rcv_len );
		void on_cmd_check_folder_dir1( struct io_thread_args* p_thread_args, int rcv_len );
		void on_cmd_check_folder_dir12( struct io_thread_args* p_thread_args, int rcv_len );
		void on_cmd_check_folder_dir123( struct io_thread_args* p_thread_args, int rcv_len );
		void on_cmd_set_rtc( struct io_thread_args* p_thread_args, int rcv_len );
		void on_cmd_get_rtc( struct io_thread_args* p_thread_args, int rcv_len );
		void on_cmd_set_eeprom( struct io_thread_args* p_thread_args, int rcv_len );
		void on_cmd_get_eeprom( struct io_thread_args* p_thread_args, int rcv_len );
		void on_cmd_get_ver( struct io_thread_args* p_thread_args, int rcv_len );
		void on_cmd_update_fw( struct io_thread_args* p_thread_args, int rcv_len );
		void on_cmd_broken_output_test( struct io_thread_args* p_thread_args, int rcv_len );
		void on_cmd_unknown( struct io_thread_args* p_thread_args, int rcv_len );

		bool process_control_packet(usb_raw_gadget *usb, usb_raw_control_event *e, struct usb_packet_control *pkt);

		std::shared_ptr<nvr::eeprom> eeprom_;

		const char *driver = USB_RAW_GADGET_DRIVER_DEFAULT;
		const char *device = USB_RAW_GADGET_DEVICE_DEFAULT;
		
		usb_raw_gadget *g_usb = NULL;

		struct io_thread_args thread_args;

		#define USB_MAX_PACKET_SIZE 4096

		struct usb_raw_ep_io_data
		{
			struct usb_raw_ep_io inner;
			char data[USB_MAX_PACKET_SIZE];
		};

		pthread_t threadr;
    };
}
#endif
