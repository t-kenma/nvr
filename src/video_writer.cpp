#include "video_writer.hpp"
#include "logging.hpp"
#include <iostream>
#include <fstream>
#include <set>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <cerrno>

namespace nvr
{
    static const size_t BUFSIZE = 10240;

    int video_writer::sync_dir(const std::filesystem::path &path)
    {
        int ret = 0;
        int fd = open(path.c_str(), O_DIRECTORY|O_CLOEXEC);

        if (fd == -1)
        {
            SPDLOG_WARN("Failed to open {}: {}", path.c_str(), strerror(errno));
            return -1;
        }

        if (!power_down_.load(std::memory_order_acquire)) {
#ifdef NVR_DEBUG_POWER
            tmp_out2_->write_value(true);
#endif
            ret = fsync(fd);
#ifdef NVR_DEBUG_POWER
            tmp_out2_->write_value(false);
#endif
            if (ret)
            {
                SPDLOG_WARN("Failed to sync {}: {}", path.c_str(), strerror(errno));
            }
        }
        close(fd);

        return ret;
    }

    bool video_writer::copy_file(const std::filesystem::path &src, const std::filesystem::path &dst)
    {
        bool ret = false;

        int rfd = -1;
        int wfd = -1;
        unsigned char buf[BUFSIZE];

        rfd = open(src.c_str(), O_RDONLY|O_CLOEXEC);
        if (rfd < 0)
        {
            SPDLOG_ERROR("Failed to open {}: {}", src.c_str(), strerror(errno));
            goto END;
        }

        wfd = open(dst.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, S_IRWXU);
        if (wfd < 0)
        {
            SPDLOG_ERROR("Failed to open {}: {}", dst.c_str(), strerror(errno));
            goto END;
        }

        while (true)
        {
            ssize_t rsize = read(rfd, buf, BUFSIZE);
            ssize_t wsize = 0;
            if (rsize == 0)
            {
                ret = true;
                break;
            }
            if (rsize < 0)
            {
                SPDLOG_ERROR("Failed to read {}: {}", src.c_str(), strerror(errno));
                break;
            }

            if (power_down_.load(std::memory_order_acquire)) {
                return false;
            }

            wsize = write(wfd, (const void *)buf, rsize);
            if (wsize == -1)
            {
                SPDLOG_ERROR("Failed to write {}: {}", dst.c_str(), strerror(errno));
                break;
            }

            if (power_down_.load(std::memory_order_acquire)) {
                return false;
            }

#ifdef NVR_DEBUG_POWER
            tmp_out2_->write_value(true);
#endif
            if (fdatasync(wfd))
            {
                SPDLOG_WARN("Failed to sync {}: {}", dst.c_str(), strerror(errno));
            }
#ifdef NVR_DEBUG_POWER
            tmp_out2_->write_value(false);
#endif
        }

    END:
        if (rfd != -1)
        {
            close(rfd);
        }

        if (wfd != -1)
        {
            if (!power_down_.load(std::memory_order_acquire)) {
#ifdef NVR_DEBUG_POWER
            tmp_out2_->write_value(true);
#endif
                if (syncfs(wfd)) {
                    SPDLOG_WARN("Failed to syncfs: {}", dst.c_str(), strerror(errno));
                    ret = false;
                }
#ifdef NVR_DEBUG_POWER
            tmp_out2_->write_value(false);
#endif
            }
            close(wfd);

            // if (ret)
            // {
            //     if (sync_dir(dst.parent_path())) {
            //         ret = false;
            //     }
            // }
        }

        return ret;
    }

    static std::vector<std::string> split_filename(const std::string &str)
    {
        std::vector<std::string> ret;
        char delim = '-';
        std::string::size_type first = 0;
        std::string::size_type last = str.find_first_of(delim);

        while (first < str.size())
        {
            std::string part(str, first, last - first);

            ret.push_back(part);
            if (ret.size() == 4)
            {
                return ret;
            }

            first = last + 1;
            last = str.find_first_of(delim, first);

            if (last == std::string::npos)
            {
                last = str.size();
            }
        }
        return std::vector<std::string>();
    }

    static bool make_directory(const std::filesystem::path &path)
    {
        if (mkdir(path.c_str(), 0777))
        {
            SPDLOG_ERROR("Failed to create {}: {}", path.c_str(), strerror(errno));
            return false;
        }
        SPDLOG_DEBUG("{} is created.", path.c_str());

        // sync_dir(path.parent_path());

        return true;
    }

    static std::filesystem::path make_directories(const std::filesystem::path &dir, const std::string &path)
    {
        std::filesystem::path dst = dir;
        auto parts = split_filename(path);

        if (parts.empty())
        {
            return std::filesystem::path();
        }

        std::error_code ec;
        for (const std::string &part : split_filename(path))
        {
            dst /= part;
            // SPDLOG_DEBUG("chacke directory {}", dst.c_str());
            if (!std::filesystem::is_directory(dst, ec))
            {
                if (std::filesystem::exists(dst, ec))
                {
                    unlink(dst.c_str());
                }
                if (!make_directory(dst))
                {
                    return std::filesystem::path();
                }
            }
        }
        return dst;
    }

    static std::set<std::filesystem::path> src_file_set(const std::filesystem::path &dir)
    {
        std::set<std::filesystem::path> ret;

        try {
            for (const std::filesystem::directory_entry &entry : std::filesystem::directory_iterator(dir))
            {
                if (entry.is_regular_file())
                {
                    auto path = entry.path();
                    if (path.has_stem() && path.has_extension() && path.extension().compare(".mp4") == 0)
                    {
                        ret.insert(path);
                    }
                }
            }
        } catch (std::exception &err) {
            SPDLOG_ERROR("Failed to iterate {}: {}", dir.c_str(), err.what());
        }

        return ret;
    }

