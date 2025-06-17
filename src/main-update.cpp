#include <cstdio>
#include <cstdlib>
#include <cerrno>
#include <atomic>
#include <getopt.h>
#include <fstream>
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
#include "gpio.hpp"
#include "util.hpp"
#include <glib-unix.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sstream>
#include "i2c.hpp"


#define PATH_UPDATE		"/mnt/sd"
#define EXECUTE			"/usr/bin/nvr"

struct callback_data_t
{
	callback_data_t() : signal_int_id(0),
					    signal_term_id(0),
					    signal_user1_id(0),
					    signal_user2_id(0),
					    b_reboot(false),
					    interrupted(false),
					    is_power_pin_high(false),
					    main_loop(nullptr)
	{}
	
	guint signal_int_id;
	guint signal_term_id;
	guint signal_user1_id;
	guint signal_user2_id;
	bool b_reboot;
	std::atomic<bool> interrupted;
	nvr::gpio_in *gpio_power;
	std::atomic<bool> is_power_pin_high;
	bool update_ok;	
	nvr::gpio_out *_grn;
	nvr::gpio_out *_red;
	nvr::gpio_out *_yel;
	nvr::gpio_in *cminsig;
	GMainLoop *main_loop;
};


namespace fs = std::filesystem;
const std::filesystem::path inf1 = "/mnt/sd/EVC/REC_INF1.dat";
const std::filesystem::path inf2 = "/mnt/sd/EVC/REC_INF2.dat";
int g_mount_status = 0;
guint signal_int_id;
guint signal_term_id;
guint timer1_id;
pid_t child = -1;     //初期化時は-1


bool check_proc_mounts();
bool is_sd_card();
bool get_update_file( char* name );
bool execute();
void update_proc();
bool check_video();
int _do_reboot() noexcept;
bool check_power(callback_data_t *data);
bool check_cminsig(callback_data_t *data);

int g_counter_ = 0;
bool is_child = false;

int update_state = 0;

/***********************************************************
***********************************************************/
/*----------------------------------------------------------
----------------------------------------------------------*/
static gboolean timer1_cb(gpointer udata) 
{
	callback_data_t *data = static_cast<callback_data_t *>(udata);
	//500ms タイマー処理
	
	//
	
	check_power(data);
	update_proc();
	
	if( update_state > 2 && update_state < 6 )
	{
		if( g_counter_ >= 0 && g_counter_ < 1)
        {
            data->_grn->write_value(true);
            data->_red->write_value(true);
            data->_yel->write_value(true);        	
        }
        else
        if( g_counter_ >= 1 && g_counter_ < 2 )
        {
        	data->_grn->write_value(false);
            data->_red->write_value(false);
            data->_yel->write_value(false);
        }

        g_counter_++;
        if( g_counter_ > 1)
        {
            g_counter_ = 0;
        }        
	}
	else
	if( update_state >= 6 )
	{
		data->_grn->write_value(false);
        data->_red->write_value(false);
        data->_yel->write_value(false);        	
	}
	
	
	if( update_state == 6 )
	{
		if(!is_sd_card())
		{
			SPDLOG_INFO("_do_reboot");
			data->_grn->write_value(true);
            data->_red->write_value(true);
            data->_yel->write_value(true);
            update_state = 0;
//          data->b_reboot = true;
//			g_main_loop_quit(data->main_loop);
			return G_SOURCE_CONTINUE;
		}
	}
	else
	if( update_state == 0 )
	{
		int status;
		pid_t result = waitpid(child, &status, WNOHANG);

		if (result == child) {
			SPDLOG_INFO("child is dead");
			SPDLOG_INFO("child ReStart");
			execute();			
		}
		else
		if( result != 0 )
		{
			SPDLOG_ERROR("NO child");
			execute();
		}
	}

	
	return G_SOURCE_CONTINUE;
}


/*----------------------------------------------------------
----------------------------------------------------------*/
static gboolean signal_intr_cb(gpointer udata)
{
	//GMainLoopの終了(プロセスの終了)
	//
	SPDLOG_INFO("SIGINTR receiverd.");
	GMainLoop *loop = (GMainLoop *)udata;
	//g_main_loop_quit(loop);
	return G_SOURCE_REMOVE;
}


