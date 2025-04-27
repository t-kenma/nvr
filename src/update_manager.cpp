#include <cerrno>
#include <iostream>
#include <fstream>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/mount.h>
#include <sstream>
#include "update_manager.hpp"
#include "logging.hpp"
#include "util.hpp"
#include <filesystem>

namespace nvr {
    namespace fs = std::filesystem;
    const int update_manager::mount_state_not_mounted = 0;
    const int update_manager::mount_state_mounting = 1;
    const int update_manager::mount_state_mounted = 2;
    const int update_manager::update_none = 0;
    const int update_manager::update_start = 1;
    const int update_manager::update_ing = 2;
    const int update_manager::update_err1 = 3;
    const int update_manager::update_err2 = 4;
    const int update_manager::update_ok = 5;


    static const size_t BUFSIZE = 10240;

    void update_manager::timer_process()
    {
        static const std::filesystem::path proc_mounts{"/proc/mounts"};
        static const char *new_udt_file = update_file_.c_str();
        static const char *old_udt_file = nvr_file_.c_str();
        int status = get_mount_status();
        static int old_status;
        
        SPDLOG_INFO("timer_process");
        
        if (status == mount_state_mounted && old_status != mount_state_mounted ) {
            SPDLOG_INFO("is_update_file_exists = {}",is_update_file_exists());
            if (!is_update_file_exists()) {
                SPDLOG_INFO("no update faile");
                update_status_.store(update_none);
            }
            else{
                update_status_.store(update_start);
                //start_update();
            }
        }

        return;
    }

    bool update_manager::copy_file(const std::filesystem::path &src, const std::filesystem::path &dst)
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

            wsize = write(wfd, (const void *)buf, rsize);
            if (wsize == -1)
            {
                SPDLOG_ERROR("Failed to write {}: {}", dst.c_str(), strerror(errno));
                break;
            }

            if (fdatasync(wfd))
            {
                SPDLOG_WARN("Failed to sync {}: {}", dst.c_str(), strerror(errno));
            }
        }

    END:
        if (rfd != -1)
        {
            close(rfd);
        }

        if (wfd != -1)
        {
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

    int update_manager::start_update_proc()
    {
        SPDLOG_INFO("start_update");
        if (thread_.joinable()) {
            SPDLOG_WARN("update thread is running");
            return 0;
        }

        update_result_.store(update_start);
        thread_ = std::thread([this]{ this->update_proc(); });

        return 0;
    }
    
    int update_manager::get_update_status()
    {
        return update_result_.load();
    }


    void update_manager::update_proc()
    {
        while(1)
        {
            SPDLOG_INFO("update_proc");
            int status = update_status_.load();
            if( status == 1 )
            {
                pid_t pid = nvr_pid_.load();
                kill(pid, SIGTERM);
                waitpid(pid, &status, 0);
                nvr_pid_.store(-1);
                //set_pid(-1);
                update_process();
            }
            else if( status == 5)
            {
	            pid_t new_pid = fork();
	            if (new_pid < 0) 
	            {
	                SPDLOG_ERROR("Failed to fork process: {}", strerror(errno));
	            } 
	            else if (new_pid == 0) 
	            {
	                nvr_pid_.store(new_pid);
	                //set_pid(new_pid);
	                execl("/usr/bin/nvr", "/usr/bin/nvr", "-r", "now", nullptr);
	                SPDLOG_ERROR("Failed to exec nvr.");
	                exit(-1);
	            }      
	        }
        	sleep(1);
        }
    }

    
    void update_manager::update_process()
    {
        int result = update_start;
        static const char *new_udt_file = update_file_.c_str();
        static const char *old_udt_file = nvr_file_.c_str();
        static const char *rename_file = nvr_file_.c_str();

        SPDLOG_INFO("nvr rename.....\n");
        try
        {
            fs::rename(old_udt_file, "/usr/bin/_nvr");
        } 
        catch (const fs::filesystem_error& e) 
        {
            std::cerr << "リネームに失敗: " << e.what() << '\n';
            update_result_.store(update_err1);
            goto END;
        }

        SPDLOG_INFO("SDカードから /usr/bin/nvr にコピーしています...\n");	
        try 
        {
            fs::copy_file(new_udt_file, old_udt_file, fs::copy_options::overwrite_existing);
        }
        catch (const fs::filesystem_error& e) 
        {
            std::cerr << "コピーに失敗: " << e.what() << '\n';
            update_status_.store(update_err2);
            goto END;
        }
        
        update_status_.store(update_ok);
        sync();

        
        END:
        SPDLOG_WARN("<<< update_process {}",result);

    }
}




