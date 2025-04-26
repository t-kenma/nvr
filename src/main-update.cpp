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


int main(int argc, char **argv)
{
    int rc;
    pid_t pid;
    int status;
    
    spdlog::set_level(spdlog::level::debug);
    SPDLOG_INFO("main-update");

    std::shared_ptr<nvr::gpio_out> led_board_green = std::make_shared<nvr::gpio_out>("193", "P9_1");
    std::shared_ptr<nvr::gpio_out> led_board_red = std::make_shared<nvr::gpio_out>("200", "P10_0");
    std::shared_ptr<nvr::gpio_out> led_board_yel = std::make_shared<nvr::gpio_out>("192", "P9_0");
    std::shared_ptr<nvr::logger> logger = std::make_shared<nvr::logger>("/etc/nvr/video-recorder.log");
    
 	pid = fork();
    if (pid < 0) {
        SPDLOG_ERROR("Failed to fork process: {}", strerror(errno));
        return -1;
    } else if (pid == 0) {
        execl("/usr/bin/nvr", "/usr/bin/nvr", "-r", "now", nullptr);
        SPDLOG_ERROR("Failed to exec nvr.");
        exit(-1);
    }
    
    waitpid(pid, &status, 0);
    
    std::exit(0);
}



