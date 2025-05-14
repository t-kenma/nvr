#include "logging.hpp"

#include <filesystem>
#include <sys/file.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>

namespace nvr {
    static bool is_file_exists(const char *path) noexcept
    {
        struct stat st;
        if (stat(path, &st) == 0) {
            return S_ISREG(st.st_mode);
        }
        return false;
    }

    FILE* locked_fp::open(const std::filesystem::path& path) noexcept
    {
        const char* p = path.c_str();
        fp_ = fopen(p, "ae");
        if (fp_ == nullptr) {
            SPDLOG_ERROR("Failed to open {}: {}", p, strerror(errno));
            return nullptr;
        }

        fd_ = fileno(fp_);
        if (flock(fd_, LOCK_EX)) {
            SPDLOG_ERROR("Failed to lock {}: {}", p, strerror(errno));
            return nullptr; 
        }
        locked_ = true;
        return fp_;
    }

    locked_fp::~locked_fp()
    {
        if (locked_) {
            if (flock(fd_, LOCK_UN)) {
                SPDLOG_WARN("Failed to unlock: {}", strerror(errno));
            }
        }

        if (fp_) {
            fclose(fp_);
        }
    }

    int logger::write(const char *fmt, ...)
    {
        int ret = 1;
        FILE *fp = nullptr;
        struct stat st;
        va_list ap;
        char* src = nullptr;
        char* dst = nullptr;
        time_t t = ::time(nullptr);
        struct tm time;

        do {
            std::lock_guard<std::mutex> lock(mtx_);
            const char* path = path_.c_str();
            locked_fp lfp;

            fp = lfp.open(path_);
            if (fp == nullptr) {
                ret = -1;
                break;
            }

            if (fstat(lfp.fd(), &st)) {
                SPDLOG_ERROR("Failed to stat {}: {}", path, strerror(errno));
                ret = -1;
                break;
            }

            if (st.st_size >= max_file_size_) {
                size_t pathlen = strlen(path);
                size_t tmpsize = pathlen + 3;

                SPDLOG_DEBUG("Log file size {} >= {}", st.st_size, max_file_size_);
                if (src == nullptr) {
                    src = static_cast<char*>(malloc(tmpsize));
                    if (src) {
                        strcpy(src, path);
                        src[pathlen] = '.';
                        src[pathlen + 2] = '\0';
                    }
                }

                if (dst == nullptr) {
                    dst = static_cast<char*>(malloc(tmpsize));
                    if (dst) {
                        strcpy(dst, path);
                        dst[pathlen] = '.';
                        dst[pathlen + 2] = '\0';
                    }
                }

                if (src == nullptr || dst == nullptr) {
                    SPDLOG_ERROR("Failed to allocate memory.");
                    ret = -1;
                    break;
                }

                for (int i = backup_count_; i >= 0; i--) {
                    if (ret < 0) {
                        continue;
                    }

                    if (i == backup_count_) {
                        src[pathlen + 1] = i + 48;

                        if (is_file_exists(src)) {
                            SPDLOG_DEBUG("Remove {}", src);
                            if (unlink(src)) {
                                SPDLOG_ERROR("Failed to unlink {}: {}", src, strerror(errno));
                                ret = -1;
                            }
                        }
                    } else if (i == 0) {
                        dst[pathlen + 1] = i + 1 + 48;

                        SPDLOG_DEBUG("Rename {} to {}", path, dst);
                        if (rename(path, dst)) {
                            SPDLOG_ERROR("Failed to rename {} to {}: {}", path, dst, strerror(errno));
                            ret = -1;
                        }
                    } else {
                        src[pathlen + 1] = i + 48;
                        dst[pathlen + 1] = i + 1 + 48;

                        if (is_file_exists(src)) {
                            SPDLOG_DEBUG("Rename {} to {}", src, dst);
                            if (rename(src, dst)) {
                                SPDLOG_ERROR("Failed to rename {} to {}: {}", src, dst, strerror(errno));
                                ret = -1;
                            }
                        }
                    }
                }
            } else {
                localtime_r(&t, &time);

                if (fseek(fp, 0, SEEK_END)) {
                    SPDLOG_ERROR("Failed to seek {}: {}", path_.c_str(), strerror(errno));
                    ret = -1;
                    break;
                }

                fprintf(
                    fp,
                    "%04d/%02d/%02d %02d:%02d:%02d ",
                    time.tm_year + 1900,
                    time.tm_mon + 1,
                    time.tm_mday,
                    time.tm_hour,
                    time.tm_min,
                    time.tm_sec
                );

                va_start(ap, fmt);
                vfprintf(fp, fmt, ap);
                va_end(ap);

                fprintf(fp, "\n");
                fflush(fp);

                ret = 0;
            }
        } while(ret == 1);

        if (src) {
            free(src);
        }

        if (dst) {
            free(dst);
        }

        return ret;
    }
}