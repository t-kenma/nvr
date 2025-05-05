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
#include "config.hpp"
#include "pipeline.hpp"

#include "gpio.hpp"
#include "sd_manager.hpp"
#include "led_manager.hpp"
#include "util.hpp"
#include "logging.hpp"

#include <stdexcept>
#include <linux/usb/raw_gadget.h>

#include "usb_raw_gadget.hpp"
#include "usb_raw_control_event.hpp"

#include "usb_evc.hpp"
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
					    signal_int_id(0),
					    signal_term_id(0),
					    signal_user1_id(0),
					    signal_user2_id(0),
					    led(nullptr),
					    sd_manager(nullptr),
					    jpeg_file(nullptr),
					    pipeline(nullptr),
					    gpio_power(nullptr),
					    jpeg_time(0),
					    is_video_error(false),
					    b_reboot(false),
					    interrupted(false),
					    is_power_pin_high(false),
					    main_loop(nullptr)
	{}
	
	guint timer1_id;
	guint bus_watch_id;
	guint signal_int_id;
	guint signal_term_id;
	guint signal_user1_id;
	guint signal_user2_id;
	
	std::shared_ptr<nvr::pipeline> pipeline;
	std::shared_ptr<nvr::logger> logger;	
	nvr::led_manager *led;
	nvr::sd_manager *sd_manager;
	nvr::gpio_in *gpio_stop_btn;
	nvr::gpio_out *pwd_decoder;
	nvr::gpio_out *gpio_alrm_a;
	nvr::gpio_out *gpio_alrm_b;
	nvr::gpio_in *gpio_battery;
	nvr::gpio_in *pgood;
	std::filesystem::path done_dir;
	const char *jpeg_file;
	
	nvr::gpio_in *gpio_power;
	std::atomic<std::time_t> jpeg_time;
	bool is_video_error;
	bool b_reboot;
	std::atomic<bool> interrupted;
	std::atomic<bool> is_power_pin_high;
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

int flag = 0;
std::thread *thread_bulk_in = nullptr;
std::thread *thread_bulk_out = nullptr;


ring_buffer<char> usb_tx_buffer(524288);
std::atomic<bool> connected(false);

constexpr size_t BUF_SIZE = 512;


int count_btn_down = 0;
int count_rec_off = 0;
int count_sd_none = 0;

int count_video_lost = 0;
int count_video_found = 0;
bool system_error = false;
bool formatting = false;


/*-------後で設定ファイルに持っていく-------*/
int copy_interval = 100; 
uint8_t count_err = 0;
/*---------------------------------------*/
uint64_t test_idx = 0;

/***********************************************************
***********************************************************/

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
static void thread_gpio_power(callback_data_t *data)
{
	int fd = data->gpio_power->fd();
	int epfd = epoll_create1(EPOLL_CLOEXEC);
	struct epoll_event ev;
	struct epoll_event events;
	struct ifreq ifr;
	ev.events = EPOLLPRI;
	ev.data.fd = fd;
	
	
	if (epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev)) {
		SPDLOG_ERROR("Faild to add fd to epfs: {}", strerror(errno));
	}
	
	
	//---割り込みが来るまで実行
	//
	while (!data->interrupted.load(std::memory_order_relaxed)) 
	{
		if (!data->is_power_pin_high.load(std::memory_order_relaxed))
		{
			char buf[1];
			lseek(fd, 0, SEEK_SET);
			
			if (::read(fd, buf, 1) == 1)
			{
				SPDLOG_DEBUG("power pin :{}", buf[0]);
				
				if (buf[0] == '1') {
					data->is_power_pin_high.store(true, std::memory_order_relaxed);           
				}
			}
			else
			{
				SPDLOG_ERROR("Failed to read power pin: {}", strerror(errno));
			}
		}
		
		if (epoll_wait(epfd, &events, 1, 1000) > 0)
		{
			::open("/dev/adv71800", O_RDONLY);
			// ::open("/dev/adin0", O_RDONLY);
			// ioctl(soc, SIOCSIFFLAGS, &ifr);
			_do_reboot();
			return;
		}
	}
}

