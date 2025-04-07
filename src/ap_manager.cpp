#include "ap_manager.hpp"
#include <filesystem>
#include <cerrno>
#include <thread>
#include <chrono>
#include <iostream>
#include <fstream>

#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <net/if.h>
#include <nlohmann/json.hpp>

#include "util.hpp"
#include "logging.hpp"

namespace nvr {
    const std::filesystem::path restart_file{"/tmp/nvr-restart"};

    bool use_dhcp() noexcept
    {
        const std::filesystem::path config_file("/etc/nvr/network.json");

        try {
            std::ifstream in(config_file);
            auto json = nlohmann::json::parse(in);

            if (json.contains("use_dhcp")) {
                return (json["use_dhcp"].get<int>()) ? true : false;
            }
        } catch(const std::exception &err) {
            SPDLOG_WARN("Failed to read network.json: {}", err.what());
        }

        return true;
    }

    int ap_manager::create_restart_file()
    {
        int rc;
        int fd = ::open(restart_file.c_str(), O_CREAT | O_WRONLY | O_TRUNC | O_CLOEXEC, S_IRWXU);
        if (fd == -1) {
            SPDLOG_ERROR("Failed to open {}: {}", restart_file.c_str(), strerror(errno));
            return -1;
        }

        const char *buf = "1";
        rc = write(fd, buf, 1);
        if (rc < 0) {
            SPDLOG_ERROR("Failed to write {}: {}", restart_file.c_str(), strerror(errno));
            close(fd);
            return -1;
        }

        close(fd);

        return 0;
    }

    bool is_restart_file_exists() noexcept
    {
        std::error_code ec;
        return std::filesystem::exists(restart_file, ec);
    }

    bool remove_restart_file() noexcept
    {
        std::error_code ec;
        return std::filesystem::remove(restart_file, ec);
    }

    bool is_process_running(const std::filesystem::path& pidpath) noexcept
    {
        bool ret = false;
        std::error_code ec;

        if (!std::filesystem::exists(pidpath, ec)) {
            return false;
        }

        int fd = ::open(pidpath.c_str(), O_RDONLY | O_CLOEXEC);
        if (fd != -1) {
            char buf[32] = {'/', 'p', 'r', 'o', 'c', '/'};
            ssize_t rsize = read(fd, static_cast<void*>(buf + 6), 22);

            if (rsize > 1) {
                rsize += 6;
                if (buf[rsize - 1] == '\n') {
                    buf[rsize - 1] = '\0';
                } else {
                    buf[rsize] = '\0';
                }

                if (std::filesystem::is_directory(buf, ec)) {
                    ret = true;
                }
            }
            close(fd);
        }

        return ret;
    }

    void ap_manager::check_restart_file()
    {
        if (is_restart_file_exists()) {
            start();
        }
    }

    GIOStatus ap_manager::update_value()
    {
        GIOStatus ret = G_IO_STATUS_AGAIN;
        guchar new_value;

        {
            std::lock_guard<std::mutex> lock(mtx_);
            if (thread_.joinable()) {
                if (!active_.load(std::memory_order_relaxed)) {
                    thread_.join();
                }
                return G_IO_STATUS_NORMAL;
            }
        }

        ret = read_value(&new_value);

        if (ret != G_IO_STATUS_NORMAL) {
            return ret;
        }

        if (value_ == new_value) {
            count_++;
        } else {
            value_ = new_value;
            count_ = 0;
        }

        if (value_ == 0 && count_ == 40) {
            start();
        }

        return ret;
    }

    void ap_manager::start()
    {
        std::lock_guard<std::mutex> lock(mtx_);

        if (!thread_.joinable()) {
            active_.store(true, std::memory_order_relaxed);
            thread_ = std::thread([this]{ loop(); });
            SPDLOG_DEBUG("Start ap_manager thread.");
        }
    }

