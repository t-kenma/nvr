#ifndef __NRV_AVF_SRC_HPP_
#define __NRV_AVF_SRC_HPP_
#include "pipeline.hpp"

#ifdef __APPLE__
namespace nvr
{
    class avf_src : public video_src
    {
    public:
        explicit avf_src(int width, int height);
        avf_src() = delete;
        avf_src(const avf_src&) = delete;
        avf_src(avf_src&&) = delete;

    protected:
        virtual element make_src_element(GstBin *pipeline) override;
    };
}
#endif /* __APPLE__ */
#endif