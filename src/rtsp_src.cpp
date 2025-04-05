#include "pipeline.hpp"
#include "logging.hpp"

namespace nvr
{
    static void rtsp_src_pad_added_cb(GstElement *element, GstPad *pad, gpointer udata)
    {
        GstElement *sink = static_cast<GstElement *>(udata);
        gchar *name = gst_pad_get_name(pad);

        if (!name) {
            return;
        }

        if (gst_pad_has_current_caps(pad)) {
            auto caps = gst_pad_get_current_caps(pad);

            if (caps) {
                auto str = gst_caps_to_string(caps);

                if (str) {
                    SPDLOG_DEBUG("rtspsrc:{} caps:{}", name, str);
                    g_free(str);
                }
            }
        }

        if (!gst_element_link_pads(element, name, sink, "sink"))
        {
            SPDLOG_ERROR("Failed to link rtspsrc:{} element.", name);
        }
        else
        {
            SPDLOG_DEBUG("rtspsrc:{} is linked to {}:sink", name, gst_element_get_name(sink));
        }
        g_free(name);
    }

    static void decode_bin_pad_added_cb(GstElement *element, GstPad *pad, gpointer udata)
    {
        GstElement *sink = static_cast<GstElement *>(udata);
        gchar *name = gst_pad_get_name(pad);

        if (!name) {
            return;
        }

        if (gst_pad_has_current_caps(pad)) {
            auto caps = gst_pad_get_current_caps(pad);

            if (caps) {
                auto str = gst_caps_to_string(caps);

                if (str) {
                    SPDLOG_DEBUG("decodebin:{} caps:{}", name, str);
                    g_free(str);
                }
            }
        }

        if (!gst_element_link_pads(element, name, sink, "sink"))
        {
            SPDLOG_ERROR("Failed to link decodebin:{} to {}:sink.", name, gst_element_get_name(sink));
        }
        else
        {
            SPDLOG_DEBUG("decodebin:{} is linked to {}:sink.", name, gst_element_get_name(sink));
        }
        g_free(name);
    }

    rtsp_src::~rtsp_src()
    {
        reset();
    }

    void rtsp_src::reset()
    {
        if (rtsp_src_pad_added_handler_ && rtsp_src_)
        {
            if (rtsp_src_) {
                rtsp_src_.disconnect_signal(rtsp_src_pad_added_handler_);
            }
            rtsp_src_pad_added_handler_ = 0;
        }

        if (rtsp_src_) {
            rtsp_src_.reset();
        }

        if (decode_bin_pad_added_handler_)
        {
            if (decode_bin_) {
                decode_bin_.disconnect_signal(decode_bin_pad_added_handler_);
            }
            decode_bin_pad_added_handler_ = 0;
        }

        if (decode_bin_) {
            decode_bin_.reset();
        }

        video_src::reset();
    }

    element rtsp_src::make_src_element(GstBin *pipeline)
    {
        element rtsp_src;
        element decode_bin;
        element video_convert;
        element caps_filter;

        rtsp_src = element(pipeline, "rtspsrc", "rtsp_src");
        if (!rtsp_src)
        {
            SPDLOG_ERROR("Failed to make rtsp_src element.");
            return {};
        }

        rtsp_src.set("location", url_.c_str());

        if (!user_id_.empty())
        {
            rtsp_src.set("user-id", user_id_.c_str());
        }

        if (!password_.empty())
        {
            rtsp_src.set("user-pw", password_.c_str());
        }

        decode_bin = element(pipeline, "decodebin3", "rtsp_decodebin");
        if (!decode_bin)
        {
            SPDLOG_ERROR("Failed to make rtsp_decodebin element.");
            return {};
        }

        video_convert = element(pipeline, "videoconvert", "rtsp_convert");
        if (!video_convert)
        {
            SPDLOG_ERROR("Failed to make rtsp_convert element.");
            return {};
        }

        caps_filter = caps_filter_element(
            pipeline,
            "rtsp_src_caps",
            "video/x-raw",
            "format", G_TYPE_STRING, "UYVY");
        if (!caps_filter)
        {
            SPDLOG_ERROR("Failed to make rtsp_src_caps element.");
            return {};
        }

        if (!gst_element_link(
                static_cast<GstElement *>(video_convert),
                static_cast<GstElement *>(caps_filter)))
        {
            SPDLOG_ERROR("Failed to link rtsp_convert element.");
            return {};
        }

        rtsp_src_ = std::move(rtsp_src);
        decode_bin_ = std::move(decode_bin);
        video_convert_ = std::move(video_convert);

        rtsp_src_pad_added_handler_ = rtsp_src_.connect_signal(
            "pad-added",
            G_CALLBACK(rtsp_src_pad_added_cb),
            static_cast<GstElement *>(decode_bin_));
        if (!rtsp_src_pad_added_handler_)
        {
            SPDLOG_ERROR("Failed to connect rtsp_src:pad-added signal.");
            return {};
        }

        decode_bin_pad_added_handler_ = decode_bin_.connect_signal(
            "pad-added",
            G_CALLBACK(decode_bin_pad_added_cb),
            static_cast<GstElement *>(video_convert_));
        if (!decode_bin_pad_added_handler_)
        {
            SPDLOG_ERROR("Failed to connect rtsp_decodebin:pad-added signal.");
        }

        return caps_filter;
    }
}