/*----------------------------------------------------------
----------------------------------------------------------*/
void addOrUpdateJPEGComment(const std::string& inputPath, const std::string& outputPath, const std::string& comment) {
    std::ifstream inFile(inputPath, std::ios::binary);
    if (!inFile) {
        std::cerr << "Failed to open input file\n";
        return;
    }

    std::vector<unsigned char> data((std::istreambuf_iterator<char>(inFile)), std::istreambuf_iterator<char>());
    inFile.close();

    //---JPEG開始マーカーを確認 (0xFFD8)
	//
    if (data.size() < 2 || data[0] != 0xFF || data[1] != 0xD8) {
        std::cerr << "Not a valid JPEG file.\n";
        return;
    }

    size_t pos = 2;
    bool commentWritten = false;

    //---新しいバッファに書き出す準備
	//
    std::vector<unsigned char> newData(data.begin(), data.begin() + 2); // SOIコピー

    // セグメントを順に処理
    while (pos + 4 < data.size() && data[pos] == 0xFF) {
        unsigned char marker = data[pos + 1];

        // EOIまたはSOSに達したら打ち切り
        if (marker == 0xD9 || marker == 0xDA) {
            break;
        }

        uint16_t length = (data[pos + 2] << 8) + data[pos + 3];
        if (pos + 2 + length > data.size()) break;

        // COMセグメントならスキップ（上書きのため）
        if (marker == 0xFE) {
            pos += 2 + length;  // スキップして書き込まない
            continue;
        }

        // 他のセグメントはそのままコピー
        newData.insert(newData.end(), data.begin() + pos, data.begin() + pos + 2 + length);
        pos += 2 + length;
    }

    // COMセグメント追加
    std::string cmt = comment;
    uint16_t comLen = cmt.length() + 2; // 2バイトは長さフィールド
    newData.push_back(0xFF);
    newData.push_back(0xFE); // COMマーカー
    newData.push_back((comLen >> 8) & 0xFF); // 長さ(上位)
    newData.push_back(comLen & 0xFF);        // 長さ(下位)
    newData.insert(newData.end(), cmt.begin(), cmt.end());

    // 残り（画像データ）を追加
    newData.insert(newData.end(), data.begin() + pos, data.end());

    // 書き出し
    std::ofstream outFile(outputPath, std::ios::binary);
    outFile.write(reinterpret_cast<char*>(newData.data()), newData.size());
    outFile.close();

    std::cout << "Comment added or updated successfully.\n";
}

