#ifndef _NVR_UPDATE_MANAGER_HPP_
#define _NVR_UPDATE_MANAGER_HPP_

#include <atomic>
#include <filesystem>
#include <thread>
#include "logging.hpp"
#include "gpio.hpp"

namespace nvr {
    class update_manager
    {
    public:
        static const int mount_state_not_mounted;
        static const int mount_state_mounting;
        static const int mount_state_mounted;
        static const int update_none;
        static const int update_start;
        static const int update_ing;
        static const int update_err1;
        static const int update_err2;
        static const int update_ok;

        explicit update_manager(
            const char *device_file,
            const char *mount_point,
            const char *root_file,
            const char *update_file,
            const char *nvr_file,
            std::shared_ptr<logger> logger
        ) noexcept : device_file_(device_file),
              mount_point_(mount_point),
              root_file_(root_file),
              update_file_(update_file),
              nvr_file_(nvr_file),
              logger_(logger),
              mount_status_(0),
              update_result_(0),
              update_status_(0),
              nvr_pid_(-1)
        {}
       

        update_manager() = delete;
        update_manager(const update_manager &) = delete;
        update_manager(update_manager &&) = delete;
        ~update_manager() = default;

        void timer_process(); 
        int start_update_proc();  
        int get_update_status();   
        
        inline void set_pid(pid_t val)
		{
		    nvr_pid_.store(val);
		}

		
		inline pid_t get_pid()
		{
		    return nvr_pid_.load();
		}
    private:
        bool wait_update();
        void update_process();

        inline bool is_update_file_exists() noexcept
        {
            std::error_code ec;
            return std::filesystem::exists(update_file_, ec);
        }


        inline int get_mount_status()
        {
            return mount_status_.load();
        }

        bool copy_file(const std::filesystem::path &src, const std::filesystem::path &dst);

        const std::filesystem::path device_file_;
        const std::filesystem::path mount_point_;
        const std::filesystem::path root_file_;
        const std::filesystem::path update_file_;
        const std::filesystem::path nvr_file_;
        std::thread thread_;
        std::atomic<int> update_result_;
        std::atomic<int> mount_status_;
        std::atomic<int> update_status_;    
        std::shared_ptr<logger> logger_;
        std::atomic<pid_t> nvr_pid_;    

    };
}

#endif



