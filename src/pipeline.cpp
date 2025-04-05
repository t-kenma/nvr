#include "pipeline.hpp"
#include "logging.hpp"

namespace nvr
{
    pipeline::pipeline()
        : tee_(nullptr),
          tee_video_pad_(nullptr),
          tee_jpeg_pad_(nullptr),
          running_(false)
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
        if (tee_video_pad_)
        {
            gst_element_release_request_pad(tee_, tee_video_pad_);
            gst_object_unref(tee_video_pad_);
            tee_video_pad_ = nullptr;
        }

        if (tee_jpeg_pad_)
        {
            gst_element_release_request_pad(tee_, tee_jpeg_pad_);
            gst_object_unref(tee_jpeg_pad_);
            tee_jpeg_pad_ = nullptr;
        }

        if (tee_) {
            gst_object_unref(tee_);
            tee_ = nullptr;
        }

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

    gboolean pipeline::setup()
    {
        GstElement *tee = nullptr;

        // pipeline_ = gst_pipeline_new("nrs-recorder-pipeline");
        // if (!pipeline_) {
        //     SPDLOG_ERROR("Failed to create pipeline.");
        //     return FALSE;
        // }

        // gst_pipeline_set_auto_flush_bus(static_cast<GstPipeline*>(pipeline_), TRUE);

        tee = gst_element_factory_make("tee", "tee");
        if (!tee) {
            SPDLOG_ERROR("Failed to make tee element.");
            return FALSE;
        }

        gst_bin_add(GST_BIN(pipeline_), tee);
        tee_ = static_cast<GstElement *>(g_object_ref(tee));

#if GST_CHECK_VERSION(1,20,0)
        tee_video_pad_ = gst_element_request_pad_simple(tee_, "src_%u");
#else
        tee_video_pad_ = gst_element_get_request_pad(tee_, "src_%u");
#endif
        if (!tee_video_pad_) {
            SPDLOG_ERROR("Failed to request tee video src pad.");
            return FALSE;
        }
    
#if GST_CHECK_VERSION(1,20,0)
        tee_jpeg_pad_ = gst_element_request_pad_simple(tee_, "src_%u");
#else
        tee_jpeg_pad_ = gst_element_get_request_pad(tee_, "src_%u");
#endif
        if (!tee_jpeg_pad_) {
            SPDLOG_ERROR("Failed to request tee jpeg src pad.");
            return FALSE;
        }

        return TRUE;
    }

    gboolean pipeline::link_src() {
        return gst_element_link(video_src_->sink(), tee_);
    }

    gboolean pipeline::link_video_sink()
    {
        GstPadLinkReturn result = video_sink_->link(tee_video_pad_);
        return (result == GST_PAD_LINK_OK) ? TRUE : FALSE;
    }

    gboolean pipeline::link_jpeg_sink()
    {
        GstPadLinkReturn result = jpeg_sink_->link(tee_jpeg_pad_);
        return (result == GST_PAD_LINK_OK) ? TRUE : FALSE;
    }

    bool pipeline::start()
    {
        GstStateChangeReturn result;

        if (!setup()) {
            return false;
        }

        if (!video_src_->setup(GST_BIN(pipeline_))) {
            return false;
        }

        if (!video_sink_->setup(GST_BIN(pipeline_))) {
            return false;
        }

        if (!jpeg_sink_->setup(GST_BIN(pipeline_))) {
            return false;
        }

        if (!link_src()) {
            SPDLOG_ERROR("Failed to link src.");
            return false;
        }

        if (!link_video_sink()) {
            SPDLOG_ERROR("Failed to link video sink.");
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
            video_sink_->reset();
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