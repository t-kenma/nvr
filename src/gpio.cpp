#include "gpio.hpp"
#include "logging.hpp"
#include <cerrno>

namespace nvr {
    static const char *gpio_base_dir = "/sys/class/gpio";

    // static gboolean gpio_monitor_watch_cb(GIOChannel *channel, GIOCondition, gpointer udata)
    // {
    //     gpio_monitor* monitor = static_cast<gpio_monitor*>(udata);
    //     monitor->update_value();

    //     return TRUE;
    // }

    // static gboolean gpio_monitor_timer_cb(gpointer udata)
    // {
    //     gpio_monitor* monitor = static_cast<gpio_monitor*>(udata);
    //     monitor->update_value();

    //     return TRUE;
    // }

    // static gboolean motion_sensor_timer_cb(gpointer udata)
    // {
    //     motion_sensor* sensor = static_cast<motion_sensor*>(udata);
    //     sensor->update_value();

    //     return TRUE;
    // }

    int gpio_base::open_port(const char *direction, const char *edge) noexcept
    {
        int ret = 0;
        int fd;
        ssize_t wsize;
        std::filesystem::path base_path = gpio_base_dir;
        std::filesystem::path export_path = base_path / "export";
        std::filesystem::path direction_path = base_path / port_name_ / "direction";

        std::error_code ec;
        if (!std::filesystem::exists(direction_path, ec)) {
            fd = open(export_path.c_str(), O_WRONLY | O_CLOEXEC);
            wsize = 0;

            SPDLOG_DEBUG("Write {} to {}.", port_number_.c_str(), export_path.c_str());
            if (fd == -1) {
                SPDLOG_ERROR("Failed to open {}: {}", export_path.c_str(), strerror(errno));
                return -1;
            }

            wsize = write(fd, (const void *)port_number_.c_str(), port_number_.size());
            if (wsize == -1)
            {
                SPDLOG_ERROR("Failed to write {}: {}", export_path.c_str(), strerror(errno));
                ret = -1;
            }
            close(fd);
        }

        if (ret) {
            return ret;
        }

        fd = open(direction_path.c_str(), O_WRONLY | O_CLOEXEC);
        if (fd == -1) {
            SPDLOG_ERROR("Failed to open {}: {}", direction_path.c_str(), strerror(errno));
            return -1;
        }

        wsize = write(fd, (const char *)direction, strlen(direction));
        if (wsize == -1) {
            SPDLOG_ERROR("Failed to write {}: {}", direction_path.c_str(), strerror(errno));
            ret = -1;
        }
        close(fd);

        if (ret) {
            return ret;
        }

        if (edge) {
            std::filesystem::path edge_path = base_path / port_name_ / "edge";

            fd = open(edge_path.c_str(), O_WRONLY | O_CLOEXEC);
            if (fd == -1) {
                SPDLOG_ERROR("Failed to open {}: {}", edge_path.c_str(), strerror(errno));
                return -1;
            }

            wsize = write(fd, (const char *)edge, strlen(edge));
            if (wsize == -1) {
                SPDLOG_ERROR("Failed to write {}: {}", edge_path.c_str(), strerror(errno));
                ret = -1;
            }
            close(fd);

            if (ret) {
                return ret;
            }
        }

        value_path_ = base_path / port_name_ / "value";
        return ret;
    }

    int gpio_out::open(bool high)
    {
        int ret = open_port(high ? "high" : "out");
        if (ret) {
            return ret;
        }

        fd_ = ::open(value_path_.c_str(), O_WRONLY | O_CLOEXEC);
        if (fd_ == -1) {
            return -1;
        }

        return ret;
    }

    int gpio_out::write_value(bool value)
    {
        int ret = 0;
        ssize_t wsize;
        guchar buf[1];

        buf[0] = (value) ? '1' : '0';

        // int fd = open(value_path_.c_str(), O_WRONLY | O_CLOEXEC);
        // if (fd == -1) {
        //     SPDLOG_ERROR("Failed to open {}: {}", value_path_.c_str(), strerror(errno));
        //     return -1;
        // }

        wsize = write(fd_, (const void *)buf, 1);
        if (wsize == -1) {
            SPDLOG_ERROR("Failed to write {}: {}", value_path_.c_str(), strerror(errno));
            ret = -1;
        }
        // close(fd);

        return ret;
    }

    int gpio_in::open(const char *edge)
    {
        int ret = 0;

        ret = open_port("in", edge);
        if (ret) {
            return ret;
        }

        int fd = ::open(value_path_.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd == -1) {
            SPDLOG_ERROR("Failed to open {}: {}", value_path_.c_str(), strerror(errno));
            return -1;
        }

        channel_ = g_io_channel_unix_new(fd);
        if (channel_ == nullptr) {
            SPDLOG_ERROR("Failed to create channel.");
            ret = -1;
        }


        return ret;
    }

    GIOStatus gpio_in::read_value(guchar *value)
    {
        GIOStatus ret = G_IO_STATUS_AGAIN;
        GError* error = 0;
        gsize rsize = 0;
        gchar buf[2] = {0,};

        if (channel_ == nullptr) {
            return G_IO_STATUS_EOF;
        }
        g_io_channel_seek_position(channel_, 0, G_SEEK_SET, 0);

        while (ret == G_IO_STATUS_AGAIN)
        {
            ret = g_io_channel_read_chars(channel_, buf, sizeof(buf), &rsize, &error);
            if (ret == G_IO_STATUS_NORMAL) {
                *value = buf[0] - 48;
                return ret;
            } else if (ret == G_IO_STATUS_ERROR) {
                if (error) {
                    SPDLOG_ERROR("Failed to read channel: {}", error->message);
                    g_error_free(error);
                } else {
                    SPDLOG_ERROR("Failed to read channel");
                }
            }
        }
        return ret;
    }

    guint gpio_monitor::add_watch(GIOFunc func, gpointer user_data)
    {
        int ret = 0;
        GIOCondition cond = GIOCondition(G_IO_PRI);

        update_value();
        return g_io_add_watch(channel_, cond, func, user_data);
    }

    gpio_monitor::update_state gpio_monitor::update_value()
    {
        GIOStatus ret = G_IO_STATUS_AGAIN;
        guchar value;

        ret = read_value(&value);

        if (ret == G_IO_STATUS_NORMAL) {
            if (value != value_) {
                value_ = value;
                SPDLOG_DEBUG("GPIO value updated: {}: {}", port_name_, value_);
                if (value_ == 1) {
                    return update_state::changed_to_high;
                } else {
                    return update_state::changed_to_low;
                }
            } else {
                return update_state::unchanged;
            }
        }
        return update_state::error;
    }

    int power_monitor::open()
    {
        int ret = open_port("in", "falling");
        if (ret) {
            return ret;
        }

        fd_ = ::open(value_path_.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd_ == -1) {
            return -1;
        }
        return ret;
    }
}