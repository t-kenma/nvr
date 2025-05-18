#include "pipeline.hpp"
#include "logging.hpp"

namespace nvr
{
    gboolean jpeg_sink::setup(GstBin *pipeline)
    {
        gboolean ret = TRUE;
        element queue;
//        element jpeg_enc;
        element jpeg_file_sink;

        queue = element(pipeline, "queue", "queue_jpeg");
        if (!queue)
        {
            SPDLOG_ERROR("Failed to make queue_jpeg element.");
            ret = FALSE;
        } else {
            queue.set(
                "max-size-buffers", 1,
                "max-size-bytes", 0,
                "max-size-time", 0
            );
        }

//        jpeg_enc = element(pipeline, "jpegenc", "jpeg_enc");
//        if (!jpeg_enc)
//        {
//            SPDLOG_ERROR("Failed to make jpeg_enc element.");
//            ret = FALSE;
//        }

        jpeg_file_sink = element(pipeline, "multifilesink", "jpeg_file_sink");
        if (!jpeg_file_sink)
        {
            SPDLOG_ERROR("Failed to make jpeg_file_sink element.");
            ret = FALSE;
        } else {
            jpeg_file_sink.set(
                "post-messages", TRUE,
                "async", TRUE,
                "location", location_.c_str()
            );
        }

        if (!ret) {
            return ret;
        }

        queue_ = std::move(queue);

        ret = gst_element_link_many(
            static_cast<GstElement*>(queue_),
//            static_cast<GstElement*>(jpeg_enc),
            static_cast<GstElement*>(jpeg_file_sink),
            nullptr
        );
        if (!ret)
        {
            SPDLOG_ERROR("Failed to link jpeg_sink elements.");
        }

        return ret;
    }

    void jpeg_sink::reset()
    {
        queue_.reset();
    }

    gboolean jpeg_sink::link(GstElement *src)
    {
        return gst_element_link(src, static_cast<GstElement*>(queue_));
    }
}
