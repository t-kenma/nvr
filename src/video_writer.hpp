#ifndef __NVR_WIDEO_WRITER__
#define __NVR_WIDEO_WRITER__
#include <string>
#include <functional>
#include <filesystem>
#include <mutex>
#include <condition_variable>
#include <tuple>

#include "gpio.hpp"
#include "sd_manager.hpp"

#include "logging.hpp"
// #define NVR_DEBUG_POWER

namespace nvr
{
    class video_writer
    {
    public:
        explicit video_writer(
            const char *src_dir,
            const char *dst_dir)
            : src_dir_(src_dir),
              dst_dir_(dst_dir),
              active_(true),
              power_down_(false),
              writing_(false)
        {
        }
        video_writer() = delete;
        video_writer(const video_writer &) = delete;
        video_writer(video_writer &&) = delete;
        ~video_writer() = default;

        inline std::function<void()> process()
        {
            return [this]()
            {
                this->loop();
            };
        }

        constexpr void stop() { active_ = false; }
        inline void set_powerdown() { 
            power_down_.store(true, std::memory_order_relaxed);
            active_ = false;
        }

        inline void notify() { 
            std::lock_guard<std::mutex> lock(mtx_);
            cv_.notify_one(); 
        }

        inline void set_led_manager(std::shared_ptr<led_manager> led_manager) {
            led_manager_ = led_manager;
        }

        inline void set_sd_manager(std::shared_ptr<sd_manager> sd_manager) {
            sd_manager_ = sd_manager;
        }

        inline void set_logger(std::shared_ptr<logger> logger) {
            logger_ = logger;
        }

#ifdef NVR_DEBUG_POWER
        std::shared_ptr<nvr::gpio_out> tmp_out2_;
#endif
    protected:
        void loop();
        void move_file(const std::filesystem::path& file);

    private:
        bool should_move_file();
        int sync_dir(const std::filesystem::path &path);
        bool copy_file(const std::filesystem::path &src, const std::filesystem::path &dst);

        std::tuple<std::uintmax_t, bool> delete_old_files_(
            const std::filesystem::path &dir,
            std::uintmax_t size,
            bool can_delete_dir
        );
        inline bool delete_old_files(const std::filesystem::path &dir, std::uintmax_t size) {
            const auto ret = delete_old_files_(dir, size, false);
            return std::get<0>(ret) >= size;
        }
        bool check_volume_size(const std::filesystem::path &dir, size_t size);


        std::filesystem::path src_dir_;
        std::filesystem::path dst_dir_;
        bool active_;
        std::mutex mtx_;
        std::condition_variable cv_;
        std::shared_ptr<led_manager> led_manager_;
        std::shared_ptr<sd_manager> sd_manager_;
        std::atomic<bool> power_down_;
        std::atomic<bool> writing_;
        std::mutex box_state_mutex_;
        std::shared_ptr<logger> logger_;
    };
}

#endif
