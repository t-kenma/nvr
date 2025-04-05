#include "avf_src.hpp"
#include "logging.hpp"

namespace nvr
{
    avf_src::avf_src(int width, int height) : video_src(width, height)
    {
    }

    element avf_src::make_src_element(GstBin *pipeline)
    {
        return element(pipeline, "avfvideosrc", "avf_src");
    }
}