/*----------------------------------------------------------
----------------------------------------------------------*/
static gboolean signal_term_cb(gpointer udata)
{
	//GMainLoopの終了(プロセスの終了)
	//
	SPDLOG_INFO("SIGTERM receiverd.");
	GMainLoop *loop = (GMainLoop *)udata;
	//g_main_loop_quit(loop);
	return G_SOURCE_REMOVE;
}


/*----------------------------------------------------------
----------------------------------------------------------*/
bool check_proc_mounts()
{
	static const std::filesystem::path proc_mounts{"/proc/mounts"};
	static const char *dev_file = "/dev/mmcblk1p1";
	
	
	// /proc/mountsに/dev/mmcblk1p1の記述があるか
	//
	try
	{
		std::ifstream ifs(proc_mounts);
		std::string line;
		
		while (std::getline(ifs, line))
		{
			if (line.find(dev_file) != std::string::npos){
				return true;
			}
		}
	}
	catch (std::exception &ex)
	{
		SPDLOG_ERROR("Failed to check mout point: {}.", ex.what());
	}
	
	return false;
}


/*----------------------------------------------------------
----------------------------------------------------------*/
bool check_power(callback_data_t *data)
{
	guchar value;
	if (!data) {
//        SPDLOG_INFO("data is null");
        return false;
    }

    if (!data->gpio_power) {
//        SPDLOG_INFO("data->gpio_power is null");
        return false;
    }
    
	data->gpio_power->read_value(&value);
//	SPDLOG_INFO("power pin = {}",value);
	if (value == 1){
		return false;
	}
	
	
	//main kill
	//
	if (child > 0) 
	{
	 	int status;
		SPDLOG_INFO("PROCESS KILL...");
		kill(child, SIGTERM);
		waitpid(child, &status, 0);
		SPDLOG_INFO("PROCESS KILLED!!");
		child = -1;
	} 
	
	
    data->b_reboot = true;
	g_main_loop_quit(data->main_loop);
      
	SPDLOG_INFO("power reboot",value);
	return true;
}


/*----------------------------------------------------------
----------------------------------------------------------*/
bool is_sd_card()
{
	if ( !check_proc_mounts() ){
		return false;
	}
	
	return true;
}


/*----------------------------------------------------------
----------------------------------------------------------*/
int uncompress_encrypted( const char* zip_path )
{
	pid_t _pid = fork();  // 子プロセスを作る
	if (_pid == 0) {
		size_t len = std::strlen("/mnt/sd/") + std::strlen(zip_path);
		char* result = new char[len];

		// 連結
		std::strcpy(result, "/mnt/sd/");
		std::strcat(result, zip_path);
		SPDLOG_INFO("update path = {}",result);
	    // 子プロセスで unzip 実行
	    execl("/usr/bin/unzip", "unzip", "-P", "NRS", result, "-d", "/usr/bin/", (char*)NULL);
			
	    // execl が失敗した場合だけここに来る
	    std::perror("execl failed");
	    return 1;
	}
	else
	if (_pid > 0) 
	{
	    // 親プロセス：子プロセスの終了を待つ
	    int status;
	    waitpid(_pid, &status, 0);
	    if (WIFEXITED(status)) {
	        std::cout << "Unzip exited with code: " << WEXITSTATUS(status) << std::endl;
	    }
	    
	   	// 実行権限を付与
		const char* path = "/usr/bin/nvr";
		int result = chmod(path, S_IRUSR | S_IWUSR | S_IXUSR |
		                          S_IRGRP | S_IXGRP |
		                          S_IROTH | S_IXOTH);

		if (result != 0) {
		    SPDLOG_ERROR("chmod failed: {}", strerror(errno));
		    return 2;
		}

//		SPDLOG_INFO("chmod succeeded");	
	} 
	else
	{
	    // fork 失敗
	    SPDLOG_ERROR("fork failed");	
	    return 1;
	}
	
	system("sync");
	
	return 0;
}


