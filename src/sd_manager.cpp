#include <cerrno>
#include <iostream>
#include <fstream>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/mount.h>
#include <sstream>
#include "sd_manager.hpp"
#include "logging.hpp"

namespace nvr {
    const char *MKFS_PATH = "/sbin/mkfs.vfat";

    const int sd_manager::mount_state_not_mounted = 0;
    const int sd_manager::mount_state_mounting = 1;
    const int sd_manager::mount_state_mounted = 2;
    const int sd_manager::format_result_none = 0;
    const int sd_manager::format_result_success = 1;
    const int sd_manager::format_result_error = 2;

    int sd_manager::start_format()
    {
        SPDLOG_DEBUG("start_format");
        if (thread_.joinable()) {
            SPDLOG_WARN("format thread is running");
            return 0;
        }

        format_result_.store(format_result_none);
        thread_ = std::thread([this]{ this->format_process(); });

        return 0;
    }

    void sd_manager::format_process()
    {
        int result = format_result_none;
        pid_t pid;
        int status = 0;
        int rc = 0;
        std::stringstream tid;

        tid << std::this_thread::get_id();

        SPDLOG_DEBUG(">>> format_process {}", tid.str());
        if (!check_proc_mounts()) {
            rc = mount_sd();
            if (rc == 0 && is_root_file_exists()) {
                result = format_result_success;
                goto END;
            }
        }

        if (check_proc_mounts()) {
            if (is_root_file_exists()) {
                result = format_result_success;
                goto END;
            }

            rc = unmount_sd();
            if (rc) {
                SPDLOG_ERROR("Failed to unmount: {}", strerror(errno));
                result = format_result_error;
                goto END;
            }
        }

        led_manager_->set_status(led_manager::state_sd_formatting);
        SPDLOG_DEBUG("Start mkfs.vfat");
        pid = fork();
        if (pid == 0) {
            execl(MKFS_PATH, MKFS_PATH, "-f", "2", "-F", "32", device_file_.c_str(), nullptr);
            exit(-1);
        } else if (pid < 0) {
            SPDLOG_ERROR("Failed to fork format process: {}", strerror(errno));
            logger_->write("E SDカードフォーマットエラー");
            result = format_result_error;
            led_manager_->set_and_clear_status(
                led_manager::state_error_sd_format,
                led_manager::state_sd_formatting
            );
            goto END;
        }

        waitpid(pid, &status, 0);

        if (!WIFEXITED(status)) {
            SPDLOG_ERROR("mkfs is not exited.");
            logger_->write("E SDカードフォーマットエラー");
            led_manager_->set_and_clear_status(
                led_manager::state_error_sd_format,
                led_manager::state_sd_formatting
            );
            result = format_result_error;
            goto END;
        }

        rc = WEXITSTATUS(status);
        SPDLOG_DEBUG("End mkfs.vfat: {}", rc);
        if (rc != 0) {
            SPDLOG_ERROR("mkfs exit code: {}", rc);
            logger_->write("E SDカードフォーマットエラー");
            led_manager_->set_and_clear_status(
                led_manager::state_error_sd_format,
                led_manager::state_sd_formatting
            );
            result = format_result_error;
            goto END;
        }

        led_manager_->clear_status(
            led_manager::state_sd_formatting | led_manager::state_error_sd_format
        );

        rc = mount_sd();
        if (rc) {
            SPDLOG_ERROR("Failed to mount sd: {}", strerror(errno));
            logger_->write("E SDカードイニシャライズエラー");
            result = format_result_error;
            led_manager_->set_status(led_manager::state_error_sd_init);
            goto END;
        }

        if (!is_root_file_exists()) {
            rc = create_root_file();
            if (rc) {
                logger_->write("E SDカードイニシャライズエラー");
                result = format_result_error;
                led_manager_->set_status(led_manager::state_error_sd_init);
                goto END;
            }
        }

        sync();

        result = format_result_success;
        led_manager_->clear_status(led_manager::state_error_sd_init | led_manager::state_error_sd_format);
    END:
        format_result_.store(result);
        SPDLOG_DEBUG("<<< format_process {} {}", tid.str(), result);
    }

