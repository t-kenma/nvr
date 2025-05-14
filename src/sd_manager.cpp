#include <cerrno>
#include <iostream>
#include <fstream>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/mount.h>
#include <sstream>
#include "sd_manager.hpp"
#include "logging.hpp"
#include "util.hpp"
#include <filesystem>

namespace nvr {
    const char *MKFS_PATH = "/sbin/mkfs.vfat";
    const std::filesystem::path inf1 = "/mnt/sd/EVC/REC_INF1.dat";
    const std::filesystem::path inf2 = "/mnt/sd/EVC/REC_INF2.dat";
    namespace fs = std::filesystem;
    const int sd_manager::mount_state_not_mounted = 0;
    const int sd_manager::mount_state_mounting = 1;
    const int sd_manager::mount_state_mounted = 2;
    const int sd_manager::format_result_none = 0;
    const int sd_manager::format_result_success = 1;
    const int sd_manager::format_result_error = 2;
    const int sd_manager::update_result_none = 0;
    const int sd_manager::update_result_success = 1;
    const int sd_manager::update_result_error = 2;


    static const size_t BUFSIZE = 10240;

    int sd_manager::start_format()
    {
        SPDLOG_DEBUG("start_format");
        if (thread_.joinable()) {
            SPDLOG_WARN("format thread is running");
            return 0;
        }

        format_result_.store(format_result_none);
        thread_ = std::thread([this]{ this->format_process(); });

        return 0;
    }

    void sd_manager::format_process()
    {
        int result = format_result_none;
        pid_t pid;
        int status = 0;
        int rc = 0;
        std::stringstream tid;

        tid << std::this_thread::get_id();

        SPDLOG_DEBUG(">>> format_process {}", tid.str());
        if (!check_proc_mounts()) {
            rc = mount_sd();
            if (rc == 0 && is_root_file_exists()) {
                result = format_result_success;
                goto END;
            }
        }

        if (check_proc_mounts()) {
            if (is_root_file_exists()) {
                result = format_result_success;
                goto END;
            }

            rc = unmount_sd();
            if (rc) {
                SPDLOG_ERROR("Failed to unmount: {}", strerror(errno));
                result = format_result_error;
                goto END;
            }
        }

        SPDLOG_DEBUG("Start mkfs.vfat");
        pid = fork();
        if (pid == 0) {
            execl(MKFS_PATH, MKFS_PATH, "-f", "2", "-F", "32", device_file_.c_str(), nullptr);
            exit(-1);
        } else if (pid < 0) {
            SPDLOG_ERROR("Failed to fork format process: {}", strerror(errno));
            logger_->write("E SDカードフォーマットエラー");
            result = format_result_error;
            goto END;
        }

        waitpid(pid, &status, 0);

        if (!WIFEXITED(status)) {
            SPDLOG_ERROR("mkfs is not exited.");
            logger_->write("E SDカードフォーマットエラー");
            result = format_result_error;
            goto END;
        }

        rc = WEXITSTATUS(status);
        SPDLOG_DEBUG("End mkfs.vfat: {}", rc);
        if (rc != 0) {
            SPDLOG_ERROR("mkfs exit code: {}", rc);
            logger_->write("E SDカードフォーマットエラー");
            result = format_result_error;
            goto END;
        }

        rc = mount_sd();
        if (rc) {
            SPDLOG_ERROR("Failed to mount sd: {}", strerror(errno));
            logger_->write("E SDカードイニシャライズエラー");
            result = format_result_error;
            goto END;
        }

        if (!is_root_file_exists()) {
            rc = create_root_file();
            if (rc) {
                logger_->write("E SDカードイニシャライズエラー");
                result = format_result_error;
                goto END;
            }
        }

        sync();

        result = format_result_success;
    END:
        format_result_.store(result);
        SPDLOG_DEBUG("<<< format_process {} {}", tid.str(), result);
    }

