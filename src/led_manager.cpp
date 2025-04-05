#include "gpio.hpp"
#include "logging.hpp"

namespace nvr {
    const int led_manager::state_none = 0;
    const int led_manager::state_error_sd_init = 1;
    const int led_manager::state_error_sd_format = (1 << 1);
    const int led_manager::state_error_sd_dir = (1 << 2);
    const int led_manager::state_error_sd_file = (1 << 3);
    const int led_manager::state_error_video = (1 << 4);
    const int led_manager::state_error_other = (1 << 5);
    const int led_manager::state_error = led_manager::state_error_sd_init
        | led_manager::state_error_sd_format
        | led_manager::state_error_sd_dir
        | led_manager::state_error_sd_file
        | led_manager::state_error_video
        | led_manager::state_error_other;
    const int led_manager::state_recording = (1 << 6);
    const int led_manager::state_sd_waiting = (1 << 7);
    const int led_manager::state_sd_formatting = (1 << 8);
    const int led_manager::state_box_open = (1 << 9);
    const int led_manager::state_resetting = (1 << 10);
    const int led_manager::state_station_associated = (1 << 11);
    const int led_manager::state_wifi_active = (1 << 12);
    const int led_manager::state_sd_all = led_manager::state_error_sd_init
        | led_manager::state_error_sd_format
        | led_manager::state_error_sd_dir
        | led_manager::state_error_sd_file
        | led_manager::state_sd_waiting
        | led_manager::state_sd_formatting;

    void led_manager::update_led()
    {
        int status = get_status();
        if (prev_status_ != status) {
            SPDLOG_DEBUG("led_manager status changed: {} -> {}", prev_status_, status);
            prev_status_ = status;
            counter_ = 0;

            if (status & state_error) {
                set_alarm(true);
            } else {
                set_alarm(false);
            }

            set_red(false);
            set_green(false);
        }

        if (status & state_resetting) {
            if (counter_ == 0 || counter_ == 6 || counter_ == 12 || counter_ == 18) {
                set_red(true);
            } else if (counter_ == 3 || counter_ == 9 || counter_ == 15 || counter_ == 21) {
                set_red(false);
            }
            if (counter_ == 40) {
                counter_ = 0;
            } else {
                counter_++;
            }
            return;
        }

        if (status & state_sd_formatting) {
            if (counter_ == 0 || counter_ == 6) {
                set_red(true);
            } else if (counter_ == 3 || counter_ == 9) {
                set_red(false);
            }
            if (counter_ == 28) {
                counter_ = 0;
            } else {
                counter_++;
            }
            return;
        }

        if (status & state_sd_waiting) {
            if (counter_ == 0) {
                set_red(true);
            } else if (counter_ == 3) {
                set_red(false);
            }
            if (counter_ == 22) {
                counter_ = 0;
            } else {
                counter_++;
            }
            return;
        }

        if (status & state_box_open || status & state_station_associated) {
            if (counter_ == 0) {
                set_red(true);
                set_green(true);
            } else if (counter_ == 10) {
                set_red(false);
                set_green(false);
            }
            if (counter_ == 19) {
                counter_ = 0;
            } else {
                counter_++;
            }
            return;
        }

        if (status & state_wifi_active) {
            if (counter_ == 0 || counter_ == 6) {
                set_green(true);
            } else if (counter_ == 3 || counter_ == 9) {
                set_green(false);
            }
            if (counter_ == 28) {
                counter_ = 0;
            } else {
                counter_++;
            }
            return;
        }

        if (status & state_error) {
            set_red(true);
            return;
        }

        if (status & state_recording) {
            if (counter_ == 0) {
                set_green(true);
            } else if (counter_ == 20) {
                set_green(false);
            }

            if (counter_ == 28) {
                counter_ = 0;
            } else {
                counter_++;
            }
            return;
        }

        if (counter_ == 0) {
            set_green(true);
        }
    }
}