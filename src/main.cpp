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
#include "video_writer.hpp"
#include "gpio.hpp"
#include "sd_manager.hpp"
#include "led_manager.hpp"
#include "util.hpp"
#include "logging.hpp"
#ifdef G_OS_UNIX
#include <glib-unix.h>
#endif
#ifdef __APPLE__
#include "avf_src.hpp"
#endif


/***********************************************************
***********************************************************/
struct callback_data_t
{
	callback_data_t() : timer1_id(0),
					    timer2_id(0),
					    bus_watch_id(0),
					    signal_int_id(0),
					    signal_term_id(0),
					    signal_user1_id(0),
					    signal_user2_id(0),
					    led_manager(nullptr),
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
	guint timer2_id;
	guint timer3_id;
	guint bus_watch_id;
	guint signal_int_id;
	guint signal_term_id;
	guint signal_user1_id;
	guint signal_user2_id;
	
	std::shared_ptr<nvr::pipeline> pipeline;
	std::shared_ptr<nvr::video_writer> video_writer;
	std::shared_ptr<nvr::logger> logger;
	nvr::led_manager *led_manager;
	nvr::sd_manager *sd_manager;
	
	nvr::gpio_out *rst_decoder;
	nvr::gpio_out *pwd_decoder;
	nvr::gpio_out *gpio_alrm_a;
	nvr::gpio_out *gpio_alrm_b;

	nvr::gpio_in *gpio_battery;
	nvr::gpio_in *pgood;
	nvr::gpio_in *gpio_stop_btn;
	
	std::filesystem::path done_dir;
	const char *jpeg_file;
	
	nvr::gpio_in *gpio_power;
	std::atomic<std::time_t> jpeg_time;
	bool is_video_error;
	bool b_reboot;
	bool rec_en;
	std::atomic<bool> interrupted;
	std::atomic<bool> is_power_pin_high;
#ifdef NVR_DEBUG_POWER
	nvr::gpio_out *tmp_out1;
#endif
	GMainLoop *main_loop;
};


namespace fs = std::filesystem;
static const char *g_interrupt_name = "nrs-video-recorder-interrupted";
static const char *g_quit_name = "nrs-video-recorder-quit";


/***********************************************************
***********************************************************/

/*----------------------------------------------------------
----------------------------------------------------------*/











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
			data->video_writer->set_powerdown();
			::open("/dev/adv71800", O_RDONLY);
			// ::open("/dev/adin0", O_RDONLY);
			// ioctl(soc, SIOCSIFFLAGS, &ifr);
			_do_reboot();
			return;
		}
	}
}


/***********************************************************
***********************************************************/