    bool sd_manager::wait_format()
    {
        if (!thread_.joinable()) {
            SPDLOG_WARN("format thread is not joinable.");
            return false;
        }

        auto result = format_result_.load();
        if (result == format_result_none) {
            return false;
        }
        thread_.join();
        SPDLOG_DEBUG("format thread is joined.");

        return true;
    }

    int sd_manager::mount_sd()
    {
        return mount(
            device_file_.c_str(),
            mount_point_.c_str(),
            "vfat",
            MS_NOATIME|MS_NOEXEC,
            "errors=continue"
        );
    }

    int sd_manager::unmount_sd() {
        return umount(mount_point_.c_str());
    }

    void sd_manager::timer_process()
    {
        static const std::filesystem::path proc_mounts{"/proc/mounts"};
        static const char *new_udt_file = update_file_.c_str();
        static const char *old_udt_file = nvr_file_.c_str();
        int status = get_mount_status();
        int counter = counter_;

        if (counter == 20) {
            counter_ = 0;
        } else {
            counter_++;
        }

        if (counter != 0) {
            return;
        }

        if (status == mount_state_mounted) {
            if (!check_mount_point()) {
                status = mount_state_not_mounted;
            }
        }

        if (status == mount_state_not_mounted) {
            // SPDLOG_DEBUG("timer_process not_mounted");
            SPDLOG_INFO("state_sd_waiting");
            if (check_proc_mounts() && is_root_file_exists()) {
                SPDLOG_INFO("mount_state_mounted");
                set_mount_status(mount_state_mounted);
            } else if (is_device_file_exists()) {
            	SPDLOG_INFO("mount_state_mounting");
                set_mount_status(mount_state_mounting);
                start_format();
            }
            return;
        }

        if (status == mount_state_mounting) {
            // SPDLOG_DEBUG("timer_process mounting");
            if (wait_format()) {
                auto result = format_result_.load();
                if (result == format_result_success) {
                    SPDLOG_INFO("Set mount_status_t to mounted");
                    set_mount_status(mount_state_mounted);
                    return;
                }
                SPDLOG_INFO("Set mount_status_t to not_mounted");
            }
            set_mount_status(mount_state_not_mounted);

            return;
        }
    }

    bool sd_manager::check_proc_mounts()
    {
        static const std::filesystem::path proc_mounts{"/proc/mounts"};
        static const char *dev_file = device_file_.c_str();

        try
        {
            std::ifstream ifs(proc_mounts);
            std::string line;

            while (std::getline(ifs, line))
            {
                if (line.find(dev_file) != std::string::npos)
                {
                    return true;
                }
            }
        }
        catch (std::exception &ex)
        {
            SPDLOG_ERROR("Failed to check mout point: {}.", ex.what());
        }
        
        return false;
    }

    bool sd_manager::check_mount_point()
    {
        int status = get_mount_status();
        if (status == mount_state_mounted) {
            if (is_root_file_exists()) {
                return true;
            }
            set_mount_status(mount_state_not_mounted);
        }
        return false;
    }


