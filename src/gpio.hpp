#ifndef __NVR_GPIO_HPP__
#define __NVR_GPIO_HPP__

#include <atomic>
#include <filesystem>
#include <unistd.h>
#include <mutex>
#include <gst/gst.h>
#include "logging.hpp"

namespace nvr
{
    class gpio_base
    {
    public:
        explicit gpio_base(const char *port_number, const char *port_name) noexcept
            : port_number_(port_number),
              port_name_(port_name)
        {}
        gpio_base() = delete;
        gpio_base(const gpio_base &) = delete;
        gpio_base(gpio_base &&) = delete;

    protected:
        virtual ~gpio_base(){};
        int open_port(const char *direction, const char *edge = nullptr) noexcept;

        const std::string port_number_;
        const std::string port_name_;
        std::filesystem::path value_path_;
    };

    class gpio_out: public gpio_base
    {
    public:
        explicit gpio_out(const char *port_number, const char *port_name) noexcept
            : gpio_base(port_number, port_name),
              fd_(-1)
        {}
        gpio_out() = delete;
        gpio_out(const gpio_out &) = delete;
        gpio_out(gpio_out &&) = delete;

        virtual ~gpio_out()
        {
            if (fd_ != -1) {
                close(fd_);
                fd_ = -1;
            }
        }

        int open(bool high = false);
        int write_value(bool value);

    private:
        int fd_;
    };

    class gpio_in: public gpio_base
    {
    public:
        explicit gpio_in(const char *port_number, const char *port_name) noexcept
            : gpio_base(port_number, port_name),
              channel_(nullptr)
        {}
        gpio_in() = delete;
        gpio_in(const gpio_in &) = delete;
        gpio_in(gpio_in &&) = delete;

        int open(const char *edge = nullptr);
        GIOStatus read_value(guchar *v);

        virtual ~gpio_in()
        {
            if (channel_) {
                g_io_channel_shutdown(channel_, TRUE, nullptr);
                g_io_channel_unref(channel_);
                channel_ = nullptr;
            }
        }

    protected:
        GIOChannel* channel_;
    };

    class gpio_monitor: public gpio_in
    {
    public:
        enum update_state {
            error,
            unchanged,
            changed_to_high,
            changed_to_low,
        };

        explicit gpio_monitor(const char *port_number, const char *port_name, guchar default_value) noexcept
            : gpio_in(port_number, port_name),
              value_(default_value)
        {}
        gpio_monitor() = delete;
        gpio_monitor(const gpio_base &) = delete;
        gpio_monitor(gpio_base &&) = delete;
        virtual ~gpio_monitor() = default;

        guint add_watch(GIOFunc func, gpointer user_data);
        update_state update_value();
        constexpr guchar value() { return value_; }
        constexpr bool is_high() { return value_ == 1; }
    private:
        guchar value_; 
    };

    class led_manager
    {
    public:
        static const int state_none;
        static const int state_error_sd_init;
        static const int state_error_sd_format;
        static const int state_error_sd_dir;
        static const int state_error_sd_file;
        static const int state_error_video;
        static const int state_error_other;
        static const int state_error;
        static const int state_recording;
        static const int state_sd_waiting;
        static const int state_sd_formatting;
        static const int state_box_open;
        static const int state_station_associated;
        static const int state_wifi_active;
        static const int state_resetting;
        static const int state_sd_all;
        
        explicit led_manager(
            std::shared_ptr<gpio_out> green,
            std::shared_ptr<gpio_out> red,
            std::shared_ptr<gpio_out> alarm_a,
            std::shared_ptr<gpio_out> alarm_b
        ) noexcept
        : green_(green),
          red_(red),
          alarm_a_(alarm_a),
          alarm_b_(alarm_b),
          status_(0),
          prev_status_(0),
          counter_(0),
          alarm_state_(false)
        {}
        led_manager() = delete;
        led_manager(const led_manager &) = delete;
        led_manager(led_manager &&) = delete;
        ~led_manager() = default;

        inline int get_status()
        {
            return status_.load();
        }

        // inline bool is_sd_waiting() { return get_status() & state_sd_waiting;  }
        // inline bool is_sd_formatting() { return get_status() & state_sd_formatting; }
        // inline bool is_box_open() { return get_status() & state_box_open; }
        // inline bool is_recording() { return get_status() & state_recording; }
        // inline bool is_error() { return get_status() & state_error; }

        inline bool is_sd_waiting() const { return status_.load() & state_sd_waiting;  }
        inline bool is_sd_formatting() const { return status_.load() & state_sd_formatting; }
        inline bool is_box_open() const { return status_.load() & state_box_open; }
        inline bool is_recording() const { return status_.load() & state_recording; }
        inline bool is_error() const { return status_.load() & state_error; }


        void update_led();

        inline bool is_set(int s) { return status_.load() & s; }

        inline void set_status(int status)
        {
            int old_value = status_.load();
            int new_value;
            do {
                new_value = old_value | status;
                if (old_value != new_value) {
                    SPDLOG_DEBUG("set status to {} -> {}", old_value, new_value);
                }
            } while(!status_.compare_exchange_weak(old_value, new_value));
        }

        inline void set_and_clear_status(int set, int clr)
        {
            int old_value = status_.load();
            int new_value;
            do {
                new_value = (old_value | set) & ~clr;
                if (old_value != new_value) {
                    SPDLOG_DEBUG("set status to {} -> {}", old_value, new_value);
                }
            } while(!status_.compare_exchange_weak(old_value, new_value));
        }

        inline void clear_status(int status)
        {
            int old_value = status_.load();
            int new_value;
            do {
                new_value = old_value & ~status;
                if (old_value != new_value) {
                    SPDLOG_DEBUG("set status to {} -> {}", old_value, new_value);
                }
            } while(!status_.compare_exchange_weak(old_value, new_value));
        }

        inline void set_red_board(std::shared_ptr<gpio_out> port) {
            red_board_ = port;
        }

        inline void set_green_board(std::shared_ptr<gpio_out> port) {
            green_board_ = port;
        }

    private:
        inline void set_green(bool v) { 
            green_->write_value(v);
            if (green_board_) {
                green_board_->write_value(!v);
            }
        }
        inline void set_red(bool v) { 
            red_->write_value(v);
            if (red_board_) {
                red_board_->write_value(!v);
            }
        }

        inline void set_alarm(bool v) {
            if (alarm_state_ != v) {
                alarm_state_ = v;
                alarm_a_->write_value(!v);
                alarm_b_->write_value(true);
                alarm_b_->write_value(false);
            }
        }

        void update_status();

        std::atomic<int> status_;
        int prev_status_;

        int counter_;
        std::shared_ptr<gpio_out> green_;
        std::shared_ptr<gpio_out> red_;
        std::shared_ptr<gpio_out> green_board_;
        std::shared_ptr<gpio_out> red_board_;

        std::shared_ptr<gpio_out> alarm_a_;
        std::shared_ptr<gpio_out> alarm_b_;

        bool alarm_state_;
    };
    
    class power_monitor: public gpio_base
    {
    public:
        explicit power_monitor(const char *port_number, const char *port_name) noexcept
            : gpio_base(port_number, port_name),
              fd_(-1)
        {}
        power_monitor() = delete;
        power_monitor(const power_monitor &) = delete;
        power_monitor(power_monitor &&) = delete;

        int open();

        constexpr int fd() const {
            return fd_;
        }
    private:
        int fd_;
    };
}
#endif