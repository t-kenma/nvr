
#include <cerrno>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "logging.hpp"

namespace nvr {
    template <typename... Args>
    int exec_command(const char* command, Args... args) noexcept
    {
        pid_t pid;
        int status;
        int rc;

        pid = fork();
        if (pid < 0) {
            SPDLOG_ERROR("Failed to fork process: {}", strerror(errno));
            return -1;
        } else if (pid == 0) {
            execl(command, command, args..., nullptr);
            SPDLOG_ERROR("Failed to exec {}: {}", command, strerror(errno));
            exit(-1);
        }

        waitpid(pid, &status, 0);

        if (!WIFEXITED(status)) {
            SPDLOG_ERROR("{} is not exited.", command);
            return -1;
        }

        rc = WEXITSTATUS(status);
        SPDLOG_DEBUG("{} exited with {}.", command, rc);
        return rc;
    }

    inline int do_systemctl(const char *op, const char *service) noexcept
    {
        int rc = exec_command("/bin/systemctl", op, service);
        SPDLOG_DEBUG("systemctl {} {} result: {}", op, service, rc);
        return rc;
    }
}