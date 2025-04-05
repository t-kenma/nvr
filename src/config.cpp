#include "config.hpp"
#include <iostream>
#include <fstream>

#include "logging.hpp"

namespace nvr {
    config::config(const char *config_file, const char *factoryset_file)
        : recording_mode_(recording_mode::normal),
          src_type_(src_type::camera),
          width_(640),
          framerate_(30.0),
          bitrate_(1000),
          led_type_(led_type::sensor),
          use_wifi_(false),
          wifi_dbm_(800)
    {
        load_config(config_file);
        load_factoryset(factoryset_file);
    }

    void config::load_config(const char *config_file)
    {
        try {
            std::ifstream in(config_file);
            auto json = nlohmann::json::parse(in);

            if (json["recording_mode"].get<std::string>() == "sensor") {
                recording_mode_ = recording_mode::sensor;
            } else {
                recording_mode_ = recording_mode::normal;
            }

            if (json["src_type"].get<std::string>() == "rtsp") {
                src_type_ = src_type::rtsp;
            } else {
                src_type_ = src_type::camera;
            }

            width_ = (json["width"].get<int>() == 640) ? 640 : 320;
            framerate_ = json["framerate"].get<double>();
            bitrate_ = json["bitrate"].get<unsigned int>();

            if (src_type_ == src_type::rtsp) {
                rtsp_url_ = json["rtsp_url"].get<std::string>();
                rtsp_user_ = json.contains("rtsp_user") ? json["rtsp_user"].get<std::string>() : std::string();
                rtsp_password_ = json.contains("rtsp_password") ? json["rtsp_password"].get<std::string>() : std::string();
            } else {
                rtsp_url_ = std::string();
                rtsp_user_ = std::string();
                rtsp_password_ = std::string();
            }
        } catch (std::exception &err) {
            SPDLOG_ERROR("Failed to load {}: {}", config_file, err.what());
        }
    }

    void config::load_factoryset(const char *factoryset_file)
    {
        try {
            std::ifstream in(factoryset_file);
            auto json = nlohmann::json::parse(in);

            led_type_ = (json["led"].get<std::string>() == "board")
                ? led_type::board
                : led_type::sensor;
            SPDLOG_DEBUG("led is {}", json["led"].get<std::string>());

            use_wifi_ = (json["use_wifi"].get<int>()) ? true : false;
            SPDLOG_DEBUG("use_wifi is {}", use_wifi_);

            try {
                wifi_dbm_ = json["wifi_dbm"].get<int>();
            } catch (...) {
                wifi_dbm_ = 800;
            }
        } catch (std::exception &err) {
            SPDLOG_ERROR("Failed to load {}: {}", factoryset_file, err.what());
        }
    }
}