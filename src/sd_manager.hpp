#ifndef _NVR_SD_MANAGER_HPP_
#define _NVR_SD_MANAGER_HPP_

#include <atomic>
#include <filesystem>
#include <thread>
#include "logging.hpp"
#include "gpio.hpp"

namespace nvr {
    class sd_manager
    {
    public:
        static const int mount_state_not_mounted;
        static const int mount_state_mounting;
        static const int mount_state_mounted;
        static const int format_result_none;
        static const int format_result_success;
        static const int format_result_error;
        static const int update_result_none;
        static const int update_result_success;
        static const int update_result_error;

        explicit sd_manager(
            const char *device_file,
            const char *mount_point,
            const char *nvr_file,
            std::shared_ptr<logger> logger
        ) noexcept : device_file_(device_file),
              mount_point_(mount_point),
              nvr_file_(nvr_file),
              logger_(logger),
              format_result_(0),
              mount_status_(0),
              counter_(0)
        {
        }

        sd_manager() = delete;
        sd_manager(const sd_manager &) = delete;
        sd_manager(sd_manager &&) = delete;
        ~sd_manager() = default;

        void timer_process();
        bool check_mount_point();
        bool is_sd_card();
        bool is_root_file_exists() noexcept;
        int start_format();
        int is_formatting();
        bool check_proc_mounts();
        uint64_t get_sector_count();
        int unmount_sd();
        int mount_sd();
        inline bool is_device_file_exists() noexcept        
        {
            std::error_code ec;
            return std::filesystem::exists(device_file_, ec);
        }

        // int sync_dir(const std::filesystem::path& path);
    private:
        bool wait_format();
        int start_update();
        bool wait_update();
        void format_process();
        void update_process();

        inline bool is_update_file_exists() noexcept
        {
            std::error_code ec;
            return std::filesystem::exists(update_file_, ec);
        }

        inline void set_mount_status(int status)
        {
            mount_status_.store(status);
        }

        inline int get_mount_status()
        {
            return mount_status_.load();
        }

        int create_root_file();
        bool copy_file(const std::filesystem::path &src, const std::filesystem::path &dst);


        const std::filesystem::path device_file_;
        const std::filesystem::path mount_point_;
        const std::filesystem::path root_file_;
        const std::filesystem::path update_file_;
        const std::filesystem::path nvr_file_;
        std::thread thread_;
        std::atomic<int>  format_result_;
        std::atomic<int>  update_result_;
        std::atomic<int> mount_status_;
        std::shared_ptr<logger> logger_;
        int counter_;
    };
}

#endif