/*----------------------------------------------------------
----------------------------------------------------------*/
bool get_update_file( char* name )
{
	const char *bin_dir = "/mnt/sd"; 
	fs::path path(bin_dir);
	std::error_code ec;	
	
	
	//---/mnt/sd ディレクトリの存在確認
	//
	if (!fs::exists(path, ec)) {
		return false;
	}
	
	//---SDフォルダにアップデータフォルダ(鍵付きZIP)があるかの確認
	//
	for (const fs::directory_entry& x : fs::directory_iterator(PATH_UPDATE)) 
	{
		
		std::string filename = x.path().filename().string();
		
		int pos = filename.find("EVC");
		std::cout << pos << std::endl;
		
		//フォルダ名にEVCとついたフォルダがあるかの確認(実行ファイル)
		//
		if(pos != 0){
			continue;
		}
		std::cout << x.path() << std::endl;
		std::cout << filename << std::endl;
		
		pos = filename.length() - filename.find(".zip");
		
		if (filename.size() >= 4 && filename.substr(filename.size() - 4) == ".zip")
		{
//			SPDLOG_INFO("ZIP file detected: {}", filename);
		}
		else
		{
			continue;
		}
//		SPDLOG_INFO("ZIP");
		
		//最初に見つかったファイル名を返す
		//
		std::strcpy(name, filename.c_str());
		
		
//		SPDLOG_INFO("is update file");
		return true;
	}
	
	return false;
}


/*----------------------------------------------------------
----------------------------------------------------------*/
bool execute()
{
	SPDLOG_INFO("execute()");
	fs::path path(EXECUTE);
	std::error_code ec;
	int status = 0;
	
	
	//---プロセスを複製し子プロセスを作成
	//
	pid_t _pid = fork(); 
	SPDLOG_INFO(" pid = {}",_pid);
	if (_pid < 0) 
	{
		//---プロセスの複製失敗
		//
		SPDLOG_ERROR("Failed to fork process: {}", strerror(errno));
		return false;
	} 
	else
	if( _pid == 0) 
	{
		//---子プロセス処理
		//
		SPDLOG_INFO(" child pid = {}",_pid);
		
		
		//実行ファイルを実行
		//
		execl( path.c_str()  , path.c_str(), "-r", "now", nullptr);
		//SPDLOG_ERROR("Failed to exec nvr.");
		
		
		//子プロセスの終了
		//
		SPDLOG_INFO("APP killed");
		exit(-1);
	}
    else 
    {
        std::cout << "Parent process. Child PID = " << _pid << std::endl;
        child = _pid;
    }

	
	SPDLOG_INFO("execute  success");	
	return true;
}

/*----------------------------------------------------------
----------------------------------------------------------*/
void update_proc()
{
	std::error_code ec;
	static char update_name[256]  = {0};
	static int time_out = 100;
	pid_t result = -1;
	int status;
	
	switch (update_state ) 
	{
  		case 0 :
			//---SDカードの挿入確認
			//
			if( is_sd_card() == true ){
				update_state++;
			}
			
			break;
			
		case 1 :
			//アップデータファイルの存在確認
			//
			update_name[256]  = {0};
			if( get_update_file( update_name ) == true )
			{
				update_state++;
			}
			else
			{
				update_state = 0;
			}
			
			break;

	
		case 2 :
			//--- プロセスを停止
			//
			time_out = 100;
			SPDLOG_INFO(" child = {}",child);
			if( child > 0 )
			{
				int status;
				SPDLOG_INFO("PROCESS KILL...");
				kill(child, SIGTERM);
				update_state++;
			}
			else
			{
				update_state = 0;
			}
			
			break;	
			
		case 3 :
			result = waitpid(child, &status, WNOHANG);
			
			if( result != 0 )
			{
				SPDLOG_INFO("PROCESS KILLED!!");
				child = -1;
				update_state++;
			}
			else
			{
				time_out--;
				if( time_out == 0 )
				{
					SPDLOG_INFO("PROCESS KILL TimeOut");
					update_state = 0;
				}
			}
			
			break;	
			
		case 4 :
	
			//--- 実行ファイルを削除
			//
			//	SPDLOG_INFO("実行ファイルを削除");
			try
			{
				fs::path del_path = fs::path(EXECUTE);
				if ( fs::exists(del_path, ec) ) 
				{				
					fs::remove( del_path );
					system("sync");
					//SPDLOG_INFO("del execute = {}",del_path.string());
				}
				update_state++;
			}
			catch (const fs::filesystem_error& e)
			{
				std::cerr << "実行ファイル削除失敗: " << e.what() << '\n';
				update_state = 0;
			}
			
			break;	

		case 5 :
			//--- アップデートファイルをコピー
			//
			if(uncompress_encrypted( (const char*)update_name ) != 0)
			{
				SPDLOG_ERROR("uncompress_encrypted failure");
				update_state = 0;
			}
			else
			{
				update_state++;
			}
	
			break;
			
		case 6 :
			break;
			
		default:
		    break;
	}
		
}

