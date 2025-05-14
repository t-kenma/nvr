#ifndef __NVR_LOGGING_HPP__
#define __NVR_LOGGING_HPP__

#ifndef SPDLOG_ACTIVE_LEVEL
#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_INFO
#endif
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include <filesystem>
#include <thread>

namespace nvr {
    // int write_log(char type, bool on);

    class logger
    {
    public:
        logger(const char* path)
            : path_(path),
              max_file_size_(10 * 1024 * 1024), // 10M,
              backup_count_(5)
        {}
        ~logger() = default;
        logger(const logger&) = delete;
        logger(logger&&) =delete;

        int write(const char *ftm, ...);

    private:
        std::filesystem::path path_;
        std::mutex mtx_;
        off_t max_file_size_;
        int backup_count_;
    };

    class locked_fp
    {
    public:
        locked_fp(): fp_(nullptr), fd_(-1), locked_(false) {}
        ~locked_fp();
        locked_fp(const locked_fp&) = delete;
        locked_fp(locked_fp&&) = delete;

        FILE* open(const std::filesystem::path& path) noexcept;

        constexpr int fd() { return fd_; }
    private:
        FILE* fp_;
        int fd_;
        bool locked_;
    };
}
#endif