    std::tuple<std::uintmax_t, bool> video_writer::delete_old_files_(const std::filesystem::path &dir, std::uintmax_t size, bool can_delete_dir)
    {
        std::set<std::filesystem::directory_entry> entries;
        std::size_t entry_count = 0;

        try {
            for (const auto &entry : std::filesystem::directory_iterator(dir))
            {
                entry_count++;
                if (entry.is_directory())
                {
                    entries.insert(entry);
                }
                else if (entry.is_regular_file())
                {
                    auto path = entry.path();
                    if (path.has_extension() && path.extension().compare(".mp4") == 0)
                    {
                        entries.insert(entry);
                    }
                }
            }
        } catch (std::exception &err) {
            SPDLOG_ERROR("Failed to iterate {}: {}", dir.c_str(), err.what());
        }

        std::error_code ec;
        std::uintmax_t total = 0;
        std::size_t unlinked_count = 0;
        for (const auto &entry : entries)
        {
            if (entry.is_directory(ec))
            {
                auto result = delete_old_files_(entry.path(), size - total, true);
                total += std::get<0>(result);
                if (std::get<1>(result))
                {
                    unlinked_count++;
                }
            }
            else
            {
                auto file_size = entry.file_size(ec);
                if (file_size != static_cast<std::uintmax_t>(-1)) {
                    auto path = entry.path().c_str();
                    if (unlink(path)) {
                        SPDLOG_ERROR("Failed to unlink {}: {}", path, strerror(errno));
                    } else {
                        SPDLOG_DEBUG("Unlink {}", path);
                        total += file_size;
                        unlinked_count++;
                    }
                }
            }

            if (total >= size)
            {
                break;
            }
        }

        if (can_delete_dir && unlinked_count == entry_count) {
            if (rmdir(dir.c_str())) {
                SPDLOG_ERROR("Failed to remove {}: {}", dir.c_str(), strerror(errno));
            } else {
                SPDLOG_DEBUG("{} is removed", dir.c_str());
                sync_dir(dir.parent_path());
                return std::make_tuple(total, true);
            }
        }

        sync_dir(dir);
        return std::make_tuple(total, false);
    }

    bool video_writer::check_volume_size(const std::filesystem::path &dir, size_t size)
    {
        struct statvfs st;
        if (statvfs(dir.c_str(), &st) != 0)
        {
            SPDLOG_ERROR("Failed to stat {}", dir.c_str());
            return false;
        }

        const std::uintmax_t wanted = (size) ? size : st.f_frsize * st.f_blocks * 5 / 100;
        const std::uintmax_t avail = st.f_bavail * st.f_frsize;

        if (avail < wanted)
        {
            try {
                if (!delete_old_files(dir, wanted - avail))
                {
                    return false;
                }
            } catch (std::exception &err) {
                return false;
            }
        }

        return true;
    }

    void video_writer::move_file(const std::filesystem::path &file)
    {
        std::error_code ec;
        auto size = std::filesystem::file_size(file, ec);
        if (ec) {
            return;
        }

        check_volume_size(dst_dir_, size);
        std::filesystem::path dir;

        dir = make_directories(dst_dir_, file.stem());
        if (dir.empty())
        {
            logger_->write("E SDカード保存エラー");
            led_manager_->set_status(led_manager::state_error_sd_dir);
            return;
        } else {
            led_manager_->clear_status(led_manager::state_error_sd_dir);
        }

        const auto dst = dir / file.filename();
        if (copy_file(file, dst))
        {
            SPDLOG_DEBUG("{} is copied to {}", file.c_str(), dst.c_str());
            led_manager_->clear_status(led_manager::state_error_sd_file);
        }
        else
        {
            logger_->write("E SDカード保存エラー");
            led_manager_->set_status(led_manager::state_error_sd_file);
            return;
        }

        if (unlink(file.c_str()))
        {
            SPDLOG_ERROR("Failed to unlink {}: {}", file.c_str(), strerror(errno));
        }
    }

    void video_writer::loop()
    {
        int remain = 0;
        while (active_)
        {
            {
                std::unique_lock<std::mutex> lock(mtx_);
                std::cv_status result = cv_.wait_for(lock, std::chrono::seconds(1));
                if (result == std::cv_status::timeout) {
                    continue;
                }
            }

            /*
            bool sensor_active = true;
            if (motion_sensor_) {
                sensor_active = motion_sensor_->reset_status();
                if (sensor_active) {
                    remain = 1;
                } else if (remain > 0) {
                    sensor_active = true;
                    remain--;
                }
            }
            */

            auto files = src_file_set(src_dir_);
            auto file_count = files.size();

            if (!file_count)
            {
                continue;
            }

            bool moved = false;
            for (const auto &file : files) {
                if (file_count > 2) {
                    unlink(file.c_str());
                    SPDLOG_DEBUG("Delete {}.", file.c_str());
                } else {
                    if ( active_ && sd_manager_->check_mount_point()) {
                        writing_.store(true, std::memory_order_seq_cst);
                        this->move_file(file);
                        moved = true;
                        writing_.store(false, std::memory_order_seq_cst);
                        SPDLOG_DEBUG("Write {}.", file.c_str());
                    } else if (file_count > 1) {
                        unlink(file.c_str());
                        SPDLOG_DEBUG("Delete {}.", file.c_str());
                    }
                }
                file_count--;
            }
            if (moved) {
                check_volume_size(dst_dir_, 0);
            }
        }
        SPDLOG_DEBUG("video writer finished.");
    }
}