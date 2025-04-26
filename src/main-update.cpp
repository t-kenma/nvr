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
#include "sd_manager.hpp"
#include "util.hpp"
#include "logging.hpp"
#include <glib-unix.h>

struct callback_data_t {
    callback_data_t()
        : 
          signal_int_id(0),
          signal_term_id(0)
    {}
    guint signal_int_id;
    guint signal_term_id;

    std::atomic<bool> interrupted;
    
};

int loop = 1;

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


int main(int argc, char **argv)
{
    int rc;
    pid_t pid;
    int status;
    
    spdlog::set_level(spdlog::level::debug);
    SPDLOG_INFO("main-update");
    
    callback_data_t data{};

    std::shared_ptr<nvr::gpio_out> led_board_green = std::make_shared<nvr::gpio_out>("193", "P9_1");
    std::shared_ptr<nvr::gpio_out> led_board_red = std::make_shared<nvr::gpio_out>("200", "P10_0");
    std::shared_ptr<nvr::gpio_out> led_board_yel = std::make_shared<nvr::gpio_out>("192", "P9_0");
    std::shared_ptr<nvr::logger> logger = std::make_shared<nvr::logger>("/etc/nvr/video-recorder.log");
    
    data.signal_int_id = g_unix_signal_add(SIGINT, G_SOURCE_FUNC(signal_intr_cb), &data);
    data.signal_term_id = g_unix_signal_add(SIGTERM, G_SOURCE_FUNC(signal_term_cb), &data);
    
 	pid = fork();
    if (pid < 0) {
        SPDLOG_ERROR("Failed to fork process: {}", strerror(errno));
        return -1;
    } else if (pid == 0) {
        execl("/usr/bin/nvr", "/usr/bin/nvr", "-r", "now", nullptr);
        SPDLOG_ERROR("Failed to exec nvr.");
        exit(-1);
    }
    
    while(loop){
    
    }
    
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



