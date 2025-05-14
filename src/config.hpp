#ifndef _NRV_CONFIG_HPP_
#define _NRV_CONFIG_HPP_

#include <memory>
#include <nlohmann/json.hpp>

namespace nvr
{
    class config
    {
    public:
        enum class src_type
        {
            camera = 1,
            rtsp = 2
        };

        enum class recording_mode
        {
            normal = 1,
            sensor = 2
        };

        enum class led_type
        {
            sensor = 1,
            board = 2
        };

        explicit config(const char *config_file, const char *factoryset_file);
        config() = delete;
        config(const config &) = delete;
        config(config &&) = delete;

        constexpr recording_mode video_recording_mode() const { return recording_mode_; }
        constexpr src_type video_src_type() const { return src_type_; }
        constexpr bool is_video_src_rtsp() const { return src_type_ == src_type::rtsp ? true : false; }
        constexpr int video_width() const { return width_; }
        constexpr int video_height() const { return (width_ == 640) ? 480 : 240; }
        constexpr double video_framerate() const { return (!is_video_src_rtsp() && framerate_ > 29.97) ? 29.97 : framerate_; }
        constexpr unsigned int video_bitrate() const { return bitrate_ * 1024; }
        constexpr const std::string &rtsp_url() const { return rtsp_url_; }
        constexpr const std::string &rtsp_user() const { return rtsp_user_; }
        constexpr const std::string &rtsp_password() const { return rtsp_password_; }
        constexpr bool use_sensor() const { 
            return (recording_mode_ == recording_mode::sensor) ? true : false; 
        }
        constexpr bool use_board_led() const {
            return (led_type_ == led_type::board) ? true : false;
        }
        constexpr bool use_wifi() const {
            return use_wifi_;
        }
        constexpr int wifi_dbm() const {
            return wifi_dbm_;
        }
    private:
        void load_config(const char *config_file);
        void load_factoryset(const char *factoryset_file);

        recording_mode recording_mode_;
        src_type src_type_;
        int width_;
        double framerate_;
        unsigned int bitrate_;
        std::string rtsp_url_;
        std::string rtsp_user_;
        std::string rtsp_password_;
        led_type led_type_;
        bool use_wifi_;
        int wifi_dbm_;
    };
}

#endif