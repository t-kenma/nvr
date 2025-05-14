#ifndef __NVR_ELEMENT_HPP__
#define __NVR_ELEMENT_HPP__

#include <gst/gst.h>
#include "logging.hpp"

namespace nvr
{
    class element
    {
    public:
        constexpr element() noexcept : element_(nullptr) {}
        explicit element(GstBin *pipeline, const char *factoryname, const char *name) noexcept;
        element(const element &src) noexcept;
        constexpr explicit element(element &&src) noexcept : element_(src.element_)
        {
            src.element_ = nullptr;
        }
        ~element();

        element &operator=(GstElement *src) & noexcept;
        element &operator=(const element &src) & noexcept;
        element &operator=(element &&src) & noexcept;

        constexpr explicit operator bool() const noexcept { return element_; }
        constexpr bool operator!() const noexcept
        {
            return !static_cast<bool>(*this);
        }

        constexpr explicit operator GstElement* () const noexcept { return element_; }

        constexpr void reset() noexcept
        {
            if (element_)
            {
                gst_object_unref(element_);
                element_ = nullptr;
            }
        }

        template <typename... Args>
        void set(Args... args) noexcept
        {
            g_object_set(G_OBJECT(element_), args..., nullptr);
        }

        void emit_signal(const char *name) const noexcept
        {
            if (element_)
            {
                g_signal_emit_by_name(element_, name);
            }
        }

        GstPad *get_static_pad(const char *name) noexcept
        {
            if (element_)
            {
                return gst_element_get_static_pad(element_, name);
            }
            return nullptr;
        }

        gulong connect_signal(const char *name, GCallback callback, gpointer data) const noexcept
        {
            if (element_)
            {
                return g_signal_connect(element_, name, callback, data);
            }
            return 0;
        }

        void disconnect_signal(gulong handler) noexcept
        {
            if (element_)
            {
                g_signal_handler_disconnect(element_, handler);
            }
        }

    private:
        GstElement *element_;
    };

    class caps_filter_element : public element
    {
    public:
        template <typename... Args>
        caps_filter_element(GstBin *pipeline, const char *name, const char *media_type, Args... args) : element(pipeline, "capsfilter", name)
        {
            if (*this)
            {
                auto caps = gst_caps_new_simple(media_type, args..., nullptr);
                if (!caps)
                {
                    SPDLOG_ERROR("Failed to crate caps.");
                    reset();
                }
                else
                {
                    set("caps", caps, nullptr);
                    gst_caps_unref(caps);
                }
            }
        }
    };
}

#endif