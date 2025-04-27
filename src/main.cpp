#include <cstdio>
#include <cstdlib>
#include <cerrno>
#include <atomic>
#include <getopt.h>
#include <filesystem>
#include <sys/epoll.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/signalfd.h>
#include <netinet/in.h>
#include <net/if.h>
#include <iostream>
#include "config.hpp"
#include "pipeline.hpp"
#include "video_writer.hpp"
#include "gpio.hpp"
#include "sd_manager.hpp"
#include "reset_monitor.hpp"
#include "util.hpp"
#include "logging.hpp"
#ifdef G_OS_UNIX
#include <glib-unix.h>
#endif
#ifdef __APPLE__
#include "avf_src.hpp"
#endif

struct callback_data_t {
    callback_data_t()
        : timer1_id(0),
          timer2_id(0),
          bus_watch_id(0),
          signal_int_id(0),
          signal_term_id(0),
          signal_user1_id(0),
          signal_user2_id(0),
          led_manager(nullptr),
          sd_manager(nullptr),
          reset_monitor(nullptr),
          jpeg_file(nullptr),
          pipeline(nullptr),
          power_monitor(nullptr),
          jpeg_time(0),
          is_video_error(false),
          do_reboot(false),
          interrupted(false),
          is_power_pin_high(false),
          main_loop(nullptr)
    {}

    guint timer1_id;
    guint timer2_id;
    guint bus_watch_id;
    guint signal_int_id;
    guint signal_term_id;
    guint signal_user1_id;
    guint signal_user2_id;

    std::shared_ptr<nvr::pipeline> pipeline;
    std::shared_ptr<nvr::video_writer> video_writer;
    std::shared_ptr<nvr::logger> logger;
    nvr::led_manager *led_manager;
    nvr::sd_manager *sd_manager;
    nvr::reset_monitor *reset_monitor;
    
    
    nvr::gpio_out *rst_decoder;
    nvr::gpio_out *pwd_decoder;
    nvr::gpio_out *alarm_out_a;
    nvr::gpio_out *alarm_out_b;
    nvr::gpio_out *led_board_green;
    nvr::gpio_out *led_board_red;
    nvr::gpio_out *led_board_yel;
    
    nvr::gpio_in *check_bat;
    nvr::gpio_in *pgood;

    std::filesystem::path done_dir;
    const char *jpeg_file;

    nvr::power_monitor *power_monitor;
    std::atomic<std::time_t> jpeg_time;
    bool is_video_error;

    bool do_reboot;


    std::atomic<bool> interrupted;
    std::atomic<bool> is_power_pin_high;
#ifdef NVR_DEBUG_POWER
    nvr::gpio_out *tmp_out1;
#endif
    GMainLoop *main_loop;
};

namespace fs = std::filesystem;
static const char *g_interrupt_name = "nrs-video-recorder-interrupted";
static const char *g_quit_name = "nrs-video-recorder-quit";

static void set_element_rank(const gchar *name, guint rank)
{
    GstRegistry *registry = NULL;
    GstElementFactory *factory = NULL;

    registry = gst_registry_get();
    if (!registry)
    {
        SPDLOG_WARN("Failed to get registry.");
        return;
    }

    factory = gst_element_factory_find(name);
    if (!factory)
    {
        SPDLOG_WARN("Failed to find factory of {}", name);
        return;
    }

    gst_plugin_feature_set_rank(GST_PLUGIN_FEATURE(factory), rank);
    if (!gst_registry_add_feature(registry, GST_PLUGIN_FEATURE(factory)))
    {
        SPDLOG_WARN("Failed to add feature to registry.");
    }

    return;
}

int do_reboot() noexcept
{
    pid_t pid;
    int status;
    int rc;

    pid = fork();
    if (pid < 0) {
        SPDLOG_ERROR("Failed to fork process: {}", strerror(errno));
        return -1;
    } else if (pid == 0) {
        execl("/sbin/shutdown", "/sbin/shutdown", "-r", "now", nullptr);
        SPDLOG_ERROR("Failed to exec reboot.");
        exit(-1);
    }

    waitpid(pid, &status, 0);

    if (!WIFEXITED(status)) {
        return -1;
    }

    rc = WEXITSTATUS(status);
    return rc;
}

