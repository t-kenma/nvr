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
              channel_(nullptr),
              fd_(-1)
        {}
        gpio_in() = delete;
        gpio_in(const gpio_in &) = delete;
        gpio_in(gpio_in &&) = delete;

        int open(const char *edge = nullptr);
        int open_with_edge();
        GIOStatus read_value(guchar *v);

        virtual ~gpio_in()
        {
            if (channel_) {
                g_io_channel_shutdown(channel_, TRUE, nullptr);
                g_io_channel_unref(channel_);
                channel_ = nullptr;
            }
        }

        constexpr int fd() const {
            return fd_;
        }
    private:
        int fd_;

    protected:
        GIOChannel* channel_;
    };

    /*
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

    */
    
}
#endif




