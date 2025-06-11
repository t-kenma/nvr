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
#include <stdio.h>
#include <cstdio> 
#include <stdlib.h>
#include <libfdisk/libfdisk.h>
#include <cstdlib>
#include <sys/resource.h> 
#include <spawn.h>      // posix_spawn

namespace nvr {
    const char *MKFS_PATH = "/sbin/mkfs.vfat";
    const std::filesystem::path inf1 = "/mnt/sd/EVC/REC_INF1.dat";
    const std::filesystem::path inf2 = "/mnt/sd/EVC/REC_INF2.dat";
    const std::filesystem::path inf3 = "/mnt/sd/EVC/REC_INF3.dat";
   	const std::filesystem::path root_mntsd = "/mnt/sd";
    const std::filesystem::path root_mmc = "/dev/mmcblk1";
    const int sd_manager::mount_state_not_mounted = 0;
    const int sd_manager::mount_state_mounting = 1;
    const int sd_manager::mount_state_mounted = 2;
    const int sd_manager::format_result_none = 0;
    const int sd_manager::format_result_success = 1;
    const int sd_manager::format_result_error = 2;
    const int sd_manager::format_result_nonstandard = 3;
    const int sd_manager::update_result_none = 0;
    const int sd_manager::update_result_success = 1;
    const int sd_manager::update_result_error = 2;
    
    namespace fs = std::filesystem;
    

    static const size_t BUFSIZE = 10240;
    