static void power_monitor_process(callback_data_t *data)
{
    // int soc = socket(AF_INET, SOCK_DGRAM, 0);
    int fd = data->power_monitor->fd();
    int epfd = epoll_create1(EPOLL_CLOEXEC);
    struct epoll_event ev;
    struct epoll_event events;
    // int n;
    // char buf[1];
    struct ifreq ifr;

    // strncpy(ifr.ifr_name, "eth0", IFNAMSIZ - 1);
    // ioctl(soc, SIOCGIFFLAGS, &ifr);
    // ifr.ifr_flags &= ~IFF_UP;

    ev.events = EPOLLPRI;
    ev.data.fd = fd;


    if (epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev)) {
        SPDLOG_ERROR("Faild to add fd to epfs: {}", strerror(errno));
    }

    while (!data->interrupted.load(std::memory_order_relaxed)) {
        if (!data->is_power_pin_high.load(std::memory_order_relaxed)) {
            char buf[1];
            lseek(fd, 0, SEEK_SET);
            if (::read(fd, buf, 1) == 1) {
                SPDLOG_DEBUG("power pin :{}", buf[0]);

                if (buf[0] == '1') {
                    data->is_power_pin_high.store(true, std::memory_order_relaxed);           
                }
            } else {
                SPDLOG_ERROR("Failed to read power pin: {}", strerror(errno));
            }
        }

        if (epoll_wait(epfd, &events, 1, 1000) > 0) {
            data->video_writer->set_powerdown();
            ::open("/dev/adv71800", O_RDONLY);
            // ::open("/dev/adin0", O_RDONLY);
            // ioctl(soc, SIOCSIFFLAGS, &ifr);
#ifdef NVR_DEBUG_POWER
            data->tmp_out1->write_value(true);
#endif
            do_reboot();
            return;
        }
    }
}

static gboolean timer1_cb(gpointer udata)
{
    callback_data_t *data = static_cast<callback_data_t *>(udata);
    std::time_t t = std::time(nullptr);
    const std::tm *tm = std::localtime(&t);

    if (tm->tm_sec == 0)
    {
        if (data->pipeline->is_running()) {
            data->pipeline->split_video_file();
        }
    }

    std::time_t jpeg_time = data->jpeg_time.load(std::memory_order_relaxed);
    
    const char *exe_dir = "/mnt/sd"; 
	fs::path path(exe_dir);
    
    try 
    {
        fs::copy_file("/tmp/video.jpg", path / g_strdup_printf(
            "%04d-%02d-%02d-%02d-%02d-%02d.jpeg",
            tm->tm_year + 1900,
            tm->tm_mon + 1,
            tm->tm_mday,
            tm->tm_hour,
            tm->tm_min,
            tm->tm_sec), 
            fs::copy_options::overwrite_existing);
    }
    catch (const fs::filesystem_error& e) 
    {
        std::cerr << "gazouコピーに失敗: " << e.what() << '\n';
    }
    
    
    if (jpeg_time <= t) {
        bool is_video_error = data->is_video_error;
        if ((t - jpeg_time) > 1) {
            if (!is_video_error) {
                data->led_manager->set_status(nvr::led_manager::state_error_video);
                data->logger->write("E カメラ映像同期エラー");
                data->is_video_error = true;
            }
        } else {
            if (is_video_error) {
                data->led_manager->clear_status(nvr::led_manager::state_error_video);
                data->is_video_error = false;
            }
        }
    }
    return G_SOURCE_CONTINUE;
}

gboolean send_interrupt_message(GstElement *pipeline) {
    if (!pipeline) {
        return FALSE;
    }

    /* post an application specific message */
    gboolean ret = gst_element_post_message(
        GST_ELEMENT(pipeline),
        gst_message_new_application(
            GST_OBJECT(pipeline),
            gst_structure_new(g_interrupt_name, "message", G_TYPE_STRING, "Pipeline interrupted", nullptr)));

    if (ret) {
        SPDLOG_DEBUG("Interrupt message sent.");
    }

    return ret;
}

static void on_interrupted(callback_data_t *data)
{
    send_interrupt_message(static_cast<GstElement*>(*data->pipeline));
    data->interrupted.store(true, std::memory_order_relaxed);
    data->video_writer->stop();
}