    int sd_manager::create_root_file()
    {
        //以下SDのrootに作成
        /*************debug用************************* */
        uint64_t record_file_size = 64;

        /********************************************* */

        //---EVCのフォルダ作成
        //
        if( !create_dir("/mnt/sd/EVC")){
            return -7;
        }


        //作成可能なファイル数を取得
        //
        uint64_t sector_count = get_sector_count();                             //セクター数 
        uint64_t total_bytes = sector_count * 512;                          //総容量
        uint64_t Usable_byte = total_bytes - ((total_bytes/100)*5);    //予備5％除いたの使用可能容量
        uint64_t file_count = Usable_byte / (record_file_size * 1024); //保存可能枚数

        SPDLOG_INFO("SDカードのセクター数= {}",sector_count );
        SPDLOG_INFO("総容量= {}",total_bytes );
        SPDLOG_INFO("使用可能容量",Usable_byte );
        SPDLOG_INFO("ファイル作成可能数= {}",file_count );

        char numStr[256];
        sprintf(numStr, "%lu", file_count);

        int len = strlen(numStr);
        //---inf1ファイルの作成
        //
        int rc;
        int fd = open(inf1.c_str(), O_CREAT | O_WRONLY | O_TRUNC | O_CLOEXEC, S_IRWXU);
        if (fd == -1) {
            SPDLOG_ERROR("Failed to open {}: {}", inf1.c_str(), strerror(errno));
            return -1;
        }


        //---inf1に作成可能なファイル数を記載
        //  
        rc = write(fd, numStr, len);
        if (rc < 0) {
            SPDLOG_ERROR("Failed to write {}: {}", inf1.c_str(), strerror(errno));
            close(fd);
            return -2;
        }


        //----inf1を閉じる
        close(fd);


        //---inf2ファイルの作成
        //
        fd = open(inf2.c_str(), O_CREAT | O_WRONLY | O_TRUNC | O_CLOEXEC, S_IRWXU);
        if (fd == -1) {
            SPDLOG_ERROR("Failed to open {}: {}", inf2.c_str(), strerror(errno));
            return -3;
        }
        
        
        //---inf2に現在のファイルIDXを記載(フォーマット時は0)
        //
        const char *buf = "0";
        rc = write(fd, buf, 1);
        if (rc < 0) {
            SPDLOG_ERROR("Failed to write {}: {}", inf2.c_str(), strerror(errno));
            close(fd);
            return -4;
        }


        //---inf2を閉じる
        close(fd);


        //---作成可能ファイル数からdir3を作成するかの判断
        //
        std::error_code ec;
        if(file_count > 999999){
            //---DIR1_00/DIR2_00/DIR3_00ディレクトリ作成
            //
            if( !create_dir("/mnt/sd/EVC/DIR1_00/DIR2_00/DIR3_00")){
                return -5;
            }
        }
        else
        {
            //---DIR1_00/DIR2_00ディレクトリ作成
            //
            if( !create_dir("/mnt/sd/EVC/DIR1_00/DIR2_00")){
                return -6;
            }
            
            /*            
            fs::path path("/mnt/sd/DIR1_00/DIR2_00");
            if (!fs::exists(path, ec))
            {
                if (!fs::create_directories(path, ec)) {
                    SPDLOG_ERROR("Failed to make directory: {}.", path.c_str());   
                }
            }
            */
        }

        //---sync
        
        return 0;
    }

    uint64_t sd_manager::get_sector_count()
    {
        std::ifstream file("/sys/block/mmcblk1/size");
        if (!file.is_open()) {
            throw std::runtime_error("Failed to open sector size file: ");
        }
    
        uint64_t sector_count = 0;
        file >> sector_count;
        return sector_count;
    }


    /*----------------------------------------------------------
    ----------------------------------------------------------*/
    bool sd_manager::is_sd_card()
    {

        //---SDカードが挿入されているか
        //
        if ( !is_device_file_exists() ){
            SPDLOG_INFO("is_device_file_exists false");
            return false;
        }


        //---SDカードがマウントされているか
        //
        if ( !check_proc_mounts() ){
            mount_sd();
            if(!check_proc_mounts()){
                SPDLOG_INFO("check_proc_mounts false");
                return false;
            }
        }
        
        //---SDカードを読めるか
        //
        if( !is_root_file_exists() ) {
            SPDLOG_INFO("is_root_file_exists false");
            return false;
        }
        
        
        return true;
    }

    /*----------------------------------------------------------
    ----------------------------------------------------------*/
    int sd_manager::is_formatting()
    {
        int res;
        auto result = format_result_.load();
        if (result == format_result_none) {
            res = 0;
        }
        else
        if (result == format_result_success) {
            res = 1;
        }
        else
        if (result == format_result_error) {
            res = 2;
        }
        
        return res;
    }


    /*----------------------------------------------------------
    ----------------------------------------------------------*/
    bool sd_manager::is_root_file_exists() noexcept
    {
        std::error_code ec;

        if (!fs::exists(inf1, ec)) {
            SPDLOG_INFO("no inf1");
            return false;
        }

        if (!fs::exists(inf2, ec)) {
            SPDLOG_INFO("no inf2");
            return false;
        }

        return true;
    }
}




