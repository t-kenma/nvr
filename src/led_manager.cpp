#include "gpio.hpp"
#include "logging.hpp"
#include "led_manager.hpp"
namespace nvr 
{

    const int led_manager::off      = 0;
    const int led_manager::on       = 1;
    const int led_manager::blink    = 2;
    const int led_manager::one      = 3;
    const int led_manager::two      = 4;

    led_manager::led_manager() noexcept
    : g_type_(off),
      r_type_(off),
      g_counter_(0),
      r_counter_(0)
    {
        init();
    }
            
/***********************************************************
***********************************************************/

/*----------------------------------------------------------
----------------------------------------------------------*/
    void led_manager::init()
    {
       	grn_ = std::make_shared<nvr::gpio_out>("193", "P9_1");
		yel_ = std::make_shared<nvr::gpio_out>("192", "P9_0");
		red_ = std::make_shared<nvr::gpio_out>("200", "P10_0");
        
        
        //---LED初期化
        //
        if (grn_->open(true)) {
            SPDLOG_ERROR("Failed to open grn_.");
            exit(-1);
        }
        
        if (yel_->open(true)) {
            SPDLOG_ERROR("Failed to open yel_.");
            exit(-1);
        }
        
        if (red_->open(true)) {
            SPDLOG_ERROR("Failed to open red_.");
            exit(-1);
        }
    }


/*----------------------------------------------------------
----------------------------------------------------------*/
    void led_manager::update_led()
    {
        //SPDLOG_INFO("g_counter_{}", g_counter_);
        //SPDLOG_INFO("r_counter_{}", r_counter_);
        //SPDLOG_INFO("g_type_{}", g_type_);
        //SPDLOG_INFO("r_type_{}", r_type_);
        SPDLOG_INFO("update_led");

        if(g_type_ == off)
        {
            //---消灯
            //
            set_green(true);
            g_counter_ = 0;
        }
        else
        if(g_type_ == on)
        {
            //---点灯
            //
            set_green(false);
            g_counter_ = 0;
        }
        else
        if(g_type_ == blink)
        {
            //---点滅
            //
            if( g_counter_ >= 0 && g_counter_ < 10 )
            {
                set_green(false);
            }
            else
            if( g_counter_ >= 10 && g_counter_ < 20 )
            {
                set_green(true);
            }
            
            g_counter_++;
            if( g_counter_ >= 20)
            {
                g_counter_ = 0;
            }
        }
        else
        if(g_type_ == one)
        {
            //---1回点滅
            //
            if( g_counter_ >= 0 && g_counter_ < 2 )
            {
                set_green(false);
            }
            else
            if( g_counter_ >= 2 && g_counter_ < 12 )
            {
                set_green(true);
            }
            
            g_counter_++;
            if( g_counter_ >= 12)
            {
                g_counter_ = 0;
            }
        }
        else
        if( g_type_ == two)
        {
            //---2回点滅
            //
            if( g_counter_ >= 0 && g_counter_ < 2 )
            {
                set_green(false);
            }
            else
            if( g_counter_ >= 2 && g_counter_ < 4 )
            {
                set_green(true);
            }
            if( g_counter_ >= 4 && g_counter_ < 6 )
            {
                set_green(false);
            }
            if( g_counter_ >= 6 && g_counter_ < 16 )
            {
                set_green(true);
            }
            
            g_counter_++;
            if( g_counter_ >= 16)
            {
                g_counter_ = 0;
            }
        }
        
        if( r_type_ == off)
        {
            //---消灯
            //
            set_red(true);
            r_counter_ = 0;
        }
        else
        if(r_type_ == blink)
        {
            //---点滅
            //
            if( r_counter_ >= 0 && r_counter_ < 10 )
            {
                set_red(false);
            }
            else
            if( r_counter_ >= 10 && r_counter_ < 20 )
            {
                set_red(true);
            }
            
            r_counter_++;
            if( r_counter_ >= 20)
            {
                r_counter_ = 0;
            }
        }
    }
}

