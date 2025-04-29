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


namespace fs = std::filesystem;
int g_mount_status = 0;
guint signal_int_id;
guint signal_term_id;
guint timer1_id;
pid_t pid = -1;     //初期化時は-1
nvr::gpio_out *led_board_green;
nvr::gpio_out *led_board_red;
nvr::gpio_out *led_board_yel;


#define PATH_UPDATE		"/mnt/sd/bin/"
#define PATH_EXECUTE	"/tmp/app_exe/"
#define BUCKUP_EXECUTE	"/usr/bin/nvr"


bool check_proc_mounts();
inline bool is_root_file_exists()noexcept;
bool is_sd_card();
bool get_update_file( char* name );
bool get_execute_file( char* name );
bool execute();
bool update();

/***********************************************************
***********************************************************/

/*----------------------------------------------------------
----------------------------------------------------------*/
static gboolean timer1_cb(gpointer udata) 
{
	//1000ms タイマー処理
	//
	if( update() == true )
	{
		execute();
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
	g_main_loop_quit(loop);
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
	g_main_loop_quit(loop);
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
				SPDLOG_INFO("check_proc_mounts true");
				return true;
			}
		}
	}
	catch (std::exception &ex)
	{
		SPDLOG_ERROR("Failed to check mout point: {}.", ex.what());
	}
	
	SPDLOG_INFO("check_proc_mounts false");
	return false;
}

/*----------------------------------------------------------
----------------------------------------------------------*/
inline bool is_root_file_exists() noexcept
{
	static const char *root_file_ = "/mnt/sd/.nrs_video_data";
	std::error_code ec;
	
	
	//---SDカードに.nrs_video_dataファイルがあるかの確認
	//
	try
	{
		std::filesystem::exists(root_file_, ec);	
	}
	catch (const fs::filesystem_error& e)
	{
		std::cerr << "失敗: " << e.what() << '\n';
		return false;
	}
	
	SPDLOG_INFO("is_root_file_exists true");
	return std::filesystem::exists(root_file_, ec);
}

/*----------------------------------------------------------
----------------------------------------------------------*/
bool is_sd_card()
{
	if ( !check_proc_mounts() ){
		SPDLOG_INFO("check_proc_mounts false");
		return false;
	}
	
	if( !is_root_file_exists() ) {
		SPDLOG_INFO("is_root_file_exists false");
		return false;
	}
	
	return true;
}

/*----------------------------------------------------------
----------------------------------------------------------*/
bool get_update_file( char* name )
{
	SPDLOG_INFO("get_update_file()");
	const char *bin_dir = "/mnt/sd/bin"; 
	fs::path path(bin_dir);
	std::error_code ec;	
	
	
	//---/mnt/sd/bin ディレクトリの存在確認
	//
	if (!fs::exists(path, ec)) {
		SPDLOG_INFO("no dir");
		return false;
	}
	
	
	//---binフォルダにアップデータファイルがあるかの確認
	//
	for (const fs::directory_entry& x : fs::directory_iterator(PATH_UPDATE)) 
	{
		std::cout << x.path() << std::endl;
		std::string filename = x.path().filename().string();
		std::cout << filename << std::endl;
		
		int pos = filename.find("nvr");
		std::cout << pos << std::endl;
		
		
		//ファイル名にnvrとついたファイルがあるかの確認(実行ファイル)
		//
		if(pos != 0){
			continue;
		}
		
		
		//最初に見つかったファイル名を返す
		//
		std::strcpy(name, filename.c_str());
		return true;
	}
	
	return false;
}

