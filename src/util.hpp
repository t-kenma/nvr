
#include <cerrno>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include "logging.hpp"
#include <filesystem>

namespace fs = std::filesystem;

namespace nvr
{
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
	
	
	inline  int file_create(char* path, char* val, int len){
		int rc;
		int fd = open(path, O_CREAT | O_WRONLY | O_TRUNC | O_CLOEXEC, S_IRWXU);
		if (fd == -1) {
			SPDLOG_ERROR("Failed to open {}: {}", path, strerror(errno));
			return -1;
		}

		close(fd);
		
		return 0;
	}

	inline int file_write(const char* path, char* val, int len, int *rc)
	{
		int fd = open(path, O_CREAT | O_WRONLY | O_TRUNC | O_CLOEXEC, S_IRWXU);
		if (fd == -1) {
			SPDLOG_ERROR("Failed to open {}: {}", path, strerror(errno));
			return -1;
		}

		*rc = write(fd, val, len);
		if (*rc < 0) {
			SPDLOG_ERROR("Failed to write {}: {}", path, strerror(errno));
			close(fd);
			return -2;
		}

		close(fd);
		system( "sync" );
		
		return 0;
	}

	inline int file_read( const char* path, char* val, int len, int *rc )
	{
		int fd = open( path, O_RDONLY | O_CLOEXEC, S_IRWXU );
		if( fd == -1 )
		{
			SPDLOG_ERROR( "Failed to open {}: {}", path, strerror(errno) );
			return -1;
		}

		*rc = read( fd, val, len );
		if( *rc < 0 )
		{
			SPDLOG_ERROR("Failed to write {}: {}", path, strerror(errno));
			close( fd) ;
			return -2;
		}

		close( fd );
		
		return 0;
	}

	inline bool create_dir( const char * dir)
	{
		fs::path path(dir);
		std::error_code ec;

		if (!fs::exists(path, ec))
		{
			if (!fs::create_directories(path, ec)) {
				SPDLOG_ERROR("Failed to make directory: {}.", path.c_str());   
				return false;
			}
		}

		return true;
	}

}