static gboolean timer2_cb(gpointer udata)
{
    callback_data_t *data = static_cast<callback_data_t *>(udata);
    int reset_status = 0;

    data->sd_manager->timer_process();
    reset_status = data->reset_monitor->update_value();

    data->led_manager->update_led();

    if (reset_status == 2) {
        on_interrupted(data);
        data->timer2_id = 0;
        return G_SOURCE_REMOVE;
    }
    return G_SOURCE_CONTINUE;
}

// static gboolean power_watch_cb(GIOChannel *channel, GIOCondition cond, gpointer udata)
// {
//     struct callback_data_t *data = static_cast<callback_data_t *>(udata);
//     data->video_writer->set_powerdown();
//     send_interrupt_message(static_cast<GstElement *>(*data->pipeline));
//     data->interrupted.store(true, std::memory_order_relaxed);
//     SPDLOG_DEBUG("Power state changed.");

//     data->power_watch_id = 0;
//     return G_SOURCE_REMOVE;
// }

static gboolean bus_watch_cb(GstBus * /* bus */, GstMessage *message, gpointer udata)
{
    struct callback_data_t *data = static_cast<callback_data_t *>(udata);

    switch (GST_MESSAGE_TYPE(message))
    {
    case GST_MESSAGE_ERROR:
    {
        GError *err;
        gchar *debug;

        gst_message_parse_error(message, &err, &debug);
        if (err) {
            // SPDLOG_ERROR("{}: {}", GST_OBJECT_NAME(message->src), err->message);
            g_error_free(err);
        }
        if (debug)
        {
            // SPDLOG_DEBUG("{}", debug);
            g_free(debug);
        }

        // if (g_strcmp0("rtsp_src", GST_OBJECT_NAME(message->src)) == 0) {
        //     gst_element_set_state(data->pipeline, GST_STATE_NULL);
        // } else {
        //     g_main_loop_quit(data->main_loop);
        // }

        if (data->interrupted.load(std::memory_order_relaxed)) {
            g_main_loop_quit(data->main_loop);
        } else {
            if (!data->pipeline->stop()) {
                g_main_loop_quit(data->main_loop);
            }
            SPDLOG_INFO("test_7");
            if (!data->pipeline->start()) {
                g_main_loop_quit(data->main_loop);
            }
        }
        break;
    }
    case GST_MESSAGE_EOS:
        /* end-of-stream */
        SPDLOG_DEBUG("End of stream.");
        if (data->interrupted.load(std::memory_order_relaxed)) {
            g_main_loop_quit(data->main_loop);
        } else {
            if (!data->pipeline->stop()) {
                g_main_loop_quit(data->main_loop);
            }
            SPDLOG_INFO("test_8");
            if (!data->pipeline->start()) {
                g_main_loop_quit(data->main_loop);
            }
        }
        break;

    // case GST_MESSAGE_STATE_CHANGED:
    // {
    //     // if (g_strcmp0("nrs-recorder-pipeline", GST_OBJECT_NAME(message->src)) == 0) {
    //     if (g_strcmp0("video_file_sink", GST_OBJECT_NAME(message->src)) == 0) {
    //         GstState old_state, new_state;
    //         gst_message_parse_state_changed (message, &old_state, &new_state, NULL);
    //         g_print ("Element %s changed state from %s to %s.\n",
    //             GST_OBJECT_NAME (message->src),
    //             gst_element_state_get_name (old_state),
    //             gst_element_state_get_name (new_state));
    //     }

    
    //    break;
    // }

    case GST_MESSAGE_ELEMENT:
    {
        const GstStructure *s = gst_message_get_structure(message);

        if (s && gst_structure_has_name(s, "splitmuxsink-fragment-closed"))
        {
            break;

            /*
            const gchar *location = gst_structure_get_string(s, "location");

            if (location)
            {
                std::filesystem::path dst = data->done_dir;
                dst.append(strrchr(location, '/') + 1);
                if (std::rename(location, dst.c_str()))
                {
                    SPDLOG_ERROR("Failed to rename {} to {}: {}", location, dst.c_str(), std::strerror(errno));
                }
                else
                {
                    SPDLOG_DEBUG("VIDEO {} is created.", dst.c_str());
                    data->video_writer->notify();
                }
            }
            */
        }
        else if (s && gst_structure_has_name(s, "GstMultiFileSink"))
        {
            const gchar *filename = gst_structure_get_string(s, "filename");

            if (filename)
            {
                std::time_t jpeg_time;
                if (std::rename(filename, data->jpeg_file) == -1)
                {
                    SPDLOG_ERROR("Failed to rename {} to {}: {}", filename, data->jpeg_file, std::strerror(errno));
                } else {
                    SPDLOG_DEBUG("Rename {} to {}", filename, data->jpeg_file);
                }
                jpeg_time = time(nullptr);
                data->jpeg_time.store(jpeg_time, std::memory_order_relaxed);
            }
        }
        break;
    }

    case GST_MESSAGE_APPLICATION:
    {
        const GstStructure *s = gst_message_get_structure(message);

        if (gst_structure_has_name(s, g_interrupt_name))
        {
            SPDLOG_DEBUG("Interrupted: Stopping pipeline.");
            if (data->pipeline->send_eos_event()) {
                SPDLOG_DEBUG("EOS message sent.");
            } else {
                SPDLOG_DEBUG("Failed to send eos event.");
            }

            if (data->pipeline->post_application_message(
                gst_structure_new(g_quit_name, "message", G_TYPE_STRING, "Quit", nullptr)
            )) {
                SPDLOG_DEBUG("Quit message sent.");
            } else {
                SPDLOG_DEBUG("Failed to send start message.");
            }
        } else if (gst_structure_has_name(s, g_quit_name)) {
            SPDLOG_DEBUG("Quit message.");
            g_main_loop_quit(data->main_loop);
        }
        break;
    }
    default:
        /* unhandled message */
        break;
    }

    return G_SOURCE_CONTINUE;
}

