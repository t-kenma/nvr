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
#include "logging.hpp"
#include <glib-unix.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sstream>


struct callback_data_t
{
	callback_data_t() : signal_int_id(0),
					    signal_term_id(0),
					    signal_user1_id(0),
					    signal_user2_id(0),
					    b_reboot(false),
					    interrupted(false),
					    is_power_pin_high(false)
	{}
	
	guint signal_int_id;
	guint signal_term_id;
	guint signal_user1_id;
	guint signal_user2_id;
	bool b_reboot;
	std::atomic<bool> interrupted;
	nvr::gpio_in *gpio_power;
	std::atomic<bool> is_power_pin_high;
	
	std::shared_ptr<nvr::logger> logger;	
};


namespace fs = std::filesystem;
const std::filesystem::path inf1 = "/mnt/sd/EVC/REC_INF1.dat";
const std::filesystem::path inf2 = "/mnt/sd/EVC/REC_INF2.dat";
int g_mount_status = 0;
guint signal_int_id;
guint signal_term_id;
guint timer1_id;
pid_t pid = -1;     //初期化時は-1
nvr::gpio_out *led_board_green;
nvr::gpio_out *led_board_red;
nvr::gpio_out *led_board_yel;


#define PATH_UPDATE		"/mnt/sd"
#define EXECUTE	"/usr/bin/nvr"


bool check_proc_mounts();
inline bool is_root_file_exists()noexcept;
bool is_sd_card();
bool get_update_file( char* name );
bool get_execute_file( char* name );
bool execute();
bool update();
int _do_reboot() noexcept;
bool update_ok = false;
bool check_power(callback_data_t *data);
/***********************************************************
***********************************************************/

