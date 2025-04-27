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
#include "gpio.hpp"
#include "update_manager.hpp"
#include "util.hpp"
#include "logging.hpp"
#include <glib-unix.h>

struct callback_data_t {
    callback_data_t()
        : 
          signal_int_id(0),
          signal_term_id(0),
          timer1_id(0),
          update_manager(nullptr)
    {}
    guint signal_int_id;
    guint signal_term_id;
    guint timer1_id;

    std::atomic<bool> interrupted;
    nvr::update_manager *update_manager;
    GMainLoop *main_loop;
};

std::atomic<pid_t> nvr_pid_;
std::thread thread_;
int loop = 1;
namespace fs = std::filesystem;

static gboolean timer1_cb(gpointer udata)
{
    callback_data_t *data = static_cast<callback_data_t *>(udata);

    data->update_manager->timer_process();

    return G_SOURCE_CONTINUE;
}

static void on_interrupted(callback_data_t *data)
{
    loop = 0;
}

static gboolean signal_intr_cb(gpointer udata)
{
    SPDLOG_INFO("SIGINTR receiverd.");

    callback_data_t* data = static_cast<callback_data_t*>(udata);

    on_interrupted(data);

    /* remove signal handler */
    data->signal_int_id = 0;
    return G_SOURCE_REMOVE;
}

static gboolean signal_term_cb(gpointer udata)
{
    SPDLOG_INFO("SIGTERM receiverd.");

    callback_data_t* data = static_cast<callback_data_t*>(udata);

    on_interrupted(data);

    /* remove signal handler */
    data->signal_term_id = 0;
    return G_SOURCE_REMOVE;
}

/*
void update_proc(callback_data_t* data)
{
	while(1)
	{
		SPDLOG_INFO("update_proc");
		int status = data->update_manager->get_update_status();
		if( status == 1 )
			{
				pid_t pid = nvr_pid_.load();
				kill(pid, SIGTERM);
				waitpid(pid, &status, 0);
				nvr_pid_.store(-1);
				data->update_manager->start_update();
			}
			else if( status == 5)
			{
			pid_t new_pid = fork();
			if (new_pid < 0) {
				SPDLOG_ERROR("Failed to fork process: {}", strerror(errno));
			} else if (new_pid == 0) {
				nvr_pid_.store(new_pid);
				execl("/usr/bin/nvr", "/usr/bin/nvr", "-r", "now", nullptr);
				SPDLOG_ERROR("Failed to exec nvr.");
				exit(-1);
			}      
		}
		sleep(1);
	}
}
*/

/*
int update_proc_start(callback_data_t* data)
{
    SPDLOG_DEBUG("update_proc_start");
    if (thread_.joinable()) {
        SPDLOG_WARN("update_proc_start thread is running");
        return 0;
    }

    thread_ = std::thread(update_proc, data);

    return 0;
}
*/

int main(int argc, char **argv)
{
    int rc;
    pid_t pid;
    int status;
    
    spdlog::set_level(spdlog::level::debug);
    SPDLOG_INFO("main-update");
	std::cout << "aaaaaa" << std:: endl;
    
    callback_data_t data{};

    std::shared_ptr<nvr::gpio_out> led_board_green = std::make_shared<nvr::gpio_out>("193", "P9_1");
    std::shared_ptr<nvr::gpio_out> led_board_red = std::make_shared<nvr::gpio_out>("200", "P10_0");
    std::shared_ptr<nvr::gpio_out> led_board_yel = std::make_shared<nvr::gpio_out>("192", "P9_0");
    std::shared_ptr<nvr::logger> logger = std::make_shared<nvr::logger>("/etc/nvr/video-recorder.log");

    if (led_board_green->open(false)) {
        SPDLOG_ERROR("Failed to open led_board_green.");
        exit(-1);
    }

    if (led_board_red->open(false)) {
        SPDLOG_ERROR("Failed to open led_board_red.");
        exit(-1);
    }

    if (led_board_yel->open(false)) {
        SPDLOG_ERROR("Failed to open led_board_yel.");
        exit(-1);
    }


    std::shared_ptr<nvr::update_manager> update_manager = std::make_shared<nvr::update_manager>(
        "/dev/mmcblk1p1",
        "/mnt/sd",
        "/mnt/sd/.nrs_video_data",
        "/mnt/sd/nvr",
        "/usr/bin/nvr/nvr",
        logger
    );
    
    if(!fs::exists("usr/bin/nvr/"))
    {
    	SPDLOG_INFO("create_directory");
		
		try 
        {
	        fs::create_directory("/usr/bin/nvr/ ");
        }
        catch (const fs::filesystem_error& e) 
        {
            std::cerr << "create_directoryni失敗: " << e.what() << '\n';
            //goto END;
        }

		try 
        {
			fs::copy_file("/usr/bin/nvr","/usr/bin/nvr/", fs::copy_options::overwrite_existing);
        }
        catch (const fs::filesystem_error& e) 
        {
            std::cerr << "コピーに失敗: " << e.what() << '\n';
            //goto END;
        }
    }
    
    SPDLOG_INFO("sleep end");
    /*
	led_board_green->write_value(true);
	led_board_red->write_value(true);
	led_board_yel->write_value(true);
	*/
    
    data.timer1_id = g_timeout_add_full(
        G_PRIORITY_HIGH,
        1000,
        G_SOURCE_FUNC(timer1_cb),
        &data,
        nullptr);
    if (!data.timer1_id)
    {
        SPDLOG_ERROR("Failed to add timer1.");
        exit(-1);
    }
    
    pid = fork();
    data.update_manager->set_pid(pid);
    SPDLOG_INFO("fork");
	if (pid < 0) {
		SPDLOG_ERROR("Failed to fork process: {}", strerror(errno));
		return -1;
	} else if (pid == 0) {
		execl("/usr/bin/nvr/nvr", "/usr/bin/nvr/nvr", "-r", "now", nullptr);
		SPDLOG_ERROR("Failed to exec nvr.");
		exit(-1);
	} 
	
	data.signal_int_id = g_unix_signal_add(SIGINT, G_SOURCE_FUNC(signal_intr_cb), &data);
    data.signal_term_id = g_unix_signal_add(SIGTERM, G_SOURCE_FUNC(signal_term_cb), &data);
    data.signal_term_id = g_unix_signal_add(SIGTERM, G_SOURCE_FUNC(signal_term_cb), &data);
    
    data.update_manager->start_update_proc();
    
    data.main_loop = g_main_loop_new(nullptr, FALSE);
    g_main_loop_run(data.main_loop);

    kill(pid, SIGTERM);
    waitpid(pid, &status, 0);
    SPDLOG_INFO("nvr kill");
    
    if (data.signal_int_id)
    {
        g_source_remove(data.signal_int_id);
    }
    if (data.signal_term_id)
    {
        g_source_remove(data.signal_term_id);
    }
    
    std::exit(0);
}


