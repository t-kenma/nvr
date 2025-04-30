#include "reset_monitor.hpp"
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "util.hpp"

namespace nvr {
    int reset_monitor::update_value()
    {
        GIOStatus rc = G_IO_STATUS_AGAIN;
        guchar new_value;
        int ret = 1;
        bool changed = false;

        rc = read_value(&new_value);
        if (rc != G_IO_STATUS_NORMAL) {
            return -1;
        }

        if (value_ == new_value) {
            count_++;
        } else {
            value_ = new_value;
            count_ = 0;
            changed = true;
        }

        if (thread_.joinable()) {
            if (done_.load(std::memory_order_relaxed)) {
                thread_.join();
                done_.store(false, std::memory_order_relaxed);
                ret = 2;
            }
            count_ = 0;
        } else {
            if (changed && value_ == 1) {
                state_ = !state_;
            } else if (state_ == 1 && value_ == 1 && count_ == 100) { // clear after 5sec.
                state_ = 0;
            } else if (state_ == 1 && value_ == 0 && count_ == 60) { // reset after 3sec.
                logger_->write("L リセットボタン ON");
                state_ = 0;
            }             
        }

        return ret;
    }
}
