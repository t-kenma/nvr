#ifndef __NVR_AP_MANAGER_HPP__
#define __NVR_AP_MANAGER_HPP__

#include <memory>
#include <functional>
#include <thread>
#include <atomic>
#include <string>
#include "gpio.hpp"
#include "video_writer.hpp"
#include "logging.hpp"

namespace nvr {
    class ap_manager : public gpio_in
    {
    public:
        ap_manager(const char *port_number, const char *port_name, guint default_value)
            : gpio_in(port_number, port_name),
              value_(default_value),
              txpower_("800"),
              count_(0),
              active_(false)
        {}
        ap_manager(const ap_manager&) = delete;
        ap_manager(ap_manager &&) = delete;
        ~ap_manager() = default;

        GIOStatus update_value();

        bool is_hostapd_running() const noexcept;
        bool is_dhcpd_running() const noexcept;
        int is_station_associated() const noexcept;

        inline int start_hostapd() const noexcept {
            int rc = ctrl_service("hostapd", true);
            if (rc == 0) {
                led_manager_->set_status(led_manager::state_wifi_active);
            }
            return rc;
        }

        inline int stop_hostapd() const noexcept {
            int rc = ctrl_service("hostapd", false);
            if (rc == 0) {
                led_manager_->clear_status(led_manager::state_wifi_active);
            }
            return rc;
        }

        inline int is_running() const noexcept {
            return thread_.joinable();
        }

        int start_dhcpd() const noexcept;

        inline int stop_dhcpd() const noexcept {
            return ctrl_service("dhcpd", false);
        }

        void start();
        void stop();

        inline void set_video_writer(std::shared_ptr<video_writer> writer) {
            video_writer_ = writer;
        }

        inline void set_led_manager(std::shared_ptr<led_manager> manager) {
            led_manager_ = manager;
        }

        inline void set_logger(std::shared_ptr<logger> logger) {
            logger_ = logger;
        }

        inline void set_txpower(int txpower) {
            txpower_ = std::to_string(txpower);
        }

        int update_txpower() noexcept;

        void check_restart_file();
        int create_restart_file();
    private:
        int ctrl_service(const char *service, bool start) const noexcept;
        int ctrl_interface(bool up);

        void loop();

        guchar value_;
        guint count_;
        std::atomic<bool> active_;
        std::shared_ptr<video_writer> video_writer_;
        std::shared_ptr<led_manager> led_manager_;
        std::thread thread_;
        std::mutex mtx_;
        std::shared_ptr<logger> logger_;
        std::string txpower_;
    };
}
#endif