    void ap_manager::stop()
    {
        std::lock_guard<std::mutex> lock(mtx_);

        if (thread_.joinable()) {
            active_.store(false, std::memory_order_relaxed);
            thread_.join();
        }
    }

    void ap_manager::loop()
    {
        int max = 180; // wait 3 minutes
        int counter = 0;
        int status = 0;
        int associated = 0;

        ctrl_interface(true);

        start_hostapd();

        if (update_txpower()) {
            SPDLOG_ERROR("Failed to set wlan0 txpower to {}.", txpower_);
        }

        if (use_dhcp()) {
            start_dhcpd();
        } else {
            stop_dhcpd();
        }

        if (!is_restart_file_exists()) {
            logger_->write("L AP起動");
        } else {
            remove_restart_file();
        }

        while(active_.load(std::memory_order_relaxed)) {
            if (!is_hostapd_running()) {
                video_writer_->set_station_associated(false);
                break;
            }

            associated = is_station_associated();
            if (associated == 1) {
                video_writer_->set_station_associated(true);
                if (status == 0) {
                    status = 1;
                    logger_->write("L AP接続");
                }
            } else if (associated == 0) {
                if (status == 1) {
                    status = 2;
                    logger_->write("L AP切断");
                }
                video_writer_->set_station_associated(false);
            }

            if (status == 2 || (counter >= max && status != 1)) {
                SPDLOG_INFO("Stop hostapd: {}", status);

                if (is_dhcpd_running()) {
                    stop_dhcpd();
                }

                if (stop_hostapd() == 0) {
                    break;
                }
            }

            counter++;
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }

        if (is_restart_file_exists()) {
            if (!is_hostapd_running()) {
                remove_restart_file();
            }
        }

        if (!is_restart_file_exists()) {
            if (is_dhcpd_running()) {
                stop_dhcpd();
            }

            if (is_hostapd_running()) {
                stop_hostapd();
            }

            ctrl_interface(false);
            video_writer_->set_station_associated(false);

            logger_->write("L AP停止");
        }


        SPDLOG_DEBUG("ap_manager finished.");
        active_.store(false, std::memory_order_relaxed);
    }

    bool ap_manager::is_hostapd_running() const noexcept
    {
        bool ret = is_process_running("/var/run/hostapd.pid");
        if (ret) {
            SPDLOG_DEBUG("hostapd is running.");
        } else {
            SPDLOG_INFO("hostapd is not running.");
        }
        return ret;
    }

    bool ap_manager::is_dhcpd_running() const noexcept
    {
        bool ret = is_process_running("/var/run/dhcpd.pid");
        if (ret) {
            SPDLOG_DEBUG("dhcpd is running.");
        } else {
            SPDLOG_INFO("dhcpd is not running.");
        }
        return ret;
    }

    int ap_manager::start_dhcpd() const noexcept
    {
        std::error_code ec;
        const std::filesystem::path lease_path = "/var/lib/dhcp/dhcpd.leases";

        if (std::filesystem::exists(lease_path, ec)) {
            std::filesystem::remove(lease_path, ec);
        }

        return ctrl_service("dhcpd", true);
    }

