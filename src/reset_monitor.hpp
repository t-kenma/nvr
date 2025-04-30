#ifndef __NVR_RESET_MONITOR__
#define __NVR_RESET_MONITOR__

#include <thread>
#include <atomic>

#include "gpio.hpp"
#include "logging.hpp"

namespace nvr {
    class reset_monitor : public gpio_in
    {
    public:
        explicit reset_monitor(const char *port_number, const char *port_name, guint default_value)
            : gpio_in(port_number, port_name),
              value_(default_value),
              count_(0),
              state_(0),
              done_(false)
        {}
        reset_monitor() = delete;
        reset_monitor(const reset_monitor&) = delete;
        reset_monitor(reset_monitor&&) = delete;

        int update_value();


        inline void set_logger(std::shared_ptr<logger> logger) {
            logger_ = logger;
        }
    private:
        void reset_process();
        guchar value_;
        guint count_;
        guchar state_;
        std::thread thread_;
        std::atomic<bool> done_;
        std::shared_ptr<logger> logger_;
    };
}

#endif