/*----------------------------------------------------------
----------------------------------------------------------*/
static gboolean callback_timer1(gpointer udata)
{
	callback_data_t *data = static_cast<callback_data_t *>(udata);
	const char *exe_dir = "/mnt/sd/jpeg"; 
	fs::path path(exe_dir);
	std::time_t t = std::time(nullptr);
	const std::tm *tm = std::localtime(&t);
	std::time_t jpeg_time = data->jpeg_time.load(std::memory_order_relaxed);
	static int cnt = 0;
	std::error_code ec;

	//---SDカードの挿入状態確認
	//
	data->sd_manager->timer_process();


	//---SDカードにjpegを現在時刻でコピー
	if(cnt >=10)
	{
		cnt = 0;
		if(data->sd_manager->check_mount_point() && data->rec_en == true)
		{
			if (!fs::exists(path, ec))
			{
				//---ディレクトリがないので作成
				//
				if (!fs::create_directories(path, ec)) {
					SPDLOG_ERROR("Failed to make directory: {}.", path.c_str());
				}
			}
			
			try 
			{
				path = path / g_strdup_printf( "%04d-%02d-%02d-%02d-%02d-%02d.jpeg",
												tm->tm_year + 1900,
												tm->tm_mon + 1,
												tm->tm_mday,
												tm->tm_hour,
												tm->tm_min,
												tm->tm_sec);
				
				fs::copy_file("/tmp/video.jpg", path, fs::copy_options::overwrite_existing);
				
				
				/*
				fs::copy_file("/tmp/video.jpg", path / g_strdup_printf(
				"%04d-%02d-%02d-%02d-%02d-%02d.jpeg",
				tm->tm_year + 1900,
				tm->tm_mon + 1,
				tm->tm_mday,
				tm->tm_hour,
				tm->tm_min,
				tm->tm_sec), 
				fs::copy_options::overwrite_existing);
				*/
				SPDLOG_INFO("jpeg save");
			}
			catch (const fs::filesystem_error& e) 
			{
				std::cerr << "画像コピー失敗: " << e.what() << '\n';
			}
			
			try 
			{
			    int fd = ::open(path.c_str(), O_WRONLY);
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
		}
	}
	else
	{
		cnt++;
	}
	

	return G_SOURCE_CONTINUE;
}

/*----------------------------------------------------------
----------------------------------------------------------*/
static gboolean callback_timer2(gpointer udata)
{
	callback_data_t *data = static_cast<callback_data_t *>(udata);
		
	//---100ms timer
	//
	guchar value;
	static int _cnt_btn = 0;
	static int _cnt_stop = 0;
	data->gpio_stop_btn->read_value(&value);
	std::time_t t = std::time(nullptr);
	const std::tm *tm = std::localtime(&t);
	std::time_t jpeg_time = data->jpeg_time.load(std::memory_order_relaxed);


	//一定時間でスプリットナウ
	//
	if (tm->tm_sec == 0)
	{
		if (data->pipeline->is_running())
		 {
			data->pipeline->split_video_file();
		}
	}
	
	if( data->rec_en )
	{
		if (value == 0) 
		{
			//---ボタンが押されている
			//
			_cnt_btn++;
		}
		else
		{
			_cnt_btn = 0;
		}
		
		if( _cnt_btn >= 30) 
		{
			//---３０秒間録画停止
			//
			SPDLOG_INFO("rec_stop start");
			data->led_manager->set_g(nvr::led_manager::one);
			data->rec_en = false;
			_cnt_btn = 0;
		}
	}
	else
	{
		if( _cnt_stop < 300 ){
			_cnt_stop++;
		}
		else
		{
			SPDLOG_INFO("rec_stop over30");
			if(data->sd_manager->check_mount_point())
			{
				//
				//
				SPDLOG_INFO("rec_stop end");
				data->rec_en = true;
				data->led_manager->set_g(nvr::led_manager::on);
				_cnt_stop = 0;
			}
		}
	}
	
	
	//--- 同期エラー
	//
	if (jpeg_time <= t)
	{
		bool is_video_error = data->is_video_error;
		if ((t - jpeg_time) > 1)
		{
			if (!is_video_error)
			{
				data->led_manager->set_g(nvr::led_manager::off);
				data->led_manager->set_r(nvr::led_manager::blink);
				data->logger->write("E カメラ映像同期エラー");
				data->is_video_error = true;
			}
		} 
		else 
		{
			if (is_video_error) 
			{
				//data->led_manager->clear_status(nvr::led_manager::state_error_video);
				data->is_video_error = false;
			}
		}
	}
	
	
	//---LED 点灯パターン更新
	//
	SPDLOG_INFO("callback_timer2 update_led");
	data->led_manager->update_led();
	
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
			
			if (s && gst_structure_has_name(s, "splitmuxsink-fragment-closed"))
			{
				break;
				
				//---mp4 保存
				//
			}
			else
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
	data->video_writer->stop();
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
	data->video_writer->stop();
	
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
	data->video_writer->stop();
	
	/* remove signal handler */
	data->signal_term_id = 0;
	return G_SOURCE_REMOVE;
}


#endif


/***********************************************************
***********************************************************/

/*----------------------------------------------------------
----------------------------------------------------------*/
static const std::filesystem::path pid_file_path{"/run/nvr.pid"};

/*----------------------------------------------------------
----------------------------------------------------------*/
static int video_write_pid_file()
{
	//---自分のpidをファイルに保存
	//
	pid_t pid = getpid();
	FILE *fp = fopen(pid_file_path.c_str(), "w");
	if (fp == nullptr) {
		SPDLOG_ERROR("Failed to open {}: {}", pid_file_path.c_str(), strerror(errno));
		return -1;
	}
	fprintf(fp, "%d\n", pid);
	fflush(fp);
	fclose(fp);

	return 0;
}


/*----------------------------------------------------------
----------------------------------------------------------*/
static void video_unlink_pid_file()
{
	//---/run/nvr.pidがあれば削除
	//
	std::error_code ec;
	if (std::filesystem::exists(pid_file_path, ec)) {
		std::filesystem::remove(pid_file_path, ec);
	}
}


/*----------------------------------------------------------
----------------------------------------------------------*/
static nvr::video_src *video_src(const nvr::config& config)
{
    return new nvr::v4l2_src(config.video_width(), config.video_height());
}


/*----------------------------------------------------------
----------------------------------------------------------*/
static nvr::video_sink *video_sink(const char *tmp_dir, const nvr::config& config)
{
	nvr::video_sink *ret = nullptr;
	std::filesystem::path path(tmp_dir);
	path /= "video";
	
	//---/tmp/nvrのディレクトリがあるかの存在確認
	//
	std::error_code ec;
	if (!std::filesystem::is_directory(path, ec))
	{
		//ディレクトリがないので作成
		//
		if (!std::filesystem::create_directories(path, ec))
		{
			SPDLOG_ERROR("Failed to make directory: {}.", path.c_str());
			return nullptr;
		}
	}
	SPDLOG_DEBUG("Video location directory is {}.", path.c_str());

	//動画の保存場所のpathを返す
	//
	return new nvr::video_sink( path.c_str(), config.video_framerate(), config.video_bitrate() );
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
			//なんのために行ってるか不明
			/**********************************************************/
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
	//よくわかっていない
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
int main(int argc, char **argv)
{
	//---init
	//
	std::shared_ptr<nvr::pipeline> pipeline;
	std::shared_ptr<nvr::video_writer> video_writer;
	spdlog::set_level(spdlog::level::debug);
	
	callback_data_t data{};
	SPDLOG_INFO("ver0.1.3");
	
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
	
	
	//---gstreamer初期化
	//
	gst_init(&argc, &argv);
	/**********************************************************/
	//自分のpidをファイル保存
	//
	video_write_pid_file();
	//なんのために行ってるか不明
	//終了処理時に使用。自分をしっかりと殺すため？
	/**********************************************************/
	
	
	//--- pipeline初期化
	//
	const char *config_file = "/etc/nvr/nvr.json";
	const char *factoryset_file = "/etc/nvr/factoryset.json";
	const char *tmp_dir = "/tmp/nvr";
	const char *jpeg_file = "/tmp/video.jpg";
	pipeline = std::make_shared<nvr::pipeline>();
	
	auto config = nvr::config(config_file, factoryset_file);
	pipeline->set_video_src(video_src(config));
	pipeline->set_video_sink(video_sink(tmp_dir, config));
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
	
	std::shared_ptr<nvr::led_manager> led_manager = std::make_shared<nvr::led_manager>();
	
	led_manager->set_g(nvr::led_manager::off);
	led_manager->set_r(nvr::led_manager::off);


	//---ボタン電池の電圧確認
	//
	guchar bat;
	gpio_battery->read_value(&bat);
	
	
	//---SD周りの初期化
	//
	std::shared_ptr<nvr::sd_manager> sd_manager = std::make_shared<nvr::sd_manager>(
		"/dev/mmcblk1p1",
		"/mnt/sd",
		"/mnt/sd/.nrs_video_data",
		"/mnt/sd/nvr",
		"/usr/bin/nvr",
		logger
	);
	
	
	//--- 映像系初期化
	//
	video_writer = std::make_shared<nvr::video_writer>(data.done_dir.c_str(), "/mnt/sd");
	//video_writer->set_led_manager(led_manager);
	video_writer->set_sd_manager(sd_manager);
	video_writer->set_logger(logger);
	
	//--- callback_data_tにポインターセット
	//
	data.pipeline      = pipeline;
	data.video_writer  = video_writer;
	data.led_manager   = led_manager.get();
	data.sd_manager    = sd_manager.get();
	data.gpio_power = gpio_power.get();
	data.logger        = logger;
	data.gpio_stop_btn  = gpio_stop_btn.get();
	
	
	//--- callback bus
	//
	data.bus_watch_id = video_add_bus_watch(pipeline, &data);
	if (!data.bus_watch_id) {
		SPDLOG_ERROR("Failed to wach bus.");
		exit(-1);
	}
	
	
	//--- timer1 start
	//
	data.timer1_id = g_timeout_add_full( G_PRIORITY_HIGH,
										 100,
										 G_SOURCE_FUNC(callback_timer1),
										 &data,
										 nullptr );
	if (!data.timer1_id){
		SPDLOG_ERROR("Failed to add timer1.");
		exit(-1);
	}
	
	//--- timer2 start
	//
	data.timer2_id = g_timeout_add_full( G_PRIORITY_DEFAULT,
										 100,
										 G_SOURCE_FUNC(callback_timer2),
										 &data,
										 nullptr );
	if (!data.timer2_id) {
		SPDLOG_ERROR("Failed to add timer2.");
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
	std::thread thread_video_writer;
	
	
	//---電源監視スレッドの作成
	//
	thread_power_monitior = std::thread(thread_gpio_power, &data);
	if (wait_power_pin(gpio_power, &data)) {
		goto END;
	}
	
	
	//--- ???
	//
	struct sched_param param;
	param.sched_priority = std::max(sched_get_priority_max(SCHED_FIFO) / 3, sched_get_priority_min(SCHED_FIFO));
	if (pthread_setschedparam(thread_power_monitior.native_handle(), SCHED_FIFO, &param) != 0) {
		SPDLOG_WARN("Failed to thread_power_monitior scheduler.");
	}
	
	
	//---録画スレッドの作成
	//
	SPDLOG_DEBUG("Start video writer.");
	thread_video_writer = std::thread(video_writer->process());
	led_manager->set_g(nvr::led_manager::on);
	
	
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
	video_writer->stop();
	data.interrupted.store(true, std::memory_order_relaxed);
	
	if (thread_video_writer.joinable()) {
	thread_video_writer.join();
	}
	
	
	if (thread_power_monitior.joinable()) {
	thread_power_monitior.join();
	}
	
	if (data.bus_watch_id)
	{
	g_source_remove(data.bus_watch_id);
	}
	
	if (data.timer2_id)
	{
		g_source_remove(data.timer2_id);
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
	
	video_unlink_pid_file();
	

	if (data.b_reboot) {
		std::this_thread::sleep_for(std::chrono::seconds(1));
		_do_reboot();
	}
	std::exit(0);
}

/***********************************************************
	end of file
***********************************************************/