#ifdef G_OS_UNIX
static gboolean signal_user1_cb(gpointer udata)
{
    SPDLOG_INFO("SIGUSER1 receiverd.");
    callback_data_t* data = static_cast<callback_data_t*>(udata);

    send_interrupt_message(static_cast<GstElement*>(*data->pipeline));
    data->interrupted.store(true, std::memory_order_relaxed);
    data->video_writer->stop();
    data->do_reboot = true;

    data->logger->write("L リブート");
    /* remove signal handler */
    data->signal_user1_id = 0;
    return G_SOURCE_REMOVE;
}

static gboolean signal_user2_cb(gpointer udata)
{
    SPDLOG_INFO("SIGUSER2 receiverd.");
    callback_data_t* data = static_cast<callback_data_t*>(udata);

    return G_SOURCE_CONTINUE;
}

static gboolean signal_intr_cb(gpointer udata)
{
    SPDLOG_INFO("SIGINTR receiverd.");

    callback_data_t* data = static_cast<callback_data_t*>(udata);

    on_interrupted(data);

    /* remove signal handler */
    data->signal_int_id = 0;
    return G_SOURCE_REMOVE;
}

static gboolean signal_term_cb(gpointer udata)
{
    SPDLOG_INFO("SIGTERM receiverd.");

    callback_data_t* data = static_cast<callback_data_t*>(udata);

    on_interrupted(data);

    /* remove signal handler */
    data->signal_term_id = 0;
    return G_SOURCE_REMOVE;
}

#endif

static nvr::video_src *prepare_video_src(const nvr::config& config)
{
    nvr::video_src *ret = nullptr;

    if (config.is_video_src_rtsp())
    {
        return new nvr::rtsp_src(
            config.rtsp_url(),
            config.rtsp_user(),
            config.rtsp_password(),
            config.video_width(),
            config.video_height());
    }
    return new nvr::v4l2_src(config.video_width(), config.video_height());
}

static nvr::video_sink *prepare_video_sink(const char *tmp_dir, const nvr::config& config)
{
    nvr::video_sink *ret = nullptr;
    std::filesystem::path path(tmp_dir);
    path /= "video";

    std::error_code ec;
    if (!std::filesystem::is_directory(path, ec))
    {
        if (!std::filesystem::create_directories(path, ec))
        {
            SPDLOG_ERROR("Failed to make directory: {}.", path.c_str());
            return nullptr;
        }
    }
    SPDLOG_DEBUG("Video location directory is {}.", path.c_str());

    return new nvr::video_sink(
        path.c_str(),
        config.video_framerate(),
        config.video_bitrate()
    );
}

