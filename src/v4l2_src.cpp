#include "pipeline.hpp"
#include "logging.hpp"

namespace nvr
{
    element v4l2_src::make_src_element(GstBin *pipeline)
    {
        auto v4l2_src = element(pipeline, "v4l2src", "v4l2_src");
        if (!v4l2_src) {
            SPDLOG_ERROR("Failed to make v4l2src element.");
            return {};
        }

        auto src_caps_filter = caps_filter_element(
            pipeline,
            "v4l2_src_caps",
            "video/x-raw",
            "format", G_TYPE_STRING, "UYVY"
        );
        if (!src_caps_filter) {
            SPDLOG_ERROR("Failed to make v4l2src caps filter element.");
            return {};
        }

        auto video_crop = element(pipeline, "videocrop", "v4l2_crop");
        if (!video_crop) {
            SPDLOG_ERROR("Failed to make videocrop element.");
            return {};
        }
        video_crop.set(
            "left", 40,
            "right", 40,
            "top", 0,
            "bottom", 0
        );
        // video_crop.set(
        //     "left", 12,
        //     "right", 8,
        //     "top", 0,
        //     "bottom", 4
        // );

        if (!gst_element_link_many(
            static_cast<GstElement*>(v4l2_src),
            static_cast<GstElement*>(src_caps_filter),
            static_cast<GstElement*>(video_crop),
            nullptr
        )) {
            SPDLOG_ERROR("Failed to link elements.");
            return {};
        }

        return static_cast<element>(video_crop);
    }
}