/***********************************************************
***********************************************************/
/*----------------------------------------------------------
----------------------------------------------------------*/
int _do_reboot() noexcept
{
	pid_t _pid;
	int status;
	int rc;
	
	
	//---プロセスの複製
	//
	_pid = fork();
	SPDLOG_INFO(" pid = {}",_pid);
	if (_pid < 0)
	{
		SPDLOG_ERROR("Failed to fork process: {}", strerror(errno));
		return -1;
	} 
	else
	if(_pid == 0) 
	{
		//---shutdownプロセス実行
		//
		SPDLOG_INFO(" child pid = {}",_pid);
		execl("/sbin/shutdown", "/sbin/shutdown", "-r", "now", nullptr);
		SPDLOG_ERROR("Failed to exec reboot.");
		exit(-1);
	}
	
	
	//---shutdownプロセス完了までwait
	//
	waitpid(_pid, &status, 0);

	
	if (!WIFEXITED(status)) {
		SPDLOG_ERROR("_do_reboot() return -1;");
		return -1;
	}
	
	rc = WEXITSTATUS(status);
	
	return rc;
}

#ifdef G_OS_UNIX
/*----------------------------------------------------------
----------------------------------------------------------*/
static gboolean callback_signal_user1(gpointer udata)
{
	SPDLOG_INFO("SIGUSER1 receiverd.");
	callback_data_t* data = static_cast<callback_data_t*>(udata);
	
	
	data->b_reboot = true;
	

	/* remove signal handler */
	data->signal_user1_id = 0;
	g_main_loop_quit(data->main_loop);
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
	
	//video_send_message(static_cast<GstElement*>(*data->pipeline));
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
	
	//video_send_message(static_cast<GstElement*>(*data->pipeline));
	data->interrupted.store(true, std::memory_order_relaxed);
	
	/* remove signal handler */
	data->signal_term_id = 0;
	return G_SOURCE_REMOVE;
}


