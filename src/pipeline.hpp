#ifndef _NRV_PIPELINE_H_
#define _NRV_PIPELINE_H_

#include <string>
#include <filesystem>
#include <atomic>
#include <gst/gst.h>
#include "element.hpp"

namespace nvr
{
    class video_src
    {
    public:
        explicit video_src(int width, int height) : caps_filter_(), width_(width), height_(height){};
        video_src() = delete;
        video_src(const video_src &) = delete;
        video_src(video_src &&) = delete;
        virtual ~video_src();

        constexpr int width() const noexcept { return width_; }
        constexpr int height() const noexcept { return height_; }
        constexpr GstElement* sink() const noexcept { return static_cast<GstElement*>(caps_filter_); }

        gboolean setup(GstBin *pipeline);
        virtual void reset();
    protected:
        virtual element make_src_element(GstBin *pipeline) = 0;
        virtual gboolean link_src_element(const element& src, const element& sink);

    private:
        element caps_filter_;
        int width_;
        int height_;
    };

    class v4l2_src : public video_src
    {
    public:
        explicit v4l2_src(int width, int height): video_src(width, height) {}
        v4l2_src() = delete;
        v4l2_src(const v4l2_src&) = delete;
        v4l2_src(v4l2_src&&) = delete;

    protected:
        virtual element make_src_element(GstBin *pipeline) override;
    };

    class rtsp_src : public video_src
    {
    public:
        explicit rtsp_src(
            const std::string &url,
            const std::string &user_id,
            const std::string &password,
            int width, int height)
            : video_src(width, height),
              url_(url),
              user_id_(user_id),
              password_(password),
              rtsp_src_(),
              decode_bin_(),
              video_convert_(),
              rtsp_src_pad_added_handler_(0),
              decode_bin_pad_added_handler_(0)
        {}
        rtsp_src() = delete;
        rtsp_src(const rtsp_src &) = delete;
        rtsp_src(rtsp_src &&) = delete;
        virtual ~rtsp_src();

        constexpr GstElement *src() const noexcept { return static_cast<GstElement*>(rtsp_src_); }
        constexpr GstElement *decode_bin() const noexcept { return static_cast<GstElement*>(decode_bin_); }

        virtual void reset() override;

    protected:
        virtual element make_src_element(GstBin *pipeline) override;

    private:
        std::string url_;
        std::string user_id_;
        std::string password_;
        element rtsp_src_;
        element decode_bin_;
        element video_convert_;
        gulong rtsp_src_pad_added_handler_;
        gulong decode_bin_pad_added_handler_;
    };

    class jpeg_sink
    {
    public:
        jpeg_sink(const char *location) : location_(location), queue_(){};
        jpeg_sink(const jpeg_sink &) = delete;
        jpeg_sink(jpeg_sink &&) = delete;
        ~jpeg_sink() = default;

        gboolean setup(GstBin *pipeline);
        void reset();
        GstPadLinkReturn link(GstPad *src);

    private:
        std::string location_;
        element queue_;
    };

    class video_sink
    {
    public:
        explicit video_sink(
            const char *location_dir,
            double framerate,
            unsigned int bitrate
        );
        video_sink() = delete;
        video_sink(const video_sink &) = delete;
        video_sink(const video_sink &&) = delete;
        ~video_sink();

        gboolean setup(GstBin *pipeline);
        void reset();

        GstPadLinkReturn link(GstPad *src);
        void split_video_file() const;

        constexpr const std::filesystem::path &location_dir() const { return location_dir_; }

    protected:
        element make_encoder(GstBin *pipeline, const char *name);

    private:
        element queue_;
        element video_sink_;
        gulong format_location_full_handler_;
        std::filesystem::path location_dir_;
        gint framerate_n_;
        gint framerate_d_;
        double framerate_;
        unsigned int bitrate_;
    };

    class pipeline
    {
    public:
        pipeline();
        pipeline(const pipeline &) = delete;
        pipeline(pipeline &&) = delete;
        ~pipeline();

        inline GstBus *bus() const noexcept { return gst_element_get_bus(pipeline_); }

        bool start();
        bool stop();

        gboolean setup();
        void reset();

        explicit operator GstBin* () const noexcept { return GST_BIN(pipeline_); }
        constexpr explicit operator GstElement* () const noexcept { return pipeline_; }

        inline void set_video_src(nvr::video_src* video_src)
        {
            video_src_ = std::unique_ptr<nvr::video_src>(video_src);
        }

        inline void set_video_sink(nvr::video_sink* video_sink)
        {
            video_sink_ = std::unique_ptr<nvr::video_sink>(video_sink);
        }

        inline void set_jpeg_sink(nvr::jpeg_sink* jpeg_sink)
        {
            jpeg_sink_ = std::unique_ptr<nvr::jpeg_sink>(jpeg_sink);
        }

        inline void split_video_file() const
        {
            video_sink_->split_video_file();
        }

        inline gboolean send_eos_event()
        {
            if (pipeline_) {
                return gst_element_send_event(pipeline_, gst_event_new_eos());
            }
            return FALSE;
        }

        gboolean post_application_message(GstStructure *message);

        inline bool is_running() const {
            GstState state = GST_STATE_VOID_PENDING;
            gst_element_get_state(pipeline_, &state, nullptr, 1);

            return (state == GST_STATE_PLAYING) ? true : false;
        }
    private:
        gboolean link_src();
        gboolean link_video_sink();
        gboolean link_jpeg_sink();

        std::unique_ptr<nvr::video_src> video_src_;
        std::unique_ptr<nvr::video_sink> video_sink_;
        std::unique_ptr<nvr::jpeg_sink> jpeg_sink_;

        GstElement *pipeline_;
        GstElement *tee_;
        GstPad *tee_video_pad_;
        GstPad *tee_jpeg_pad_;

        std::atomic<bool> running_;
    };
}

#endif