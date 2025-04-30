#ifndef __NVR_LED_MANAGERMA_HPP__
#define __NVR_LED_MANAGERMA_HPP__

#ifndef SPDLOG_ACTIVE_LEVEL
#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_INFO
#endif
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

namespace nvr {
    class led_manager
    {
        public:
        static const int blink;
        static const int one;
        static const int two;
        static const int off;
        static const int on;
    
        explicit led_manager() noexcept;
    
        led_manager(const led_manager &) = delete;
        led_manager(led_manager &&) = delete;
        ~led_manager() = default;
    
        void update_led();    
        void set_g(int v) {
            SPDLOG_INFO("set_green_type {}", v);
            g_type_ = v;
        }
        void set_r(int v) {
            r_type_ = v;
        }
    
    private:
        void init();    

        void set_green(bool v) {
            grn_->write_value(v);
        }
    
        void set_red(bool v) {
            red_->write_value(v);
        }
    
        int g_type_;
        int r_type_;
        int g_counter_;
        int r_counter_;
		std::shared_ptr<nvr::gpio_out> red_;
		std::shared_ptr<nvr::gpio_out> yel_;
		std::shared_ptr<nvr::gpio_out> grn_;
	
    };
}
#endif