/*----------------------------------------------------------
----------------------------------------------------------*/
/*
int copy(char* file_path)
{
	int rc;
	int fd = open(path, O_CREAT | O_WRONLY | O_TRUNC | O_CLOEXEC, S_IRWXU);
	if (fd == -1) {
		SPDLOG_ERROR("Failed to open {}: {}", path, strerror(errno));
		return -1;
	}



	rc = write(fd, val, len);
	if (rc < 0) {
		SPDLOG_ERROR("Failed to write {}: {}", path, strerror(errno));
		close(fd);
		return -2;
	}

	close(fd);

}
*/


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
int copy_jpeg( uint64_t file_idx )
//int copy_jpeg(gpointer udata)
{
	const std::filesystem::path inf2 = "/mnt/sd/REC_INF2.dat";
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

		save_path = fs::path("/mnt/sd") / dir1 / dir2 / dir3 ;
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
		save_path = fs::path("/mnt/sd") / dir1 / dir2;
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

	//--- jpegヘッダーの更新
	//
	std::string inputJPEG = "input.jpg";
    std::string outputJPEG = "output.jpg";
    std::string newComment = "これはテストコメントです";

    addOrUpdateJPEGComment("/tmp/_video.jpg", save_path.c_str(), newComment);

	/*
	//---ファイル保存
	//
	try 
	{
		save_path = save_path / g_strdup_printf( "JP%06lu.jpeg",file_idx);
										
		SPDLOG_INFO("path = {}",save_path.c_str());
		
		fs::copy_file("/tmp/_video.jpg", save_path, fs::copy_options::overwrite_existing);
		SPDLOG_INFO("jpeg save");
	}	
	catch (const fs::filesystem_error& e) 
	{
		std::cerr << "画像コピー失敗: " << e.what() << '\n';
	}
	*/


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

	
	//---inf2に対して書き込みファイル数を上書き
	//
	sprintf(num_file, "%lu", file_idx);
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
----------------------------------------------------------*/
static gboolean on_timer(gpointer udata)
{
	callback_data_t *data = static_cast<callback_data_t *>(udata);
	static int count_btn_down = 0;
	static int count_rec_off = 0;
	static int count_sd_none = 0;
	static int count_copy_interval = 0;
	
	
	SPDLOG_INFO("on_timer");

	/*
	//--- システムエラー状態
	//
	if( system_error == true )
	{
		led.set_r( BLINK );
		led.set_g( OFF );
		led.set_y( OFF );
		
		return;
	}
	
	
	//--- 映像入力異常 監視 ( 60秒映像断 )
	//
	if( is_video() != false )
	{
		if( count_video_lost > 60s )
		{
			count_video = 3s;
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
		led.set_r( BLINK );
		led.set_g( OFF );
		led.set_y( OFF );
		
		count_video_lost = 0;
		count_video_found--;
	}
	//--- sd card フォーマット中
	//
	else
	*/
	if( formatting == true )
	{
		data->led->set_r( data->low_battry  ? data->led->blink : data->led->off );
		//data->led->.set_g( is_usb_connenct() ? BLINK : BLINK_TWO );

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
			SPDLOG_INFO("on-timer 1");
			data->led->set_r( data->low_battry  ? data->led->blink : data->led->off );
			//led.set_g( is_usb_connenct() ? BLINK : BLINK_ONE );

			
			//--- SDカードチェック
			//
			if( data->sd_manager->is_sd_card() == false )
			{
				SPDLOG_INFO("on-timer 2");
				//--- SDカードの未挿入時間を計測開始
				//
				SPDLOG_INFO("いきなり抜かれた");
				count_sd_none = 600;  //60s
				data->sd_manager->unmount_sd();
			}
			
			
			//--- 録画停止ボタンチェック
			//
			SPDLOG_INFO("on-timer 3");
			if( is_btn(data) == true )
			{
			SPDLOG_INFO("on-timer 3-1");
				count_btn_down++;
				if( count_btn_down == 30 )
				{
				SPDLOG_INFO("on-timer 3-2");
					//--- 録画停止中へ移行
					//
					SPDLOG_INFO("録画停止");
					count_rec_off = 300; //30S
					data->sd_manager->unmount_sd();
				}
			}
			else
			{
				SPDLOG_INFO("on-timer 3-3");
				count_btn_down = 0;
			}

			
			//--- JPEGファイルコピー
			//
			SPDLOG_INFO("on-timer 4");			
			count_copy_interval++;
			if( count_copy_interval >= data->copy_interval )
			{
				SPDLOG_INFO("on-timer 4-1");
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
				SPDLOG_INFO("on-timer 4-2");
				data->led->set_y( data->led->off );
			}
		}
		//--- 録画停止中
		//
		else
		{
			SPDLOG_INFO("on-timer 5");
			//led.set_r( is_low_battery()  ? BLINK : OFF );
			//led.set_g( is_usb_connenct() ? BLINK : BLINK_ONE ); 仕組みがないよ
			data->led->set_y( data->led->off );
			
			count_btn_down = 0;
			count_copy_interval = 0;
			
			if( data->sd_manager->is_sd_card() ==false )
			{
			SPDLOG_INFO("on-timer 5-1");
				//--- SDカードの未挿入時間を計測開始
				//
				count_sd_none = 72000;
			}
			else
			{
			SPDLOG_INFO("on-timer 5-2");
				//--- 30秒経過で録画中に戻る
				//
				count_rec_off--;
			}
		}
	}
	//--- sd card なし
	//
	else
	{
		SPDLOG_INFO("on-timer 6");
		data->led->set_r( data->led->blink );
		data->led->set_g( data->led->off );
		data->led->set_y( data->led->off );
		
		count_rec_off = 0;
		count_btn_down = 0;
		count_copy_interval = 0;
		data->sd_manager->mount_sd();
		//--- sdカードが挿入された
		//
		if ( data->sd_manager->check_proc_mounts() == true )
		{
			SPDLOG_INFO("on-timer 6-1");
			count_sd_none = 0;
			SPDLOG_INFO("SD挿入");
//			data->sd_manager->mount_sd();
				
				
			//---sd card チェック
			//
			if( data->sd_manager->is_root_file_exists()  == false) {
				// なんならフォーマット
				//フォーマット中はどうする？
				SPDLOG_INFO("on-timer 6-2");
				SPDLOG_INFO("format start");				
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
			SPDLOG_INFO("on-timer 7");
			/*
			out_err on(); 
			IO.SET_1( LOW );
			IO.SET_2( LOW );
			*/
		}
		
		//--- SDカード未挿入時間カウント中
		//
		else
		{
		SPDLOG_INFO("on-timer 8");
			count_sd_none--;
		}
	}
	
	data->led->update_led();

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


#ifdef G_OS_UNIX
/*----------------------------------------------------------
----------------------------------------------------------*/
static gboolean callback_signal_user1(gpointer udata)
{
	SPDLOG_INFO("SIGUSER1 receiverd.");
	callback_data_t* data = static_cast<callback_data_t*>(udata);
	
	video_send_message(static_cast<GstElement*>(*data->pipeline));
	data->interrupted.store(true, std::memory_order_relaxed);
	data->b_reboot = true;
	
	data->logger->write("L リブート");
	/* remove signal handler */
	data->signal_user1_id = 0;
	return G_SOURCE_REMOVE;
}


/*----------------------------------------------------------
----------------------------------------------------------*/
static gboolean callback_signal_user2(gpointer udata)
{
	SPDLOG_INFO("SIGUSER2 receiverd.");
	callback_data_t* data = static_cast<callback_data_t*>(udata);
	
	return G_SOURCE_CONTINUE;
}


/*----------------------------------------------------------
----------------------------------------------------------*/
static gboolean callback_signal_intr(gpointer udata)
{
	SPDLOG_INFO("SIGINTR receiverd.");
	
	callback_data_t* data = static_cast<callback_data_t*>(udata);
	
	video_send_message(static_cast<GstElement*>(*data->pipeline));
	data->interrupted.store(true, std::memory_order_relaxed);
	/* remove signal handler */
	data->signal_int_id = 0;
	return G_SOURCE_REMOVE;
}


/*----------------------------------------------------------
----------------------------------------------------------*/
static gboolean callback_signal_term(gpointer udata)
{
	SPDLOG_INFO("SIGTERM receiverd.");
	
	callback_data_t* data = static_cast<callback_data_t*>(udata);
	
	video_send_message(static_cast<GstElement*>(*data->pipeline));
	data->interrupted.store(true, std::memory_order_relaxed);
	
	/* remove signal handler */
	data->signal_term_id = 0;
	return G_SOURCE_REMOVE;
}


#endif


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
static bool wait_power_pin(std::shared_ptr<nvr::gpio_in> pm, callback_data_t *data)
{
	sigset_t mask;
	sigset_t old_mask;
	
	bool ret = false;
	int epfd = -1;
	int sfd = -1;
	int fd = pm->fd();
	unsigned char buf[1];
	struct epoll_event ev;
	struct epoll_event events;
	bool first = true;
	
	sigemptyset (&mask);
	sigaddset (&mask, SIGINT);
	sigaddset (&mask, SIGTERM);
	sigaddset (&mask, SIGUSR1);
	sigaddset (&mask, SIGUSR2);
	
	/**********************************************************/
	//アップローダーに持っていく
	/**********************************************************/
	if (sigprocmask (SIG_BLOCK, &mask, nullptr) == -1) {
		SPDLOG_ERROR("Failed to sigprocmask: {}", strerror(errno));
		return ret;
	}
	
	sfd = signalfd (-1, &mask, SFD_NONBLOCK|SFD_CLOEXEC);
	if (sfd == -1){
		SPDLOG_ERROR("Failed to signalfd: {}", strerror(errno));
		goto END;
	}
	
	epfd = epoll_create1(EPOLL_CLOEXEC);
	if (epfd == -1) {
		SPDLOG_ERROR("Failed to epoll_create1: {}", strerror(errno));
		goto END;
	}
	
	ev.events = EPOLLIN;
	ev.data.fd = sfd;
	
	if (epoll_ctl(epfd, EPOLL_CTL_ADD, sfd, &ev)) {
		SPDLOG_ERROR("Faild to add signal fd to epfs: {}", strerror(errno));
		goto END;
	}
	
	/**********************************************************/
	while (true)
	{
		int rc;
		rc = epoll_wait(epfd, &events, 1, 1000);
		
		if (rc > 0)
		{
			struct signalfd_siginfo info = {0};
			
			::read (sfd, &info, sizeof(info));
			SPDLOG_INFO("Got signal {}", info.ssi_signo);
			
			if (info.ssi_signo == SIGINT || info.ssi_signo == SIGTERM) 
			{
				data->interrupted.store(true, std::memory_order_relaxed);
				ret = true;
				break;
			} 
			else 
			if (info.ssi_signo == SIGUSR1)
			{
				data->interrupted.store(true, std::memory_order_relaxed);
				data->b_reboot = true;
				ret = true;
				break;
			}
			else
			if (info.ssi_signo == SIGUSR2) 
			{
				//---restartをコマンド実行
				//
				nvr::do_systemctl("restart", "systemd-networkd");
			}
		}
		
		if (rc == 0)
		{
			if (data->is_power_pin_high.load(std::memory_order_relaxed))
			{
				break;
			}
			else
			if(first)
			{
				SPDLOG_INFO("Wait power pin to be high.");
				first = false;
			}
		}
	}
	
END:
	if (epfd != -1) {
		close(epfd);
	}
	
	if (sfd != -1) {
		close(sfd);
	}
	
	sigprocmask (SIG_UNBLOCK, &mask, nullptr);
	
	return ret;
}

/***********************************************************
***********************************************************/

/*----------------------------------------------------------
----------------------------------------------------------*/
void *usb_bulk_in_thread(usb_raw_gadget *usb, int ep_num)
{
    struct usb_packet_control pkt;
    auto timeout_at = std::chrono::steady_clock::now();

    while (true) {
        const auto now = std::chrono::steady_clock::now();
        while (timeout_at <= now) {
            timeout_at += std::chrono::milliseconds(40);
        }
     
        usb_tx_buffer.wait(timeout_at);
        
        
        /*

        pkt.data[0] = 0x31;
        pkt.data[1] = 0x60;
        int payload_length = usb_tx_buffer.dequeue(&pkt.data[2], sizeof(pkt.data) - 2);

        pkt.header.ep = ep_num;
        pkt.header.flags = 0;
        pkt.header.length = 2 + payload_length;

        if (connected.load()) {pkt.data[0] |= 0x80;}

        usb->ep_write(reinterpret_cast<struct usb_raw_ep_io *>(&pkt));
        */
    }

    return NULL;
}


/*----------------------------------------------------------
----------------------------------------------------------*/
void *usb_bulk_out_thread(usb_raw_gadget *usb, int ep_num) {
    struct usb_packet_bulk pkt;
    std::string buffer;

    bool echo = false;

    while (true) {
	    SPDLOG_INFO("Thread RECV");
        pkt.header.ep = ep_num;
        pkt.header.flags = 0;
        pkt.header.length = sizeof(pkt.data);

        int ret = usb->ep_read(reinterpret_cast<struct usb_raw_ep_io *>(&pkt));
        int payload_length = pkt.data[0] >> 2;
        if (payload_length != ret - 1) {
            printf("Payload length mismatch! (payload length in header: %d, received payload: %d)\n", payload_length, ret - 1);
            payload_length = std::min(payload_length, ret - 1);
        }
        buffer.append(&pkt.data[1], payload_length);

		bool enter_online = false;

        auto newline_pos = buffer.find('\x0d');
        if (newline_pos == std::string::npos) {break;}
        std::string line = buffer.substr(0, newline_pos);
        buffer.erase(0, newline_pos + 1);
        if (line.empty()) {break;}

        printf("command: %s\n", line.c_str());

    }

    return NULL;
}


/*----------------------------------------------------------
----------------------------------------------------------*/
bool process_control_packet(usb_raw_gadget *usb, usb_raw_control_event *e, struct usb_packet_control *pkt)
{
    if (e->is_event(USB_TYPE_STANDARD, USB_REQ_GET_DESCRIPTOR)) {
        const auto descriptor_type = e->get_descriptor_type();
        if (descriptor_type == USB_DT_DEVICE) {
            memcpy(pkt->data, &me56ps2_device_descriptor, sizeof(me56ps2_device_descriptor));
            pkt->header.length = sizeof(me56ps2_device_descriptor);
            return true;
        }
        if (descriptor_type == USB_DT_CONFIG) {
            memcpy(pkt->data, &me56ps2_config_descriptors, sizeof(me56ps2_config_descriptors));
            pkt->header.length = sizeof(me56ps2_config_descriptors);
            return true;
        }
        if (descriptor_type == USB_DT_STRING) {
            const auto id = e->ctrl.wValue & 0x00ff;
            if (id >= STRING_DESCRIPTORS_NUM) {return false;} // invalid string id
            const auto len = reinterpret_cast<const struct _usb_string_descriptor<1> *>(me56ps2_string_descriptors[id])->bLength;
            memcpy(pkt->data, me56ps2_string_descriptors[id], len);
            pkt->header.length = len;
            return true;
        }
    }
    if (e->is_event(USB_TYPE_STANDARD, USB_REQ_SET_CONFIGURATION)) 
    {
    	SPDLOG_INFO("USB_REQ_SET_CONFIGURATION");
    	
    	if( flag == 0 )
    	{
  			SPDLOG_INFO("ep_enable");
  			usb->reset_eps();


			const int ep_num_bulk_in = usb->ep_enable(
			reinterpret_cast<struct usb_endpoint_descriptor *>(&me56ps2_config_descriptors.endpoint_bulk_in));
			SPDLOG_INFO("ep1  OK  int = {}",ep_num_bulk_in);
			thread_bulk_in = new std::thread(usb_bulk_in_thread, usb, ep_num_bulk_in);


			const int ep_num_bulk_out = usb->ep_enable(
			reinterpret_cast<struct usb_endpoint_descriptor *>(&me56ps2_config_descriptors.endpoint_bulk_out));
			SPDLOG_INFO("ep2  OK  int = {}",ep_num_bulk_out);
			thread_bulk_out = new std::thread(usb_bulk_out_thread, usb, ep_num_bulk_out);

			/*
			const int ep_num_bulk_in2 = usb->ep_enable(
			reinterpret_cast<struct usb_endpoint_descriptor *>(&me56ps2_config_descriptors.endpoint_bulk_in2));
			SPDLOG_INFO("ep3  OK");


			const int ep_num_bulk_out2 = usb->ep_enable(
			reinterpret_cast<struct usb_endpoint_descriptor *>(&me56ps2_config_descriptors.endpoint_bulk_out2));
			SPDLOG_INFO("ep4  OK");
  			*/
  				
  			/*
  			try 
  			{
				const int ep_num_bulk_in = usb->ep_enable(
				reinterpret_cast<struct usb_endpoint_descriptor *>(&me56ps2_config_descriptors.endpoint_bulk_in));
				SPDLOG_INFO("ep1  OK");
			} catch (const std::exception& e) {
				SPDLOG_ERROR("ep1 例外発生: {}", e.what());
				return false;
			}
  			
  			
  			try 
  			{
				const int ep_num_bulk_out = usb->ep_enable(
				reinterpret_cast<struct usb_endpoint_descriptor *>(&me56ps2_config_descriptors.endpoint_bulk_out));
				SPDLOG_INFO("ep2  OK");
			} catch (const std::exception& e) {
				SPDLOG_ERROR("EP2例外発生: {}", e.what());
				return false;
			}

			
  			try {
				const int ep_num_bulk_in2 = usb->ep_enable(
				reinterpret_cast<struct usb_endpoint_descriptor *>(&me56ps2_config_descriptors.endpoint_bulk_in2));
				SPDLOG_INFO("ep3  OK");
			} catch (const std::exception& e) {
				SPDLOG_ERROR("EP3例外発生: {}", e.what());
				return false;
			}


  			try {
				const int ep_num_bulk_out2 = usb->ep_enable(
				reinterpret_cast<struct usb_endpoint_descriptor *>(&me56ps2_config_descriptors.endpoint_bulk_out2));
				SPDLOG_INFO("ep4  OK");
			} catch (const std::exception& e) {
				SPDLOG_ERROR("EP4例外発生: {}", e.what());
				return false;
			}
			*/
						
			flag = 1;
			SPDLOG_INFO("ep_enable END");
		}
        
        
		usb->vbus_draw(me56ps2_config_descriptors.config.bMaxPower);
        usb->configure();
        printf("USB configurated.\n");
        pkt->header.length = 0;
        return true;
    }
    if (e->is_event(USB_TYPE_STANDARD, USB_REQ_SET_INTERFACE)) {
        pkt->header.length = 0;
        
        return true;
    }
     if (e->is_event(USB_TYPE_VENDOR, 0x01)) {
        pkt->header.length = 0;
		SPDLOG_INFO("USB_TYPE_VENDOR001");
        return true;
    }
    if (e->is_event(USB_TYPE_VENDOR)) {
        pkt->header.length = 0;
		SPDLOG_INFO("USB_TYPE_VENDOR");
        return true;
    }

    return false;
}


/*----------------------------------------------------------
----------------------------------------------------------*/
bool event_usb_control_loop(usb_raw_gadget *usb)
{
    usb_raw_control_event e;
    e.event.type = 0;
    e.event.length = sizeof(e.ctrl);

    struct usb_packet_control pkt;
    pkt.header.ep = 0;
    pkt.header.flags = 0;
    pkt.header.length = 0;

    usb->event_fetch(&e.event);
 
    switch(e.event.type) {
        case USB_RAW_EVENT_CONNECT:
            break;
        case USB_RAW_EVENT_CONTROL:
            if (!process_control_packet(usb, &e, &pkt)) {
                usb->ep0_stall();
                break;
            }

            pkt.header.length = std::min(pkt.header.length, static_cast<unsigned int>(e.ctrl.wLength));
            if (e.ctrl.bRequestType & USB_DIR_IN) {
                usb->ep0_write(reinterpret_cast<struct usb_raw_ep_io *>(&pkt));
            } else 
            {
			    SPDLOG_INFO("ep0_read");            	
                usb->ep0_read(reinterpret_cast<struct usb_raw_ep_io *>(&pkt));
            }
            break;
        default:
            break;
    }

    return true;
}


/*----------------------------------------------------------
----------------------------------------------------------*/
int main(int argc, char **argv)
{
	const char *driver = USB_RAW_GADGET_DRIVER_DEFAULT;
    const char *device = USB_RAW_GADGET_DEVICE_DEFAULT;
    
    SPDLOG_INFO("main start");
    
    /*
    SPDLOG_INFO("usb_raw_gadget");
	usb_raw_gadget *usb = new usb_raw_gadget("/dev/raw-gadget");
    //usb->set_debug_level(debug_level);
    SPDLOG_INFO("init");
    usb->init(USB_SPEED_HIGH, driver, device);
    usb->reset_eps();
    SPDLOG_INFO("run");
    usb->run();
    
	SPDLOG_INFO("while Start");
	while(event_usb_control_loop(usb));
	SPDLOG_INFO("while end");
	*/
	



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
//	std::shared_ptr<nvr::gpio_in>	cminsig       = std::make_shared<nvr::gpio_in >("225", "P13_1");

	std::shared_ptr<nvr::gpio_in>	gpio_battery  = std::make_shared<nvr::gpio_in >("201", "P10_1");
	std::shared_ptr<nvr::gpio_out>	gpio_alrm_a   = std::make_shared<nvr::gpio_out>("216", "P12_0");
	std::shared_ptr<nvr::gpio_out>	gpio_alrm_b   = std::make_shared<nvr::gpio_out>("217", "P12_1");
	std::shared_ptr<nvr::gpio_in>	gpio_power    = std::make_shared<nvr::gpio_in >("224", "P13_0");
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
	
	//--- 電源監視pin初期化
	//
	if (gpio_power->open_with_edge()) {
		SPDLOG_ERROR("Failed to open gpio_power");
		exit(-1);
	}
	
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
	data.led   = led.get();
	data.sd_manager    = sd_manager.get();
	data.pipeline      = pipeline;
	data.gpio_power = gpio_power.get();
	data.logger        = logger;
	data.gpio_stop_btn  = gpio_stop_btn.get();
	data.copy_interval = 10;
	
	
	
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
	

	//---console input enable(メインループをコンソールからkillするため)
	//
#ifdef G_OS_UNIX
	data.signal_int_id   = g_unix_signal_add(SIGINT,  G_SOURCE_FUNC(callback_signal_intr), &data);
	data.signal_term_id  = g_unix_signal_add(SIGTERM, G_SOURCE_FUNC(callback_signal_term), &data);
	data.signal_user1_id = g_unix_signal_add(SIGUSR1, G_SOURCE_FUNC(callback_signal_user1), &data);
	data.signal_user2_id = g_unix_signal_add(SIGUSR2, G_SOURCE_FUNC(callback_signal_user2), &data);
#endif
	
	std::thread thread_power_monitior;
	
	
	//---電源監視スレッドの作成
	//
	thread_power_monitior = std::thread(thread_gpio_power, &data);
	if (wait_power_pin(gpio_power, &data)) {
		goto END;
	}
	
	
	//--- 電源監視の優先度を上げる。
	//
	struct sched_param param;
	param.sched_priority = std::max(sched_get_priority_max(SCHED_FIFO) / 3, sched_get_priority_min(SCHED_FIFO));
	if (pthread_setschedparam(thread_power_monitior.native_handle(), SCHED_FIFO, &param) != 0) {
		SPDLOG_WARN("Failed to thread_power_monitior scheduler.");
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
	
	if (thread_power_monitior.joinable()) {
	thread_power_monitior.join();
	}
	
	if (data.bus_watch_id)
	{
	g_source_remove(data.bus_watch_id);
	}
	
	if (data.timer1_id)
	{
		g_source_remove(data.timer1_id);
	}
	
#ifdef G_OS_UNIX
	if (data.signal_int_id)
	{
		g_source_remove(data.signal_int_id);
	}
	if (data.signal_term_id)
	{
		g_source_remove(data.signal_term_id);
	}
	if (data.signal_user1_id)
	{
		g_source_remove(data.signal_user1_id);
	}
	if (data.signal_user2_id)
	{
		g_source_remove(data.signal_user2_id);
	}
#endif
	

	if (data.b_reboot) {
		std::this_thread::sleep_for(std::chrono::seconds(1));
		_do_reboot();
	}
	std::exit(0);
}

/***********************************************************
	end of file
***********************************************************/