/*----------------------------------------------------------
----------------------------------------------------------*/
bool get_execute_file( char* name )
{
	SPDLOG_INFO("get_execute_file()");
	const char *exe_dir = "/tmp/app_exe"; 
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
	
	
	//"/tmp/app_exe にあるファイル名を取得
	//
	for (const fs::directory_entry& x : fs::directory_iterator(PATH_EXECUTE)) 
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
	const char *exe_dir = "/tmp/app_exe"; 
	fs::path path(exe_dir);
	std::error_code ec;
	
	//---/tmp/app_exeディレクトリの存在確認
	//
	if (!fs::exists(path, ec)) {
		//---ディレクトリがないので作成
		//
		if (!fs::create_directories(path, ec)) {
			SPDLOG_ERROR("Failed to make directory: {}.", path.c_str());
			return false;
		}
		
		//---実行ファイルを作成したディレクトリにコピー
		//
		try
		{
			char exe[256];
			strcpy( exe, PATH_EXECUTE );
			strcat( exe, "nvr");
			fs::copy_file( BUCKUP_EXECUTE, exe);
		}
		catch (const fs::filesystem_error& e)
		{
			std::cerr << "コピーに失敗: " << e.what() << '\n';
			return false;
		}
			SPDLOG_INFO("コピー  OK");	
	}
	
	
	//---実行ファイル名の取得
	//
	char exe_name[256];
	if(!get_execute_file(exe_name)){
		SPDLOG_INFO("get_execute_file false");
		return false;
	}
	
	
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
		char exe_path[256];
		strcpy( exe_path, PATH_EXECUTE );
		strcat( exe_path, exe_name );
		SPDLOG_INFO("execute = {}",exe_path);
		
		
		//実行ファイルを実行
		//
		execl( exe_path  , exe_path, "-r", "now", nullptr);
		SPDLOG_ERROR("Failed to exec nvr.");
		
		
		//子プロセスの終了
		//
		exit(-1);
	}
	
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
	char update_name[256];
	if( get_update_file( update_name ) == false ){
		SPDLOG_INFO("get_update_file() false");
		return false;
	}
	
	
	//---実行ファイルの存在確認
	//
	/*
	char update_path[256];
	strcpy( update_path, PATH_UPDATE );
	strcat( update_path, update_name );
	*/
	fs::path update_path = fs::path(PATH_UPDATE) / update_name;

	char execute_name[256];
	if( get_execute_file( execute_name ) == false )
	{
		SPDLOG_INFO("get_execute_file() false");
		// 実行ファイルが無いのでコピーだけ
		try
		{
			SPDLOG_INFO("実行ファイルが無いのでコピーだけ");
			/*
			strcpy( exe_path, PATH_EXECUTE );
			strcat( exe_path, update_name );
			*/
			fs::path copy_path = fs::path(PATH_EXECUTE) / update_name;
			fs::copy_file( update_path, copy_path,
			fs::copy_options::overwrite_existing);
		}
		catch (const fs::filesystem_error& e)
		{
			std::cerr << "コピーに失敗: " << e.what() << '\n';
			return false;
		}
		
		return true;
	}
	
	
	//---アップデートファイルと実行ファイルが同じファイルか
	//
	SPDLOG_INFO("update_name = {}",update_name);
	SPDLOG_INFO("execute_name = {}",execute_name);
	if( update_name == execute_name ){
		SPDLOG_INFO("update_name == execute_name");
		return false;
	}
	
	
	//--- プロセスを停止
	//
	SPDLOG_INFO(" pid = {}",pid);
	if( pid )
	{
		int status;
		SPDLOG_INFO("PROCESS KILL...");
		kill(pid, SIGTERM);
		waitpid(pid, &status, 0);
		SPDLOG_INFO("PROCESS KILLED!!");
	}
	
	
	//--- 実行ファイルを削除
	//
	SPDLOG_INFO("実行ファイルを削除");
	try
	{
		fs::path del_path = fs::path(PATH_EXECUTE) / execute_name;
		fs::remove( del_path );
		SPDLOG_INFO("del execute = {}",del_path.string());
		/*
		strcpy( exe_path, PATH_EXECUTE );
		strcat( exe_path, execute_name );
		SPDLOG_INFO("del execute = {}",exe_path);
		fs::remove( exe_path );
		*/
	}
	catch (const fs::filesystem_error& e)
	{
		std::cerr << "実行ファイル削除失敗: " << e.what() << '\n';
		return false;
	}
	
	
	//--- アップデートファイルをコピー
	//
	SPDLOG_INFO("アップデートファイルをコピー");	
	try
	{
		fs::path exe_path = fs::path(PATH_EXECUTE) / update_name;
		SPDLOG_INFO("from = {}",update_path.string());
		SPDLOG_INFO("to = {}",exe_path.string());
		fs::copy_file( update_path, exe_path,
		fs::copy_options::overwrite_existing);
		SPDLOG_INFO("コピー  END");	
		/*
		strcpy( exe_path, PATH_EXECUTE );
		strcat( exe_path, update_name );
		SPDLOG_INFO("from = {}",update_path);
		SPDLOG_INFO("to = {}",exe_path);
		fs::copy_file( update_path, exe_path);
		*/
	}
	catch (const fs::filesystem_error& e)
	{
		std::cerr << "コピーに失敗: " << e.what() << '\n';
		return false;
	}
	SPDLOG_INFO("コピー  OK");	
	
	//--- led チカチカ
	//
	for( int i = 0; i < 3; i++ )
	{
		SPDLOG_INFO("led チカチカ");	
		led_board_green->write_value(true);
		led_board_red->write_value(true);
		led_board_yel->write_value(true);
		led_board_green->write_value(false);
		led_board_red->write_value(false);
		led_board_yel->write_value(false);
	}
	
	SPDLOG_INFO("update() true");
	return true;
}

/*----------------------------------------------------------
----------------------------------------------------------*/
int main(int argc, char **argv)
{
	//--- init
	//
	
	spdlog::set_level(spdlog::level::debug);
	SPDLOG_INFO("main-update");
	
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
	
	update();
	execute();
	
	
	//--- timer start
	//
	timer1_id = g_timeout_add_full(G_PRIORITY_HIGH,				// 優先度
									1000,						// タイマー周期 1000msec
									G_SOURCE_FUNC(timer1_cb),	//
									nullptr,					// 
									nullptr);					// 
	if (!timer1_id)
	{
		SPDLOG_ERROR("Failed to add timer1.");
		exit(-1);
	}
	
	
	//---メインループ 作成
	//
	GMainLoop *main_loop = g_main_loop_new(NULL, FALSE);
	
	
	//---console input enable(メインループをコンソールからkillするため)
    //
	signal_int_id  = g_unix_signal_add(SIGINT, G_SOURCE_FUNC(signal_intr_cb), main_loop);
	signal_term_id = g_unix_signal_add(SIGTERM, G_SOURCE_FUNC(signal_term_cb), main_loop);
	
	
	//---メインループ 実行
	//
	g_main_loop_run(main_loop);
	
	
	//---プロセス 終了処理
    //
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
	
	std::exit(0);
	
}

/***********************************************************
	end of file
***********************************************************/


