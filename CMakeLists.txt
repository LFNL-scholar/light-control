cmake_minimum_required(VERSION 3.22)
set(IDF_TARGET "esp32s3")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "main/index.html" "main/app.js" "main/scene.js" "main/stats.js"
    "main/vendor/three.module.min.js" "main/pack_assets.py")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(light_control)
