#include "pipeline.hpp"
#include "logging.hpp"

#define USE_VIDEO_SCALE
namespace nvr {
    gboolean video_src::setup(GstBin *pipeline)
    {
        gboolean ret = TRUE;
        element src;
        element video_scale;
        element video_scale_caps_filter;
#ifdef USE_VIDEO_SCALE
        element video_convert;
        element video_convert_caps_filter;
#endif
        src = make_src_element(pipeline);
        if (!src) {
            SPDLOG_ERROR("Failed to make src element.");
            ret = FALSE;
        }
#ifdef USE_VIDEO_SCALE
        video_scale = element(pipeline, "videoscale", "video_scale");
#else
        video_scale = element(pipeline, "vspmfilter", "video_scale");
#endif
        if (!video_scale) {
            SPDLOG_ERROR("Failed to make video scale element.");
            ret = FALSE;
        } else {
#ifndef USE_VIDEO_SCALE
            video_scale.set("dmabuf-use", TRUE);
#endif
        }

#ifdef USE_VIDEO_SCALE
        video_convert = element(pipeline, "videoconvert", "video_convert");
        if (!video_convert) {
            SPDLOG_ERROR("Failed to make video convert element.");
            ret = FALSE;
        }

        video_scale_caps_filter = caps_filter_element(
            pipeline,
            "video_scale_caps",
            "video/x-raw",
            "width", G_TYPE_INT, width_,
            "height", G_TYPE_INT, height_
        );

        video_convert_caps_filter = caps_filter_element(
            pipeline,
            "video_convert_caps",
            "video/x-raw",
            "format", G_TYPE_STRING, "UYVY"//"I420"
        );
#else
        video_scale_caps_filter = caps_filter_element(
            pipeline,
            "video_scale_caps",
            "video/x-raw",
            "format", G_TYPE_STRING, "NV12",
            "width", G_TYPE_INT, width_,
            "height", G_TYPE_INT, height_
        );
#endif
        if (!video_scale_caps_filter) {
            SPDLOG_ERROR("Failed to make video scale caps filter element.");
            ret = FALSE;
        }

#ifdef USE_VIDEO_SCALE
        if (!video_convert_caps_filter) {
            SPDLOG_ERROR("Failed to make video convert caps filter element.");
            ret = FALSE;
        }
#endif

        if (!ret) {
            return ret;
        }

#ifdef USE_VIDEO_SCALE
        ret = gst_element_link_many(
            static_cast<GstElement*>(video_convert),
            static_cast<GstElement*>(video_convert_caps_filter),
            static_cast<GstElement*>(video_scale),
            static_cast<GstElement*>(video_scale_caps_filter),
            nullptr
        );
#else
        ret = gst_element_link(
            static_cast<GstElement*>(video_scale),
            static_cast<GstElement*>(video_scale_caps_filter)
        );
#endif
        if (!ret) {
            SPDLOG_ERROR("Failed to link video scale element..");
            return ret;
        }

#ifdef USE_VIDEO_SCALE
        ret = link_src_element(src, video_convert);
        if (!ret) {
            SPDLOG_ERROR("Failed to link src element.");
            return ret;
        }
#else
        ret = link_src_element(src, video_scale);
        if (!ret) {
            SPDLOG_ERROR("Failed to link src element.");
            return ret;
        }
#endif
        caps_filter_ = std::move(video_scale_caps_filter);

        return ret;
    }

    video_src::~video_src()
    {
        reset();
    }

    void video_src::reset()
    {
        caps_filter_.reset();
    }

    gboolean video_src::link_src_element(const element& src, const element& sink)
    {
        return gst_element_link(
            static_cast<GstElement*>(src),
            static_cast<GstElement*>(sink)
        );
    }
}