/*----------------------------------------------------------
----------------------------------------------------------*/
static gboolean timer1_cb(gpointer udata) 
{
	callback_data_t *data = static_cast<callback_data_t *>(udata);
	//1000ms タイマー処理
	//
	SPDLOG_INFO("timer on");
	
	//gpio_power_check(&data);
	
	check_power(data);
	SPDLOG_INFO("timer on");
	
	if( update_ok )
	{
		SPDLOG_INFO("update_ok");
		if(is_sd_card())
		{
			_do_reboot();
		}
		
		
		return G_SOURCE_CONTINUE;
	}
	
	if( update() == true )
	{
		//---SD抜き待ち
		//
		update_ok = true;
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
inline bool is_root_file_exists() noexcept
{
	std::error_code ec;

	if (!fs::exists(inf1, ec)) {
		SPDLOG_INFO("no inf1");
		return false;
	}

	if (!fs::exists(inf2, ec)) {
		SPDLOG_INFO("no inf2");
		return false;
	}

	return true;
}

/*----------------------------------------------------------
----------------------------------------------------------*/
bool check_power(callback_data_t *data)
{

	guchar value;
	if (!data) {
        SPDLOG_INFO("data is null");
        return false;
    }

    if (!data->gpio_power) {
        SPDLOG_INFO("data->gpio_power is null");
        return false;
    }
    
	data->gpio_power->read_value(&value);
	SPDLOG_INFO("power pin = {}",value);
	if (value == 1){
		return false;
	}

	return true;
}

/*----------------------------------------------------------
----------------------------------------------------------*/
bool is_sd_card()
{
	if ( !check_proc_mounts() ){
		return false;
	}
	
	/*
	if( !is_root_file_exists() ) {
		return false;
	}
	*/
	
	return true;
}

#if 1
/*----------------------------------------------------------
----------------------------------------------------------*/
int uncompress_encrypted( const char* zip_path )
{
	pid_t pid = fork();  // 子プロセスを作る
	if (pid == 0) {
	    // 子プロセスで unzip 実行
	    execl("/usr/bin/unzip", "unzip", "-P", "NRS", zip_path, "-d", EXECUTE, (char*)NULL);
	    
	    // execl が失敗した場合だけここに来る
	    std::perror("execl failed");
	    return 1;
	} else if (pid > 0) {
	    // 親プロセス：子プロセスの終了を待つ
	    int status;
	    waitpid(pid, &status, 0);
	    if (WIFEXITED(status)) {
	        std::cout << "Unzip exited with code: " << WEXITSTATUS(status) << std::endl;
	    }
	} else {
	    // fork 失敗
	    std::perror("fork failed");
	    return 1;
	}
	
	return 0;
}






/*
	// パスワードの定義
	const char* thePassWord = "NRS";


	// ZIPファイルを読み取り専用で開く
	int errorp;

	zip_t* zipper = zip_open( zip_path, ZIP_RDONLY, &errorp );

	// ZIP内のファイルの個数を取得
	zip_int64_t num_entries = zip_get_num_entries(zipper, 0);

	// ZIP内のファイルの各ファイル名を取得
	std::cout << "count: " << num_entries << std::endl;
	for (zip_int64_t index = 0; index < num_entries; index++) {
		std::cout << "[" << index << "]" << zip_get_name(zipper, index, ZIP_FL_ENC_RAW) << std::endl;
	}

	// ZIP内の1番目のファイルに関する情報を取得する
	struct zip_stat sb;
	zip_int64_t index = 1;
	zip_stat_index(zipper, index, 0, &sb);

	// 1番目のファイルのファイルサイズと同じメモリを確保する
	char* contents = new char[sb.size];

	// 1番目のファイルの内容をメモリに読み込む
	zip_file* zf = zip_fopen_encrypted(zipper, sb.name, 0, thePassWord);
	zip_fread(zf, contents, sb.size);
	zip_fclose(zf);

	zip_close(zipper);

	//////////////////
	// ファイル名を出力できる形に変更
	// ファイル一覧は階層構造をしておらず、ディレクトリ区切りは'/'で直接出力できないので
	// ファイル名中の'/'を'-'に置き換える。
	// 本来なら再帰的にディレクトリを作るなどすべき。
	std::string target = sb.name;
	for (size_t i = 0; i < target.size(); i++) {
	if (target[i] == '/') {
	target[i] = '-';
	}
	}
	//
	//////////////////

	// 解凍したファイルを作成
	std::string outname = "/home/root/exec/" + target;
	FILE* of = fopen(outname.c_str(), "wb");
	fwrite(contents, 1, sb.size, of);
	fclose(of);
}
*/
#endif

/*----------------------------------------------------------
----------------------------------------------------------*/
bool get_update_file( char* name )
{
	SPDLOG_INFO("get_update_file()");
	const char *bin_dir = "/mnt/sd"; 
	fs::path path(bin_dir);
	std::error_code ec;	
	
	
	//---/mnt/sd ディレクトリの存在確認
	//
	if (!fs::exists(path, ec)) {
		SPDLOG_INFO("no dir");
		return false;
	}
	
	//---SDフォルダにアップデータフォルダ(鍵付きZIP)があるかの確認
	//
	for (const fs::directory_entry& x : fs::directory_iterator(PATH_UPDATE)) 
	{
		
		std::string filename = x.path().filename().string();
		
		int pos = filename.find("EVC");
		std::cout << pos << std::endl;
		
		//ファイル名にEVCとついたファイルがあるかの確認(実行ファイル)
		//
		if(pos != 0){
			continue;
		}
		std::cout << x.path() << std::endl;
		std::cout << filename << std::endl;
		
		pos = filename.length() - filename.find(".zip");
		SPDLOG_INFO("isfile pos={}",pos);
		if(pos != 4){
			continue;
		}
		SPDLOG_INFO("ZIP");
		
		//最初に見つかったファイル名を返す
		//
		std::strcpy(name, filename.c_str());
		SPDLOG_INFO("is update file");
		return true;
	}
	
	return false;
}

/*----------------------------------------------------------
----------------------------------------------------------*/
bool get_execute_file( char* name )
{
	SPDLOG_INFO("get_execute_file()");
	const char *exe_dir = "/var/tmp/app_exe"; 
	fs::path path(exe_dir);
	std::error_code ec;
	
	//---/tmp/app_exeディレクトリの存在確認
	//
	if (!fs::exists(path, ec))
	{
		if (!fs::create_directories(path, ec)) {
			SPDLOG_ERROR("Failed to make directory: {}.", path.c_str());   
		}
		return false;
	}
	
	
	for (const fs::directory_entry& x : fs::directory_iterator(EXECUTE)) 
	{
		std::cout << x.path() << std::endl;
		std::string filename = x.path().filename().string();
		std::strcpy(name, filename.c_str());
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
	
	
	
	
	//---プロセスを複製し子プロセスを作成
	//
	pid = fork();
	SPDLOG_INFO(" pid = {}",pid);
	if (pid < 0) 
	{
		//---プロセスの複製失敗
		//
		SPDLOG_ERROR("Failed to fork process: {}", strerror(errno));
		return false;
	} 
	else
	if( pid == 0) 
	{
		//---子プロセス処理
		//
		SPDLOG_INFO(" child pid = {}",pid);
		
		//実行ファイルを実行
		//
		execl( path.c_str()  , path.c_str(), "-r", "now", nullptr);
		SPDLOG_ERROR("Failed to exec nvr.");
		
		
		//子プロセスの終了
		//
		exit(-1);
	}
	
	SPDLOG_INFO("execute  success");	
	return true;
}

/*----------------------------------------------------------
----------------------------------------------------------*/
bool update()
{	
	//---SDカードの挿入確認
	//
	if( is_sd_card() == false ){
		SPDLOG_INFO("is_sd_card() false");
		return false;
	}
	
	
	//アップデータファイルの存在確認
	//
	char update_name[256]  = {0};
	if( get_update_file( update_name ) == false ){
		SPDLOG_INFO("get_update_file() false");
		return false;
	}
	

	//--- プロセスを停止
	//
	/*
	SPDLOG_INFO(" pid = {}",pid);
	if( pid )
	{
		int status;
		SPDLOG_INFO("PROCESS KILL...");
		kill(pid, SIGTERM);
		waitpid(pid, &status, 0);
		SPDLOG_INFO("PROCESS KILLED!!");
	}
	*/
	
	
	//--- 実行ファイルを削除
	//
	SPDLOG_INFO("実行ファイルを削除");
	try
	{
		fs::path del_path = fs::path(EXECUTE);
		fs::remove( del_path );
		SPDLOG_INFO("del execute = {}",del_path.string());
	}
	catch (const fs::filesystem_error& e)
	{
		std::cerr << "実行ファイル削除失敗: " << e.what() << '\n';
		return false;
	}
	
	
	//--- アップデートファイルをコピー
	//
	if(!uncompress_encrypted( (const char*)update_name ))
	{
		return false;
	}
	
	
		
	SPDLOG_INFO("update() true");
	return true;
}

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

#ifdef G_OS_UNIX
/*----------------------------------------------------------
----------------------------------------------------------*/
static gboolean callback_signal_user1(gpointer udata)
{
	SPDLOG_INFO("SIGUSER1 receiverd.");
	callback_data_t* data = static_cast<callback_data_t*>(udata);
	
	//video_send_message(static_cast<GstElement*>(*data->pipeline));
	data->interrupted.store(true, std::memory_order_relaxed);
	data->b_reboot = true;
	
	//data->logger->write("L リブート");
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
	
	/**********************************************************/
	//アップローダーに持っていく
	/**********************************************************/
	SPDLOG_INFO("wait_power_pin");
	if (sigprocmask (SIG_BLOCK, &mask, nullptr) == -1) {
		SPDLOG_ERROR("Failed to sigprocmask: {}", strerror(errno));
		return ret;
	}
	
	SPDLOG_INFO("wait_power_pin 1");
	sfd = signalfd (-1, &mask, SFD_NONBLOCK|SFD_CLOEXEC);
	if (sfd == -1){
		SPDLOG_ERROR("Failed to signalfd: {}", strerror(errno));
		goto END;
	}
	
	SPDLOG_INFO("wait_power_pin-2");
	epfd = epoll_create1(EPOLL_CLOEXEC);
	if (epfd == -1) {
		SPDLOG_ERROR("Failed to epoll_create1: {}", strerror(errno));
		goto END;
	}
	
	ev.events = EPOLLIN;
	ev.data.fd = sfd;
	
	SPDLOG_INFO("wait_power_pin-3");
	if (epoll_ctl(epfd, EPOLL_CTL_ADD, sfd, &ev)) {
		SPDLOG_ERROR("Faild to add signal fd to epfs: {}", strerror(errno));
		goto END;
	}
	
	/**********************************************************/
	while (true)
	{
		int rc;
		rc = epoll_wait(epfd, &events, 1, 1000);
		SPDLOG_INFO("wait_power_pin-while");
		if (rc > 0)
		{
			struct signalfd_siginfo info = {0};
			SPDLOG_INFO("wait_power_pin -4");
			::read (sfd, &info, sizeof(info));
			SPDLOG_INFO("Got signal {}", info.ssi_signo);
			
			if (info.ssi_signo == SIGINT || info.ssi_signo == SIGTERM) 
			{
				SPDLOG_INFO("wait_power_pin-5");
				data->interrupted.store(true, std::memory_order_relaxed);
				ret = true;
				break;
			} 
			else 
			if (info.ssi_signo == SIGUSR1)
			{	
				SPDLOG_INFO("wait_power_pin-6");
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
				SPDLOG_INFO("wait_power_pin-7");
				nvr::do_systemctl("restart", "systemd-networkd");
			}
		}
		
		if (rc == 0)
		{
			SPDLOG_INFO("wait_power_pin-8");
			pm->read_value(&new_value);
			SPDLOG_INFO("wait_power_pin-10");
			if (new_value == 1) 
			{
				SPDLOG_INFO("wait_power_pin-11");
				break;
			}
			else
			if(first)
			{
				SPDLOG_INFO("wait_power_pin-9");
				SPDLOG_INFO("Wait power pin to be high.");
				first = false;
			}
		}
		SPDLOG_INFO("wait_power_pin-while end");
		sleep(100);
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
/*
static void gpio_power_check(callback_data_t *data)
{
	int fd = data->gpio_power->fd();
	int epfd = epoll_create1(EPOLL_CLOEXEC);
	struct epoll_event ev;
	struct epoll_event events;
	struct ifreq ifr;
	ev.events = EPOLLPRI;
	ev.data.fd = fd;
	guchar new_value;
	
	if (epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev)) {
		SPDLOG_ERROR("Faild to add fd to epfs: {}", strerror(errno));
	}
	
	//---割り込みが来るまで実行
	//
	data->gpio_power-> read_value(&new_value);
	
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
*/

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
	GMainLoop *main_loop;

	
	
	//--- init
	//
	
	spdlog::set_level(spdlog::level::debug);
	SPDLOG_INFO("main-update");
	
	
	//GPIO 初期化
	//
	std::shared_ptr<nvr::gpio_in>	gpio_power    = std::make_shared<nvr::gpio_in >("224", "P13_0");
	std::shared_ptr<nvr::gpio_out> led_board_green = std::make_shared<nvr::gpio_out>("193", "P9_1");
	std::shared_ptr<nvr::gpio_out> led_board_red = std::make_shared<nvr::gpio_out>("200", "P10_0");
	std::shared_ptr<nvr::gpio_out> led_board_yel = std::make_shared<nvr::gpio_out>("192", "P9_0");
	std::shared_ptr<nvr::logger> logger = std::make_shared<nvr::logger>("/etc/nvr/video-recorder.log");
	
	if (led_board_green->open(false))
	{
		SPDLOG_ERROR("Failed to open led_board_green.");
		exit(-1);
	}
	
	if (led_board_red->open(false))
	{
		SPDLOG_ERROR("Failed to open led_board_red.");
		exit(-1);
	}
	
	if (led_board_yel->open(false))
	{
		SPDLOG_ERROR("Failed to open led_board_yel.");
		exit(-1);
	}
	
	if (gpio_power->open())
	{
		SPDLOG_ERROR("Failed to open led_board_yel.");
		exit(-1);
	}
	
	
	
	SPDLOG_INFO("LED END");	
	
	//--- callback_data_tにポインタvーセット
	//
	data.logger = logger;
	data.gpio_power = gpio_power.get();
	
	//--- timer start
	//
	SPDLOG_INFO("ttimer start");
	timer1_id = g_timeout_add_full(G_PRIORITY_HIGH,				// 優先度
									1000,						// タイマー周期 1000msec
									G_SOURCE_FUNC(timer1_cb),	//
									 &data,						// 
									nullptr);					// 
	if (!timer1_id)
	{
		SPDLOG_ERROR("Failed to add timer1.");
		exit(-1);
	}
	
	
	//---メインループ 作成
	//

	main_loop = g_main_loop_new(NULL, FALSE);
	
	
	//---console input enable(メインループをコンソールからkillするため)
    //
	signal_int_id  = g_unix_signal_add(SIGINT, G_SOURCE_FUNC(signal_intr_cb), main_loop);
	signal_term_id = g_unix_signal_add(SIGTERM, G_SOURCE_FUNC(signal_term_cb), main_loop);
#ifdef G_OS_UNIX
	data.signal_int_id   = g_unix_signal_add(SIGINT,  G_SOURCE_FUNC(callback_signal_intr), &data);
	data.signal_term_id  = g_unix_signal_add(SIGTERM, G_SOURCE_FUNC(callback_signal_term), &data);
	data.signal_user1_id = g_unix_signal_add(SIGUSR1, G_SOURCE_FUNC(callback_signal_user1), &data);
	data.signal_user2_id = g_unix_signal_add(SIGUSR2, G_SOURCE_FUNC(callback_signal_user2), &data);
#endif

	SPDLOG_INFO("電源check");
	if (wait_power_pin(gpio_power, &data)) {
        goto END;
    }
    
	SPDLOG_INFO("電源OK");
	
	mount_sd();
	//update();
	//execute();
	
	//---メインループ 実行
	//
	SPDLOG_INFO("main loop start");
	g_main_loop_run(main_loop);
		
END:
	
	//---プロセス 終了処理
    //
    SPDLOG_INFO("main-update die");
	int status;
	if(pid)
	{
		kill(pid, SIGTERM);
		waitpid(pid, &status, 0);
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
		std::this_thread::sleep_for(std::chrono::seconds(1));
		_do_reboot();
	}
	
	std::exit(0);
	
}

/***********************************************************
	end of file
***********************************************************/




