project('nrs-video-recorder',
    'cpp',
    version : '0.1',
    default_options : [
        'cpp_std=gnu++17',
        'warning_level=3'
    ]
)

app_sources = [
    'src/main.cpp',
    'src/config.cpp',
    'src/pipeline.cpp',
    'src/video_src.cpp',
    'src/jpeg_sink.cpp',
    'src/v4l2_src.cpp',
    'src/element.cpp',
    'src/gpio.cpp',
    'src/led_manager.cpp',
    'src/sd_manager.cpp',
    'src/reset_monitor.cpp',
    'src/logging.cpp',
    'src/usb_raw_control_event.cpp',
    'src/usb_raw_gadget.cpp',
    'src/usb.cpp',
    'src/eeprom.cpp',
    'src/common.cpp',
]

update_sources = [
    'src/main-update.cpp',
    'src/gpio.cpp',
    'src/led_manager.cpp',
    'src/sd_manager.cpp',
    'src/reset_monitor.cpp',
    'src/logging.cpp',
    'src/eeprom.cpp',
    'src/common.cpp',
]

if host_machine.system() == 'darwin'
    app_sources += 'src/avf_src.cpp'
endif

thirdparty = include_directories('thirdparty')

gst_dep = dependency('gstreamer-1.0', version : '>=1.16',
    required : true, fallback : ['gstreamer', 'gst_dep'])

thread_dep = dependency('threads')

libjpeg_dep = dependency('libjpeg')

deps = [ 
    gst_dep, thread_dep, libjpeg_dep
]

if host_machine.system() != 'darwin'
    cppfs_dep = meson.get_compiler('cpp').find_library('stdc++fs')
    deps += cppfs_dep
endif

exe = executable(
    'nvr',
    app_sources,
    include_directories: [thirdparty],
    dependencies : deps
)

exe = executable(
    'update',
    update_sources,
    include_directories: [thirdparty],
    dependencies : deps
)

test('basic', exe)