static nvr::jpeg_sink *prepare_jpeg_sink(const char *tmp_dir, const nvr::config& /* config */)
{
    nvr::jpeg_sink *ret = nullptr;
    std::filesystem::path path(tmp_dir);
    path /= "jpeg";

    std::error_code ec;
    if (!std::filesystem::is_directory(path, ec))
    {
        if (!std::filesystem::create_directories(path, ec))
        {
            SPDLOG_ERROR("Failed to make directory: {}.", path.c_str());
            return nullptr;
        }
    }

    path /= "image%d.jpg";
    SPDLOG_DEBUG("Jpeg location is {}.", path.c_str());

    return new nvr::jpeg_sink(path.c_str());
}

static std::filesystem::path prepare_done_directory(const char *tmp_dir)
{
    std::filesystem::path path(tmp_dir);
    path /= "done";

    std::error_code ec;
    if (!std::filesystem::is_directory(path, ec))
    {
        if (!std::filesystem::create_directories(path, ec))
        {
            SPDLOG_ERROR("Failed to make directory: {}.", path.c_str());
            return std::filesystem::path();
        }
    }

    try {
        for (const std::filesystem::directory_entry &entry : std::filesystem::directory_iterator(path))
        {
            if (entry.is_regular_file())
            {
                unlink(entry.path().c_str());
            }
        }
    } catch (const std::exception &err) {
        // Do nothing
    }
    return path;
}

static guint add_bus_watch(std::shared_ptr<nvr::pipeline> pipeline, callback_data_t *data)
{
    guint ret = 0;
    GstBus *bus = pipeline->bus();
    ret = gst_bus_add_watch(bus, (GstBusFunc)bus_watch_cb, data);
    gst_object_unref(bus);
    return ret;
}


static const std::filesystem::path pid_file_path{"/run/nvr.pid"};
static int write_pid_file()
{
    pid_t pid = getpid();
    FILE *fp = fopen(pid_file_path.c_str(), "w");
    if (fp == nullptr) {
        SPDLOG_ERROR("Failed to open {}: {}", pid_file_path.c_str(), strerror(errno));
        return -1;
    }
    fprintf(fp, "%d\n", pid);
    fflush(fp);
    fclose(fp);

    return 0;
}

static void unlink_pid_file()
{
    std::error_code ec;
    if (std::filesystem::exists(pid_file_path, ec)) {
        std::filesystem::remove(pid_file_path, ec);
    }
}

static bool wait_power_pin(std::shared_ptr<nvr::power_monitor> pm, callback_data_t *data)
{
    sigset_t mask;
    sigset_t old_mask;

    bool ret = false;
    int epfd = -1;
    int sfd = -1;
    int fd = pm->fd();
    unsigned char buf[1];
    struct epoll_event ev;
    struct epoll_event events;
    bool first = true;

    sigemptyset (&mask);
    sigaddset (&mask, SIGINT);
    sigaddset (&mask, SIGTERM);
    sigaddset (&mask, SIGUSR1);
    sigaddset (&mask, SIGUSR2);

    if (sigprocmask (SIG_BLOCK, &mask, nullptr) == -1) {
        SPDLOG_ERROR("Failed to sigprocmask: {}", strerror(errno));
        return ret;
    }

    sfd = signalfd (-1, &mask, SFD_NONBLOCK|SFD_CLOEXEC);
    if (sfd == -1) {
        SPDLOG_ERROR("Failed to signalfd: {}", strerror(errno));
        goto END;
    }

    epfd = epoll_create1(EPOLL_CLOEXEC);
    if (epfd == -1) {
        SPDLOG_ERROR("Failed to epoll_create1: {}", strerror(errno));
        goto END;
    }

    ev.events = EPOLLIN;
    ev.data.fd = sfd;

    if (epoll_ctl(epfd, EPOLL_CTL_ADD, sfd, &ev)) {
        SPDLOG_ERROR("Faild to add signal fd to epfs: {}", strerror(errno));
        goto END;
    }

    while (true) {
        int rc;

        rc = epoll_wait(epfd, &events, 1, 1000);
        if (rc > 0) {
            struct signalfd_siginfo info = {0};

            ::read (sfd, &info, sizeof(info));
            SPDLOG_INFO("Got signal {}", info.ssi_signo);

            if (info.ssi_signo == SIGINT || info.ssi_signo == SIGTERM) {
                data->interrupted.store(true, std::memory_order_relaxed);
                ret = true;
                break;
            } else if (info.ssi_signo == SIGUSR1) {
                data->interrupted.store(true, std::memory_order_relaxed);
                data->do_reboot = true;
                ret = true;
                break;
            } else if (info.ssi_signo == SIGUSR2) {
                nvr::do_systemctl("restart", "systemd-networkd");
            }
        }

        if (rc == 0) {
            if (data->is_power_pin_high.load(std::memory_order_relaxed)) {
                break;
            } else if (first) {
                SPDLOG_INFO("Wait power pin to be high.");
                first = false;
            }
        }
    }

END:
    if (epfd != -1) {
        close(epfd);
    }

    if (sfd != -1) {
        close(sfd);
    }
    sigprocmask (SIG_UNBLOCK, &mask, nullptr);

    return ret;
}