    #define UNBIND_PATH "/sys/devices/platform/soc/subsystem/devices/11c10000.mmc/driver/unbind"
	#define BIND_PATH   "/sys/devices/platform/soc/subsystem/devices/11c10000.mmc/subsystem/drivers/renesas_sdhi_internal_dmac/bind"
	#define DEVICE      "11c10000.mmc"
/*----------------------------------------------------------
----------------------------------------------------------*/
    int sd_manager::start_format()
    {
        SPDLOG_DEBUG("start_format");
        if (thread_.joinable()) {
            SPDLOG_WARN("format thread is running");
            return -1;
        }

        format_result_.store(format_result_none);
        thread_ = std::thread([this]{ this->format_process(); });
        
        return 0;
    }


/*----------------------------------------------------------
----------------------------------------------------------*/
    void sd_manager::format_process()
    {
        int result = format_result_none;
        pid_t pid;
        int status = 0;
        int rc = 0;
        std::stringstream tid;
        
        uint64_t sector_count = 0;
        uint64_t total_bytes = 0;
        uint64_t Usable_byte = 0;
        uint64_t file_count = 0;
        
        

        tid << std::this_thread::get_id();

        SPDLOG_DEBUG(">>> format_process {}", tid.str());
        if (!check_proc_mounts()) {
            rc = mount_sd();
            if (rc == 0 && is_root_file_exists() == 0 ) {
                result = format_result_success;
                goto END;
            }
        }

        if (check_proc_mounts()) {
            if (is_root_file_exists() == 0 ) {
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
        
        
       	unsigned char sz_kb;
		eeprom_->Read_FSize( &sz_kb );
        
        sector_count = get_sector_count();                    	//セクター数 
        total_bytes = sector_count * 512;                     	//総容量
        Usable_byte = total_bytes - ((total_bytes/100)*5);    	//予備5％除いたの使用可能容量
        file_count = Usable_byte / (sz_kb * 1024); 				//保存可能枚数
        
        

        SPDLOG_DEBUG("Start mkfs.vfat");
        pid = fork();
        if (pid == 0) {
            execl(MKFS_PATH, MKFS_PATH, "-f", "2", "-F", "32", device_file_.c_str(), nullptr);
            exit(-1);
        } else if (pid < 0) {
            SPDLOG_ERROR("Failed to fork format process: {}", strerror(errno));
 //           logger_->write("E SDカードフォーマットエラー");
            result = format_result_error;
            goto END;
        }

        waitpid(pid, &status, 0);

        if (!WIFEXITED(status)) {
            SPDLOG_ERROR("mkfs is not exited.");
//            logger_->write("E SDカードフォーマットエラー");
            result = format_result_error;
            goto END;
        }

        rc = WEXITSTATUS(status);
        SPDLOG_DEBUG("End mkfs.vfat: {}", rc);
        if (rc != 0) {
            SPDLOG_ERROR("mkfs exit code: {}", rc);
//            logger_->write("E SDカードフォーマットエラー");
            result = format_result_error;
            goto END;
        }

        rc = mount_sd();
        if (rc) {
            SPDLOG_ERROR("Failed to mount sd: {}", strerror(errno));
//            logger_->write("E SDカードイニシャライズエラー");
            result = format_result_error;
            goto END;
        }

       if ( is_root_file_exists() != 0) {
            rc = create_root_file();
            if (rc) {
//                logger_->write("E SDカードイニシャライズエラー");
                result = format_result_error;
                goto END;
            }
        }

        sync();

        result = format_result_success;
    END:
        format_result_.store(result);
        if( result ==  format_result_error )
        {
        	//---SDアクセス異常
			//
			logger_->LogOut( 9 );
        }
        
        
        SPDLOG_DEBUG("<<< format_process {} {}", tid.str(), result);
    }
    
    
/*----------------------------------------------------------
----------------------------------------------------------*/
    bool sd_manager::end_format()
    {
        if (!thread_.joinable()) {
            SPDLOG_WARN("format thread is not joinable.");
            return false;
        }

        thread_.join();
        SPDLOG_INFO("format thread is joined.");

        return true;
    }

/*----------------------------------------------------------
----------------------------------------------------------*/
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


/*----------------------------------------------------------
----------------------------------------------------------*/
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


/*----------------------------------------------------------
----------------------------------------------------------*/    
    int sd_manager::mount_sd_ro()
    {
        return mount(
            device_file_.c_str(),
            mount_point_.c_str(),
            "vfat",
            MS_NOATIME|MS_NOEXEC|MS_RDONLY,
            "errors=continue"
        );
    }


/*----------------------------------------------------------
----------------------------------------------------------*/
    int sd_manager::unmount_sd() {
        return umount(mount_point_.c_str());
    }


/*----------------------------------------------------------
----------------------------------------------------------*/
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


/*----------------------------------------------------------
----------------------------------------------------------*/
/*
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
*/

/*----------------------------------------------------------
----------------------------------------------------------*/
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
        sprintf(numStr, "%lu\r\n", file_count);

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
        
        
        
        //---inf3ファイルの作成
        //
        fd = open(inf3.c_str(), O_CREAT | O_WRONLY | O_TRUNC | O_CLOEXEC, S_IRWXU);
        if (fd == -1) {
            SPDLOG_ERROR("Failed to open {}: {}", inf3.c_str(), strerror(errno));
            return -3;
        }
    

        //---inf3を閉じる
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


/*----------------------------------------------------------
----------------------------------------------------------*/
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
    bool sd_manager::is_mmc_file_exists() noexcept        
    {
        std::error_code ec;
        return std::filesystem::exists(root_mmc, ec);
    }
	
	
/*----------------------------------------------------------
----------------------------------------------------------*/
    int sd_manager::is_sd_card()
    {	
        //---SDカードが挿入されているか
        //
        SPDLOG_INFO("is_sd_card w");
        
        
        int res = 100;
        static int wait = 0;
        static int count_ro = 10;
        
        if( wait > 0 )
        {
        	wait--;
        	SPDLOG_INFO("is_sd_card wait  cnt = {}",wait);
        	return -1;
        }
        
        
        //---
        //
        if( !is_mmc_file_exists() )
        {
        	count_ro = 10;
            SPDLOG_INFO("is_mmc_file_exists false");
            return -1;
        }
        
        
        //---
        //
        if ( !is_device_file_exists() )
        {
			SPDLOG_INFO("is_SD no partition  format");
			return -3;
        }
        
        
        SPDLOG_INFO("is_device_file_exists true");
        
        
        //--- Read only SD
        //
        if( count_ro == 0 )
		{
			return 1;
		}

        //---SDカードがマウントされているか
        //
        if ( !check_proc_mounts() )
        {    
            res = mount_sd();
			SPDLOG_INFO("check_proc_mounts mount = {}",res);
			if(res == -1 )
			{
				if(mount_sd_ro() == 0 )
				{
					res = unmount_sd();
					
					if( count_ro > 0 )
					{
						count_ro--;
					}
				
					SPDLOG_INFO("count_ro = {}",count_ro);
					SPDLOG_INFO("check_proc_mounts unmount = {}",res);
					return 1;
				}			

				SPDLOG_INFO("no SD");
				res = unmount_sd();
				SPDLOG_INFO("check_proc_mounts unmount = {}",res);
				
				SPDLOG_INFO("write_to_sysfs");
				write_to_sysfs(UNBIND_PATH, DEVICE);
				write_to_sysfs(BIND_PATH, DEVICE);					
				wait = 10;
				return 2;
			}
			else
			if(!check_proc_mounts() )
			{
                res = unmount_sd();
                SPDLOG_INFO("Re check_proc_mounts NG unmount = {}",res);
                return -1;
            }
            
        }
        
        SPDLOG_INFO("check_proc_mounts true");
        
        //---update check
        //
		if( check_update() )
		{
			return -1;
		}
        
        
        return 0;
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

    int sd_manager::is_writprotect()
    {
    	SPDLOG_INFO("writprotect CHECK");
        if(mount_sd_ro() != 0 )
        {
        	SPDLOG_INFO("no SD");
        	return -1;
        }
        
        unmount_sd();
        
        if( mount_sd() != 0 )
        {
        	//read only
        	SPDLOG_INFO("is sd READ ONLY");
        	return 1;        
        }
        
        SPDLOG_INFO("is sd Read Write");
        
        unmount_sd();
        
        return 2;
    }
    

    /*----------------------------------------------------------
    ----------------------------------------------------------*/
    int sd_manager::is_root_file_exists() noexcept
    {
        std::error_code ec;
         if (!fs::exists(inf3, ec)) {
            SPDLOG_INFO("no inf3");
            return -2;
        }
        
        
        SPDLOG_INFO("sd_manager::is_root_file_exists()");
        if (!fs::exists(inf1, ec)) {
            SPDLOG_INFO("no inf1");
            return -1;
        }

        if (!fs::exists(inf2, ec)) {
            SPDLOG_INFO("no inf2");
            return -1;
        }

		SPDLOG_INFO("sd_manager::is_root_file_exists() true");
        return 0;
    }
    
    /*------------------------------------------------------
	------------------------------------------------------*/
	bool sd_manager::check_update()
	{
		const char *bin_dir = "/mnt/sd"; 
		fs::path path(bin_dir);
		std::error_code ec;	
		
		
		//---/mnt/sd ディレクトリの存在確認
		//
		if (!fs::exists(path, ec)) {
			return false;
		}
		
		//---SDフォルダにアップデータフォルダ(鍵付きZIP)があるかの確認
		//
		for (const fs::directory_entry& x : fs::directory_iterator(path)) 
		{
			
			std::string filename = x.path().filename().string();
			
			int pos = filename.find("EVC");
			std::cout << pos << std::endl;
			
			//フォルダ名にEVCとついたフォルダがあるかの確認(実行ファイル)
			//
			if(pos != 0){
				continue;
			}
			
			pos = filename.length() - filename.find(".zip");
			if (filename.size() >= 4 && filename.substr(filename.size() - 4) == ".zip") {
				SPDLOG_INFO("ZIP file detected: {}", filename);
			} else {
				continue;
			}		
			
			SPDLOG_INFO("is update file");
			return true;
		}
		
		return false;
	}
	
	
    /*------------------------------------------------------
	------------------------------------------------------*/	
	bool sd_manager::try_read_device() 
	{
		const std::string device_path = "/dev/mmcblk1";
		
    	int fd = open(device_path.c_str(), O_RDONLY);
		if (fd < 0) {
		    std::cerr << "open failed: " << strerror(errno) << "\n";
		    return false;
		}

		char buffer[512];
		ssize_t ret = read(fd, buffer, sizeof(buffer));
		close(fd);

		if (ret < 0) {
		    std::cerr << "read failed: " << strerror(errno) << "\n";
		    return false;
		}

    	return true;
	}


    /*------------------------------------------------------
	------------------------------------------------------*/
	void sd_manager::trigger_rescan() 
	{
		std::ofstream rescan("/sys/class/mmc_host/mmc1/rescan");
		if (rescan.is_open()) {
		    rescan << "1" << std::endl;
		    std::cout << "Triggered rescan\n";
		} else {
		    std::cerr << "Failed to write to rescan\n";
		}
	}
	
	/*----------------------------------------------------------
	----------------------------------------------------------*/
	void sd_manager::write_to_sysfs(const char *path, const char *value) {
		 FILE *fp = fopen(path, "w");
		if (!fp) {
		    perror(path);
		    exit(EXIT_FAILURE);
		}

		if (fwrite(value, strlen(value), 1, fp) != 1) {
		    perror("fwrite");
		    fclose(fp);
		    exit(EXIT_FAILURE);
		}

		fclose(fp);
	}


/*----------------------------------------------------------
----------------------------------------------------------*/
	int sd_manager::create_partition()
	{
		const char *devpath = root_mmc.c_str();
		int result = 0;
		struct fdisk_context *cxt = fdisk_new_context();
		if (!cxt) {
		    fprintf(stderr, "fdisk_new_context failed\n");
		    return -1;
		}
		
		
		//---デバイスを割り当てる（読み書き可）
		//
		if (fdisk_assign_device(cxt, devpath, 0) != 0) {
		    fprintf(stderr, "fdisk_assign_device failed\n");
		    fdisk_unref_context(cxt);
		    return -2;
		}
		
		
		//---パーティションラベル作成
		//		
		if (fdisk_create_disklabel(cxt, "dos") != 0) 
		{
	        fprintf(stderr, "Failed to create GPT label\n");
	        fdisk_unref_context(cxt);
	        return -3;
	    }
		SPDLOG_INFO("fdisk_create_disklabel");
		
		if ( fdisk_has_label(cxt)) 
		{
			struct fdisk_label *label = fdisk_get_label(cxt, NULL);
			const char *label_name = fdisk_label_get_name(label);
			SPDLOG_INFO("fdisk_get_label {}",label_name);
		}
		//---使用可能なフリースペースを探す
		//
		fdisk_sector_t last_lba = fdisk_get_last_lba(cxt);
		
		
		//---1MB後ろから開始
		//
		fdisk_sector_t start = 2048;
		fdisk_sector_t end = last_lba;
		SPDLOG_INFO("last_lba = {}", last_lba);
		SPDLOG_INFO("Partition start = {}", start);
		SPDLOG_INFO("Partition end   = {}", end);
		SPDLOG_INFO("Partition size  = {}", end - start);
		
		fdisk_sector_t start_lba = fdisk_get_first_lba(cxt);
		SPDLOG_INFO("First LBA = {}", start_lba);
		SPDLOG_INFO("Last LBA  = {}", last_lba);

		
		
		//---新規パーティション構築
		//
		struct fdisk_partition *part = fdisk_new_partition();
		/*
		result = fdisk_partition_start_follow_default(part, 1);
		SPDLOG_INFO("fdisk_partition_start_follow_default = {}",result);
		//fdisk_partition_end_follow_default(part, 1);
		result = fdisk_partition_partno_follow_default(part, 1);
		SPDLOG_INFO("fdisk_partition_partno_follow_default = {}",result);
		*/
		
		result = fdisk_partition_set_start(part, start);
		SPDLOG_INFO("fdisk_partition_set_start = {}",result);
		result = fdisk_partition_set_size(part, end - start);
		SPDLOG_INFO("fdisk_partition_set_size = {}",result);
		
		result = fdisk_partition_partno_follow_default(part, 0);
		SPDLOG_INFO("fdisk_partition_partno_follow_default = {}",result);
		result = fdisk_partition_set_partno(part, 0);
		SPDLOG_INFO("fdisk_partition_set_partno = {}",result);
		
		//---タイプの指定（FAT32）
		//
		
//		struct fdisk_parttype *ptype = fdisk_new_parttype();	
		struct fdisk_parttype *ptype = fdisk_label_parse_parttype( fdisk_get_label(cxt, NULL), "0C");		
		if (!ptype) {
			SPDLOG_ERROR("Failed to create parttype");
			return -2;
		}		
		/*
		result = fdisk_parttype_set_code(ptype, 0x0C); // FAT32 (LBA)
		SPDLOG_INFO("fdisk_parttype_set_code = {}", result);
		*/
		
		// ここで partition に直接 type を設定する
		result = fdisk_partition_set_type(part, ptype);
		SPDLOG_INFO("fdisk_partition_set_type = {}", result);


		// ---既存パーティション確認
		//
		struct fdisk_table *table = NULL;
		fdisk_get_partitions(cxt, &table);

		int nparts = fdisk_table_get_nents(table);
		SPDLOG_INFO("Existing partition count: {}", nparts);

		for (int i = 0; i < nparts; ++i) {
			struct fdisk_partition *existing = fdisk_table_get_partition(table, i);
			fdisk_sector_t ex_start = fdisk_partition_get_start(existing);
			fdisk_sector_t ex_size = fdisk_partition_get_size(existing);
			SPDLOG_INFO("Partition {}: start={}, size={}", i, ex_start, ex_size);
		}


		//---パーティション追加
		//
		result = fdisk_add_partition(cxt, part, NULL);
		if (result != 0) {
			SPDLOG_ERROR("Failed to add partition result = {}",result);
		    fdisk_unref_partition(part);
		    fdisk_unref_context(cxt);
		    return -4;
		}
		
		
		//---パーティションテーブルをディスクに書き込む
		//
		if (fdisk_write_disklabel(cxt) != 0) {
		    fprintf(stderr, "Failed to write disklabel\n");
		    fdisk_unref_partition(part);
		    fdisk_unref_context(cxt);
		    return -5;
		}

		fdisk_unref_partition(part);
		fdisk_unref_context(cxt);
		printf("FAT32 partition created successfully\n");

		return 0;
	}
	
	
	/*----------------------------------------------------------
	----------------------------------------------------------*/
	bool sd_manager::sd_card_has_files()
	{
		std::vector<std::string> file_list;
		
		try 
		{
        	for (auto it = fs::recursive_directory_iterator(root_mntsd); it != fs::recursive_directory_iterator(); ++it) 
        	{
        	    const auto& path = it->path();
       	     	std::string name = path.filename().string();

        	    // 隠しファイル・ディレクトリ or System Volume Information をスキップ
		        if (!name.empty() && (name[0] == '.' || name == "System Volume Information")) {
		            if (fs::is_directory(path)) {
		                it.disable_recursion_pending();  // サブディレクトリもスキップ
		            }
		            continue;
		        }

		        if (fs::is_regular_file(path)) 
		        {
                	SPDLOG_INFO("sd_card_has_files() true  {}",name);
			        return true;  // ファイルを1つ見つけたら終了
		        }
		    }
		} catch (const fs::filesystem_error& e) {
		    std::cerr << "Filesystem error: " << e.what() << '\n';
		} 
		
		SPDLOG_INFO("sd_card_has_files() false " );
		return false;  // ファイルなし、またはエラー
	}
}