    int ap_manager::is_station_associated() const noexcept
    {
        int ret = 0;
        int rc;
        pid_t pid;
        const char *cmdline = "iw dev wlan0 station dump";

        int pipe_in[2];
        int pipe_out[2];
        int pipe_err[2];

        rc = pipe2(pipe_in, O_CLOEXEC);
        if (rc) {
            SPDLOG_ERROR("Failed to open pipe in: {}", strerror(errno));
            return -1;
        }

        rc = pipe2(pipe_out, O_CLOEXEC);
        if (rc) {
            SPDLOG_ERROR("Failed to open pipe out: {}", strerror(errno));
            return -1;
        }

        rc = pipe2(pipe_err, O_CLOEXEC);
        if (rc) {
            SPDLOG_ERROR("Failed to open pipe err: {}", strerror(errno));
            return -1;
        }

        pid = fork();
        if (pid < 0) {
            SPDLOG_ERROR("Failed to fork: {}", strerror(errno));
            close(pipe_in[0]);
            close(pipe_in[1]);
            close(pipe_out[0]);
            close(pipe_out[1]);
            close(pipe_err[0]);
            close(pipe_err[1]);

            return -1;
        } else if (pid == 0) {
            close(pipe_in[1]);
            close(pipe_out[0]);
            close(pipe_err[0]);

            if (dup2(pipe_in[0], 0) == -1) {
                SPDLOG_ERROR("Failed to dup stdin.");
                close(pipe_in[0]);
                close(pipe_out[1]);
                close(pipe_err[1]);
                exit(-1);
            }
            if (dup2(pipe_out[1], 1) == -1) {
                SPDLOG_ERROR("Failed to dup stdout.");
                close(pipe_in[0]);
                close(pipe_out[1]);
                close(pipe_err[1]);
                exit(-1);
            }
            if (dup2(pipe_err[1], 2) == -1) {
                SPDLOG_ERROR("Failed to dup stderr.");
                close(pipe_in[0]);
                close(pipe_out[1]);
                close(pipe_err[1]);
                exit(-1);
            }

            execl("/usr/sbin/iw", "/usr/sbin/iw", "dev", "wlan0", "station", "dump", nullptr);
            SPDLOG_ERROR("Failed to exec iw: {}", strerror(errno));
            exit(-1);
        } else {
            int status;
            close(pipe_in[0]);
            close(pipe_out[1]);
            close(pipe_err[1]);

            while (true) {
                char buf[1024];
                ssize_t rsize = read(pipe_out[0], static_cast<void*>(buf), 1023);

                SPDLOG_DEBUG("read from pipe:{}", rsize);
                if (rsize == 0) {
                    break;
                }

                if (rsize < 0) {
                    SPDLOG_ERROR("Failed to read from pipe: {}", strerror(errno));
                    break;
                }

                buf[rsize] = '\0';

                SPDLOG_DEBUG("iw output: {}", buf);
                if (strstr(buf, "Station") != nullptr) {
                    ret = 1;
                }
            }

            waitpid(pid, &status, 0);
            close(pipe_in[1]);
            close(pipe_out[0]);
            close(pipe_err[0]);
        }

        SPDLOG_DEBUG("associated stations: {}", ret);
        
        return ret;
    }

    int ap_manager::ctrl_service(const char *service, bool start) const noexcept
    {
        int rc = -1;
        if (start) {
             rc = do_systemctl("start", service);
             SPDLOG_DEBUG("start {}: {}", service, rc);
             return rc;
        }
        rc = do_systemctl("stop", service);
        SPDLOG_DEBUG("stop {}: {}", service, rc);
        return rc;
    }

    int ap_manager::update_txpower() noexcept
    {
        return exec_command("/usr/sbin/iw", "dev", "wlan0", "set", "txpower", "limit", txpower_.c_str());
    }

    int ap_manager::ctrl_interface(bool up)
    {
        int soc;
        struct ifreq ifr;

        soc = socket(AF_INET, SOCK_DGRAM, 0);
        if (soc < 0) {
            return -1;
        }

        strncpy(ifr.ifr_name, "wlan0", IFNAMSIZ - 1);

        if (ioctl(soc, SIOCGIFFLAGS, &ifr) < 0) {
            return -1;
        }

        if (ifr.ifr_flags & IFF_UP) {
            if (!up) {
                ifr.ifr_flags &= ~IFF_UP;

                if (ioctl(soc, SIOCSIFFLAGS, &ifr) != 0) {
                    return -1;
                }
                SPDLOG_DEBUG("Down wlan0.");
            }
        } else {
            if (up) {
                ifr.ifr_flags |= IFF_UP;

                if (ioctl(soc, SIOCSIFFLAGS, &ifr) != 0) {
                    return -1;
                }

                SPDLOG_DEBUG("Up wlan0.");
            }
        }

        return 0;
    }
}