int main(int argc, char **argv)
{
    std::shared_ptr<nvr::pipeline> pipeline;
    std::shared_ptr<nvr::video_writer> video_writer;
    spdlog::set_level(spdlog::level::debug);

    callback_data_t data{};
    SPDLOG_INFO("ver0.1.1");
    
    std::shared_ptr<nvr::gpio_out> rst_decoder = std::make_shared<nvr::gpio_out>("168", "P6_0");
    std::shared_ptr<nvr::gpio_out> pwd_decoder = std::make_shared<nvr::gpio_out>("169", "P6_1");

    std::shared_ptr<nvr::gpio_out> led_board_green = std::make_shared<nvr::gpio_out>("193", "P9_1");
    std::shared_ptr<nvr::gpio_out> led_board_red = std::make_shared<nvr::gpio_out>("200", "P10_0");
    std::shared_ptr<nvr::gpio_out> led_board_yel = std::make_shared<nvr::gpio_out>("192", "P9_0");

    std::shared_ptr<nvr::gpio_in> check_bat = std::make_shared<nvr::gpio_in>("201", "P10_1");
    std::shared_ptr<nvr::gpio_in> pgood = std::make_shared<nvr::gpio_in>("208", "P11_0");

    std::shared_ptr<nvr::gpio_out> alarm_out_a = std::make_shared<nvr::gpio_out>("216", "P12_0");
    std::shared_ptr<nvr::gpio_out> alarm_out_b = std::make_shared<nvr::gpio_out>("217", "P12_1");

    std::shared_ptr<nvr::power_monitor> power_monitor = std::make_shared<nvr::power_monitor>("224", "P13_0");

    std::shared_ptr<nvr::gpio_in> cminsig = std::make_shared<nvr::gpio_in>("225", "P13_1");

    std::shared_ptr<nvr::reset_monitor> reset_monitor = std::make_shared<nvr::reset_monitor>("241", "P15_1", 1);

    std::shared_ptr<nvr::logger> logger = std::make_shared<nvr::logger>("/etc/nvr/video-recorder.log");

	if (pwd_decoder->open(true)) {
        SPDLOG_ERROR("Failed to open pwd_decoder.");
        exit(-1);
    }
	
	for(int i = 0;i < 1000000;i++);
	
    if (rst_decoder->open(true)) {
        SPDLOG_ERROR("Failed to open rst_decoder.");
        exit(-1);
    }
   

#ifdef NVR_DEBUG_POWER
    std::shared_ptr<nvr::gpio_out> tmp_out1 = std::make_shared<nvr::gpio_out>("232", "P14_0");
    // std::shared_ptr<nvr::gpio_out> tmp_out2 = std::make_shared<nvr::gpio_out>("233", "P14_1");
#endif
    bool use_sensor = false;

   
    gst_init(&argc, &argv);

    write_pid_file();

    pipeline = std::make_shared<nvr::pipeline>();

    if (mount(nullptr, "/", nullptr, MS_REMOUNT, nullptr)) {
        SPDLOG_ERROR("Failed to remount /.");
        exit(-1);
    }

    if (led_board_green->open(true)) {
        SPDLOG_ERROR("Failed to open led_board_green.");
        exit(-1);
    }

    if (led_board_red->open(true)) {
        SPDLOG_ERROR("Failed to open led_board_red.");
        exit(-1);
    }

    if (led_board_yel->open(true)) {
        SPDLOG_ERROR("Failed to open led_board_red.");
        exit(-1);
    }

    SPDLOG_INFO("test_2");
    std::shared_ptr<nvr::led_manager> led_manager = std::make_shared<nvr::led_manager>(led_board_green, led_board_red, alarm_out_a, alarm_out_b);

    {
        const char *config_file = "/etc/nvr/nvr.json";
        const char *factoryset_file = "/etc/nvr/factoryset.json";
        const char *tmp_dir = "/tmp/nvr";
        const char *jpeg_file = "/tmp/video.jpg";

        auto config = nvr::config(config_file, factoryset_file);

        led_manager->set_green_board(led_board_green);
        led_manager->set_red_board(led_board_red);
        
        pipeline->set_video_src(prepare_video_src(config));
        pipeline->set_video_sink(prepare_video_sink(tmp_dir, config));
        pipeline->set_jpeg_sink(prepare_jpeg_sink(tmp_dir, config));

        data.jpeg_file = jpeg_file;
        data.done_dir = prepare_done_directory(tmp_dir);
        if (data.done_dir.empty())
        {
            std::exit(-1);
        }

        use_sensor = config.use_sensor();
    }

    if (alarm_out_a->open()) {
        SPDLOG_ERROR("Failed to open alarm_out_a.");
        exit(-1);
    }

    if (alarm_out_b->open()) {
        SPDLOG_ERROR("Failed to open alarm_out_b.");
        exit(-1);
    }


#ifdef NVR_DEBUG_POWER
    if (tmp_out1->open()) {
        SPDLOG_ERROR("Failed to open tmp_out1.");
        exit(-1);
    }

    // if (tmp_out2->open()) {
    //     SPDLOG_ERROR("Failed to open tmp_out2.");
    //     exit(-1);
    // }
#endif
    std::shared_ptr<nvr::sd_manager> sd_manager = std::make_shared<nvr::sd_manager>(
        "/dev/mmcblk1p1",
        "/mnt/sd",
        "/mnt/sd/.nrs_video_data",
        "/mnt/sd/nvr",
        "/usr/bin/nvr",
        led_manager,
        logger
    );

    if (power_monitor->open()) {
        SPDLOG_ERROR("Failed to open power_monitor");
        exit(-1);
    }

    video_writer = std::make_shared<nvr::video_writer>(data.done_dir.c_str(), "/mnt/sd");
    video_writer->set_led_manager(led_manager);
    video_writer->set_sd_manager(sd_manager);
    video_writer->set_logger(logger);
#ifdef NVR_DEBUG_POWER
    video_writer->tmp_out2_ = tmp_out2;
#endif
	SPDLOG_INFO("test_1");
    reset_monitor->set_led_manager(led_manager);
    reset_monitor->set_logger(logger);
    if (reset_monitor->open()) {
        SPDLOG_ERROR("Failed to open reset_monitor.");
        exit(-1);
    }

    logger->write("L 起動 %s", (use_sensor) ? "センサー連動モード" : "連続モード");

    data.pipeline = pipeline;
    data.video_writer = video_writer;
    data.led_manager = led_manager.get();
    data.sd_manager = sd_manager.get();
    data.reset_monitor = reset_monitor.get();
    data.power_monitor = power_monitor.get();
    data.logger = logger;
#ifdef NVR_DEBUG_POWER
    data.tmp_out1 = tmp_out1.get();
#endif
    data.bus_watch_id = add_bus_watch(pipeline, &data);
    if (!data.bus_watch_id) {
        SPDLOG_ERROR("Failed to wach bus.");
        exit(-1);
    }

    data.timer1_id = g_timeout_add_full(
        G_PRIORITY_HIGH,
        1000,
        G_SOURCE_FUNC(timer1_cb),
        &data,
        nullptr);
    if (!data.timer1_id)
    {
        SPDLOG_ERROR("Failed to add timer1.");
        exit(-1);
    }

    data.timer2_id = g_timeout_add_full(
        G_PRIORITY_DEFAULT,
        50,
        G_SOURCE_FUNC(timer2_cb),
        &data,
        nullptr
    );
    if (!data.timer2_id) {
        SPDLOG_ERROR("Failed to add timer2.");
        exit(-1);
    }

#ifdef G_OS_UNIX
    data.signal_int_id = g_unix_signal_add(SIGINT, G_SOURCE_FUNC(signal_intr_cb), &data);
    data.signal_term_id = g_unix_signal_add(SIGTERM, G_SOURCE_FUNC(signal_term_cb), &data);
    data.signal_user1_id = g_unix_signal_add(SIGUSR1, G_SOURCE_FUNC(signal_user1_cb), &data);
    data.signal_user2_id = g_unix_signal_add(SIGUSR2, G_SOURCE_FUNC(signal_user2_cb), &data);
#endif

    std::thread power_monitor_thread;
    std::thread video_writer_thread;

	SPDLOG_INFO("test_4");

    power_monitor_thread = std::thread(power_monitor_process, &data);
    if (wait_power_pin(power_monitor, &data)) {
        goto END;
    }

    {
        struct sched_param param;
        param.sched_priority = std::max(sched_get_priority_max(SCHED_FIFO) / 3, sched_get_priority_min(SCHED_FIFO));
        if (pthread_setschedparam(power_monitor_thread.native_handle(), SCHED_FIFO, &param) != 0) {
            SPDLOG_WARN("Failed to power_monitor_thread scheduler.");
        }
    }


	SPDLOG_INFO("test_5");
    SPDLOG_DEBUG("Start video writer.");
    video_writer_thread = std::thread(video_writer->process());

    // while(!data.interrupted.load(std::memory_order_relaxed)) {
        if (!use_sensor) {
            led_manager->set_status(nvr::led_manager::state_recording);
        }
        SPDLOG_DEBUG("Start pipeline.");
        SPDLOG_INFO("test_6");
        if (pipeline->start()) {
            data.main_loop = g_main_loop_new(nullptr, FALSE);
            SPDLOG_INFO("Start main loop.");
            {
                time_t t = time(nullptr);
                data.jpeg_time.store(t, std::memory_order_relaxed);
            }
            g_main_loop_run(data.main_loop);
            SPDLOG_INFO("Main loop is stopped.");
            g_main_loop_unref(data.main_loop);
            pipeline->stop();
        }
        if (!use_sensor) {
            led_manager->clear_status(nvr::led_manager::state_recording);
            led_manager->update_led();
        }

        // if (!data.interrupted.load(std::memory_order_relaxed)) {
        //     std::this_thread::sleep_for(std::chrono::milliseconds(500));
        // }
    // }

END:
    video_writer->stop();
    data.interrupted.store(true, std::memory_order_relaxed);

    if (video_writer_thread.joinable()) {
        video_writer_thread.join();
    }


    if (power_monitor_thread.joinable()) {
        power_monitor_thread.join();
    }

    if (data.bus_watch_id)
    {
        g_source_remove(data.bus_watch_id);
    }

    if (data.timer2_id)
    {
        g_source_remove(data.timer2_id);
    }

    if (data.timer1_id)
    {
        g_source_remove(data.timer1_id);
    }

#ifdef G_OS_UNIX
    if (data.signal_int_id)
    {
        g_source_remove(data.signal_int_id);
    }
    if (data.signal_term_id)
    {
        g_source_remove(data.signal_term_id);
    }
    if (data.signal_user1_id)
    {
        g_source_remove(data.signal_user1_id);
    }
    if (data.signal_user2_id)
    {
        g_source_remove(data.signal_user2_id);
    }
#endif

    unlink_pid_file();

    led_board_green->write_value(true);
    led_board_red->write_value(true);

    if (data.do_reboot) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        do_reboot();
    }
    std::exit(0);
}



