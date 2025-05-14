#include "element.hpp"

namespace nvr
{
    element::element(GstBin *pipeline, const char *factoryname, const char *name) noexcept
    {
        GstElement* element = gst_element_factory_make(factoryname, name);
        if (element)
        {
            if (gst_bin_add(pipeline, element)) {
                element_ = static_cast<GstElement *>(gst_object_ref(element));
            } else {
                SPDLOG_ERROR("Failed to add {} element to pipeline.", factoryname);
                g_object_unref(element);
                element_ = nullptr;
            }
        }
        else
        {
            SPDLOG_ERROR("Failed to create {} element.", factoryname);
            element_ = nullptr;
        }
    }
    
    element::element(const element &src) noexcept
    {
        element_ = src ? static_cast<GstElement *>(gst_object_ref(src.element_)) : nullptr;
    }

    element &element::operator=(GstElement *src) & noexcept
    {
        reset();
        if (element_ != src)
        {
            element_ = src ? static_cast<GstElement *>(gst_object_ref(src)) : nullptr;
        }
        return *this;
    }

    element &element::operator=(const element &src) & noexcept
    {
        if (this == &src)
        {
            return *this;
        }

        reset();
        element_ = src ? static_cast<GstElement *>(gst_object_ref(src.element_)) : nullptr;

        return *this;
    }

    element &element::operator=(element &&src) & noexcept
    {
        reset();
        element_ = src.element_;
        src.element_ = nullptr;

        return *this;
    }

    element::~element()
    {
        reset();
    }
}