    bool sd_manager::wait_format()
    {
        if (!thread_.joinable()) {
            SPDLOG_WARN("format thread is not joinable.");
            return false;
        }

        auto result = format_result_.load();
        if (result == format_result_none) {
            return false;
        }
        thread_.join();
        SPDLOG_DEBUG("format thread is joined.");

        return true;
    }

    int sd_manager::mount_sd()
    {
        return mount(
            device_file_.c_str(),
            mount_point_.c_str(),
            "vfat",
            MS_NOATIME|MS_NOEXEC,
            "errors=continue"
        );
    }

    int sd_manager::unmount_sd() {
        return umount(mount_point_.c_str());
    }

    void sd_manager::timer_process()
    {
        int status = get_mount_status();
        int counter = counter_;

        if (counter == 20) {
            counter_ = 0;
        } else {
            counter_++;
        }

        if (counter != 0) {
            return;
        }

        if (status == mount_state_mounted) {
            if (!check_mount_point()) {
                status = mount_state_not_mounted;
            }
        }

        if (status == mount_state_not_mounted) {
            // SPDLOG_DEBUG("timer_process not_mounted");
            led_manager_->set_status(led_manager::state_sd_waiting);
            if (check_proc_mounts() && is_root_file_exists()) {
                set_mount_status(mount_state_mounted);
                led_manager_->clear_status(led_manager::state_sd_all);
            } else if (is_device_file_exists()) {
                set_mount_status(mount_state_mounting);
                start_format();
            }
            return;
        }

        if (status == mount_state_mounting) {
            // SPDLOG_DEBUG("timer_process mounting");
            if (wait_format()) {
                auto result = format_result_.load();
                if (result == format_result_success) {
                    SPDLOG_DEBUG("Set mount_status_t to mounted");
                    set_mount_status(mount_state_mounted);
                    led_manager_->clear_status(led_manager::state_sd_all);

                    return;
                }
                SPDLOG_DEBUG("Set mount_status_t to not_mounted");
            }
            set_mount_status(mount_state_not_mounted);

            return;
        }
    }

    bool sd_manager::check_proc_mounts()
    {
        static const std::filesystem::path proc_mounts{"/proc/mounts"};
        static const char *dev_file = device_file_.c_str();

        try
        {
            std::ifstream ifs(proc_mounts);
            std::string line;

            while (std::getline(ifs, line))
            {
                if (line.find(dev_file) != std::string::npos)
                {
                    // led_manager_->clear_status(led_manager::state_sd_waiting);
                    return true;
                }
            }
        }
        catch (std::exception &ex)
        {
            SPDLOG_ERROR("Failed to check mout point: {}.", ex.what());
        }

        // led_manager_->set_status(led_manager::state_sd_waiting);
        return false;
    }

    bool sd_manager::check_mount_point()
    {
        int status = get_mount_status();
        if (status == mount_state_mounted) {
            if (is_root_file_exists()) {
                return true;
            }
            set_mount_status(mount_state_not_mounted);
        }
        return false;
    }

    int sd_manager::create_root_file()
    {
        int rc;
        int fd = open(root_file_.c_str(), O_CREAT | O_WRONLY | O_TRUNC | O_CLOEXEC, S_IRWXU);
        if (fd == -1) {
            SPDLOG_ERROR("Failed to open {}: {}", root_file_.c_str(), strerror(errno));
            return -1;
        }

        const char *buf = "1";
        rc = write(fd, buf, 1);
        if (rc < 0) {
            SPDLOG_ERROR("Failed to write {}: {}", root_file_.c_str(), strerror(errno));
            close(fd);
            return -1;
        }

        // rc = syncfs(fd);
        // if (rc < 0) {
        //     SPDLOG_ERROR("Failed to sync {}: {}", root_file_.c_str(), strerror(errno));
        //     close(fd);
        //     return -1;
        // }

        close(fd);

        // return sync_dir(root_file_.parent_path());
        return 0;
    }

    // int sd_manager::sync_dir(const std::filesystem::path& path)
    // {
    //     int rc = 0;
    //     int fd = open(path.c_str(), O_DIRECTORY | O_CLOEXEC);

    //     if (fd == -1) {
    //         SPDLOG_ERROR("Failed to open {}: {}", path.c_str(), strerror(errno));
    //         return -1;
    //     }

    //     rc = fsync(fd);
    //     close(fd);

    //     return rc;
    // }
}