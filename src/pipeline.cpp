#include "pipeline.hpp"
#include "logging.hpp"

namespace nvr
{
    pipeline::pipeline()
          :running_(false)
    {
        pipeline_ = gst_pipeline_new("nrs-recorder-pipeline");
    }

    pipeline::~pipeline()
    {
        reset();

        if (pipeline_)
        {
            gst_object_unref(pipeline_);
            pipeline_ = nullptr;
        }
    }

    void pipeline::reset()
    {
        if (pipeline_)
        {
            GstIterator *itr;
        	GstElement *elem;
        	GValue val = G_VALUE_INIT;
        	gboolean done = FALSE;

            itr = gst_bin_iterate_elements(GST_BIN(pipeline_));
	        while (!done) {
		        switch (gst_iterator_next(itr, &val)) {
		        case GST_ITERATOR_OK:
			        elem = static_cast<GstElement*>(g_value_get_object(&val));
                    gst_bin_remove(GST_BIN(pipeline_), elem);
                    // if (gst_bin_remove(GST_BIN(pipeline_), elem)) {
                    //     SPDLOG_DEBUG("Element {} is removed", gst_element_get_name(elem));
                    // } else {
                    //     SPDLOG_DEBUG("Failed to remove element {}", gst_element_get_name(elem));
                    // }
			        g_value_reset(&val);
			        break;
                case GST_ITERATOR_RESYNC:
                    gst_iterator_resync (itr);
                    break;
		        default:
			        done = TRUE;
			        break;
		        }
	        }
        }
    }


    gboolean pipeline::link_jpeg_sink()
    {
        gboolean result = jpeg_sink_->link(video_src_->sink());
        return (result) ? TRUE : FALSE;
    }

    bool pipeline::start()
    {
        GstStateChangeReturn result;

        if (!video_src_->setup(GST_BIN(pipeline_))) {
            return false;
        }

        if (!jpeg_sink_->setup(GST_BIN(pipeline_))) {
            return false;
        }

        if (!link_jpeg_sink()) {
            SPDLOG_ERROR("Failed to link jpeg sink.");
            return false;
        }

        result = gst_element_set_state(pipeline_, GST_STATE_PLAYING);
        SPDLOG_DEBUG("Set playing result: {}", result);
        // while (result == GST_STATE_CHANGE_ASYNC) {
        //     result = gst_element_get_state(pipeline_, nullptr, nullptr, GST_CLOCK_TIME_NONE);
        //     SPDLOG_DEBUG("Set playing result: {}", result);
        // }

        if (result != GST_STATE_CHANGE_FAILURE) {
            // running_.store(true, std::memory_order_relaxed);
            return true;
        }

        SPDLOG_ERROR("Failed to start pipeline: {}", result);
        return false;
    }

    bool pipeline::stop()
    {
        GstStateChangeReturn result = gst_element_set_state(pipeline_, GST_STATE_NULL);
        // while (result == GST_STATE_CHANGE_ASYNC) {
        //     result = gst_element_get_state(pipeline_, nullptr, nullptr, GST_CLOCK_TIME_NONE);
        // }

        if (result != GST_STATE_CHANGE_FAILURE) {
            // running_.store(false, std::memory_order_relaxed);

            jpeg_sink_->reset();
            video_src_->reset();
            reset();

            return true;
        }

        SPDLOG_ERROR("Failed to stop pipeline: {}", result);
        return false;
    }


    gboolean pipeline::post_application_message(GstStructure *message)
    {
        return gst_element_post_message(
            pipeline_,
            gst_message_new_application(
                GST_OBJECT(pipeline_),
                message
            )
        );
    }
}