#endif

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
	guchar new_value;
	
	sigemptyset (&mask);
	sigaddset (&mask, SIGINT);
	sigaddset (&mask, SIGTERM);
	sigaddset (&mask, SIGUSR1);
	sigaddset (&mask, SIGUSR2);
	
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
	for(int i = 0; i < 5; i++)
	{ 
		int rc;
		rc = epoll_wait(epfd, &events, 1, 100);
		if (rc > 0)
		{
			struct signalfd_siginfo info = {0};
			::read (sfd, &info, sizeof(info));
			
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
		
		pm->read_value(&new_value);
		SPDLOG_INFO("wait_power_pin pin = {}",new_value);
		if (new_value == 0) 
		{
			SPDLOG_INFO("i = {}",i);
			i = 0;
		}
		else
		if(first)
		{
			first = false;
		}
		
		//sleep(1);
	}
	
	SPDLOG_INFO("wait_power_pin ok");
	
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


/*----------------------------------------------------------
----------------------------------------------------------*/
int mount_sd()
{
    return mount(
       "/dev/mmcblk1p1",
		"/mnt/sd",
        "vfat",
        MS_NOATIME|MS_NOEXEC,
        "errors=continue"
    );
}

/*----------------------------------------------------------
----------------------------------------------------------*/
int main(int argc, char **argv)
{
	callback_data_t data{};

	//--- ルートファイルシステム 再マウント
	//
	if (mount(nullptr, "/", nullptr, MS_REMOUNT, nullptr)) {
		SPDLOG_ERROR("Failed to remount /.");
		exit(-1);
	}
	
	//--- init
	//
	
	spdlog::set_level(spdlog::level::debug);
	SPDLOG_INFO("main-update");
	
	
	//GPIO 初期化
	//
	std::shared_ptr<nvr::gpio_in>	gpio_power    = std::make_shared<nvr::gpio_in >("224", "P13_0");	//power
	std::shared_ptr<nvr::gpio_in>	cminsig       = std::make_shared<nvr::gpio_in >("225", "P13_1");	//camera
	std::shared_ptr<nvr::gpio_out> _grn = std::make_shared<nvr::gpio_out>("193", "P9_1");
	std::shared_ptr<nvr::gpio_out> _red = std::make_shared<nvr::gpio_out>("200", "P10_0");
	std::shared_ptr<nvr::gpio_out> _yel = std::make_shared<nvr::gpio_out>("192", "P9_0");
	
	if (_grn->open(true))
	{
		SPDLOG_ERROR("Failed to open led_board_green.");
		exit(-1);
	}
	
	if (_red->open(true))
	{
		SPDLOG_ERROR("Failed to open led_board_red.");
		exit(-1);
	}
	
	if (_yel->open(true))
	{
		SPDLOG_ERROR("Failed to open led_board_yel.");
		exit(-1);
	}
	
	if (gpio_power->open())
	{
		SPDLOG_ERROR("Failed to open led_board_yel.");
		exit(-1);
	}
	
	
		
	//--- callback_data_tにポインタvーセット
	//
	data.gpio_power = gpio_power.get();
	data.cminsig = cminsig.get();
	data._grn = _grn.get();
    data._red = _red.get();
    data._yel = _yel.get();

	
	//--- timer start
	//
	timer1_id = g_timeout_add_full(G_PRIORITY_HIGH,				// 優先度
									100,						// タイマー周期 100msec
									G_SOURCE_FUNC(timer1_cb),	//
									 &data,						// 
									nullptr);					// 
	if (!timer1_id)
	{
		SPDLOG_ERROR("Failed to add timer1.");
		exit(-1);
	}
	
	
	if (wait_power_pin(gpio_power, &data)) {
        goto END;
    }
    
	
	mount_sd();

	
	//---メインループ 作成実行
	//
	data.main_loop = g_main_loop_new(nullptr, FALSE);
	
	//---console input enable(メインループをコンソールからkillするため)
    //
	//signal_int_id  = g_unix_signal_add(SIGINT, G_SOURCE_FUNC(signal_intr_cb), main_loop);
	//signal_term_id = g_unix_signal_add(SIGTERM, G_SOURCE_FUNC(signal_term_cb), main_loop);
	
//	data.signal_int_id   = g_unix_signal_add(SIGINT,  G_SOURCE_FUNC(callback_signal_intr), &data);
//	data.signal_term_id  = g_unix_signal_add(SIGTERM, G_SOURCE_FUNC(callback_signal_term), &data);
	data.signal_user1_id = g_unix_signal_add(SIGUSR1, G_SOURCE_FUNC(callback_signal_user1), &data);
	data.signal_user2_id = g_unix_signal_add(SIGUSR2, G_SOURCE_FUNC(callback_signal_user2), &data);

	
	
	g_main_loop_run(data.main_loop);
	g_main_loop_unref(data.main_loop);
		
END:
	
	//---プロセス 終了処理
    //
    SPDLOG_INFO("main-update die");
	int status;
	if(child > 0)
	{
		kill(child, SIGTERM);
		waitpid(child, &status, 0);
		SPDLOG_INFO("nvr kill");
	}
	
	if (signal_int_id)
	{
		g_source_remove(signal_int_id);
	}
	if (signal_term_id)
	{
		g_source_remove(signal_term_id);
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
		SPDLOG_INFO("b_reboot");
		std::this_thread::sleep_for(std::chrono::seconds(1));
		_do_reboot();
	}
	
	std::exit(0);
	
}

/***********************************************************
	end of file
***********************************************************/
