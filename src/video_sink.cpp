#include <cmath>
#include "pipeline.hpp"
#include "logging.hpp"

namespace nvr
{
    static gchararray video_sink_format_location_full_cb(
        GstElement * /* splitmux */,
        guint /* fragment_id */,
        GstSample * /* first_sample */,
        gpointer udata)
    {
        video_sink *sink = static_cast<video_sink *>(udata);
        std::time_t now = std::time(nullptr);
        const std::tm *tm = localtime(&now);

        SPDLOG_DEBUG("format_location_full_cb");
        return g_strdup_printf(
            "%s/%04d-%02d-%02d-%02d-%02d-%02d.mp4",
            sink->location_dir().c_str(),
            tm->tm_year + 1900,
            tm->tm_mon + 1,
            tm->tm_mday,
            tm->tm_hour,
            tm->tm_min,
            tm->tm_sec);
    }

    static caps_filter_element make_video_rate_caps(GstBin *pipeline, int framerate_n, int framerate_d)
    {
        return caps_filter_element(
            pipeline,
            "video_rate_caps",
            "video/x-raw",
            "framerate", GST_TYPE_FRACTION, framerate_n, framerate_d
        );
    }

    video_sink::video_sink(
        const char *location_dir,
        double framerate,
        unsigned int bitrate
    ) : queue_(),
        video_sink_(),
        format_location_full_handler_(0),
        location_dir_(location_dir),
        bitrate_(bitrate)
    {
        if (framerate == 29.97) {
            framerate_n_ = 30000;
            framerate_d_ = 1001;
        } else {
            gst_util_double_to_fraction(framerate, &framerate_n_, &framerate_d_);
        }
        framerate_ = framerate;
        SPDLOG_DEBUG("Video rate: {}/{}", framerate_n_, framerate_d_);
    }

    video_sink::~video_sink()
    {
        reset();
    }

    void video_sink::reset()
    {
        if (format_location_full_handler_) {
            if (video_sink_) {
                video_sink_.disconnect_signal(format_location_full_handler_);
            }
            format_location_full_handler_ = 0;
        }

        if (video_sink_) {
            video_sink_.reset();
        }
    }

    gboolean video_sink::setup(GstBin *pipeline)
    {
        gboolean ret = TRUE;
        element queue;
        element video_encoder;
        element video_rate;
        element rate_caps_filter;
        element h264_caps_filter;
        element h264_parser;
        element video_sink;

        try {
            for (const std::filesystem::directory_entry &entry : std::filesystem::directory_iterator(location_dir_))
            {
                if (entry.is_regular_file())
                {
                    unlink(entry.path().c_str());
                }
            }
        } catch (std::exception &err) {
            // Do nothing
        }

        queue = element(pipeline, "queue", "queueu_video");
        if (!queue)
        {
            SPDLOG_DEBUG("Failed to make video queue.");
            ret = FALSE;
        } else {
            queue.set(
                "max-size-buffers", 1,
                "max-size-bytes", 0,
                "max-size-time", 0
            );
        }

        video_rate = element(pipeline, "videorate", "video_rate");
        if (!video_rate)
        {
            SPDLOG_DEBUG("Failed to make video rate.");
            ret = FALSE;
        }

        rate_caps_filter = make_video_rate_caps(pipeline, framerate_n_, framerate_d_);
        if (!rate_caps_filter)
        {
            SPDLOG_ERROR("Failed to make video rate caps filter.");
            ret = FALSE;
        }

        video_encoder = make_encoder(pipeline, "video_encoder");
        if (!video_encoder)
        {
            SPDLOG_ERROR("Failed to make video encoder.");
            ret = FALSE;
        }

        h264_caps_filter = caps_filter_element(
            pipeline,
            "h264_caps_filter",
            "video/x-h264",
            "profile", G_TYPE_STRING, "high",
            "level", G_TYPE_STRING, "5.1"
        );
        // h264_caps_filter = caps_filter_element(
        //     pipeline,
        //     "h264_caps_filter",
        //     "video/x-h264",
        //     "profile", G_TYPE_STRING, "baseline",
        //     "level", G_TYPE_STRING, "4.1"
        // );

        if (!h264_caps_filter)
        {
            SPDLOG_ERROR("Failed to make h264 caps filter.");
            ret = FALSE;
        }

        h264_parser = element(pipeline, "h264parse", "h264_parser");
        if (!h264_parser)
        {
            SPDLOG_ERROR("Failed to make h264 parser.");
            ret = FALSE;
        }

        video_sink = element(pipeline, "splitmuxsink", "video_file_sink");
        if (!video_sink)
        {
            SPDLOG_DEBUG("Failed to make video sink.");
            ret = FALSE;
        } else {
            video_sink.set(
                "max-size-time", 600000000000,
                "async-finalize", TRUE,
                "async-handling", TRUE
            );

            format_location_full_handler_ = video_sink.connect_signal(
                "format-location-full",
                G_CALLBACK(video_sink_format_location_full_cb),
                this
            );

            if (!format_location_full_handler_)
            {
                SPDLOG_ERROR("Failed to connect to format-location-full signal.");
                ret = FALSE;
            }
        }

        if (!ret) {
            return ret;
        }

        queue_ = std::move(queue);
        video_sink_ = std::move(video_sink);

        ret = gst_element_link_many(
            static_cast<GstElement*>(queue_),
            static_cast<GstElement*>(video_rate),
            static_cast<GstElement*>(rate_caps_filter),
            static_cast<GstElement*>(video_encoder),
            static_cast<GstElement*>(h264_caps_filter),
            static_cast<GstElement*>(h264_parser),
            static_cast<GstElement*>(video_sink_),
            nullptr
            );

        if (!ret) {
            SPDLOG_ERROR("Failed to link video sink elements");
        }
        return ret;
    }

    element video_sink::make_encoder(GstBin *pipeline, const char *name)
    {
        auto video_encoder = element(pipeline, "omxh264enc", name);
        if (!video_encoder) {
            SPDLOG_ERROR("Failed to omxh264enc element.");
            return {};
        }

        // "no-copy", TRUE,
        // "use-dmabuf", TRUE
        int interval_intraframes = std::ceil(framerate_) * 2;
        SPDLOG_DEBUG("intrerval_intrafames: {}", interval_intraframes);
        video_encoder.set(
            "target-bitrate", bitrate_,
            "control-rate", 1,
            "interval-intraframes", interval_intraframes,
            // "periodicity-idr", 45,
            "loop-filter-mode", 0,
            // "scan-type", 0,
            // "ref-frames", 4,
            "entropy-mode", 1
        );

        return video_encoder;
    }

    void video_sink::split_video_file() const
    {
        if (video_sink_) {
            SPDLOG_DEBUG("send split-now signal");
            video_sink_.emit_signal("split-now");
        }
    }

    GstPadLinkReturn video_sink::link(GstPad *src)
    {
        GstPad *sink = queue_.get_static_pad("sink");
        GstPadLinkReturn result = gst_pad_link(src, sink);
        gst_object_unref(sink);

        return result;
    }
}