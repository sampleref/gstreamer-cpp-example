cmake_minimum_required(VERSION 3.8)
project(DemoCpp)

set(CMAKE_CXX_STANDARD 11)

# Following example at https://stackoverflow.com/questions/38959678/is-there-a-simple-gstreamer-example-for-c
# https://gstreamer.freedesktop.org/documentation/installing/on-linux.html

# add extra include directories
# gst-config.h moved to /usr/lib/x86_64-linux-gnu/gstreamer-1.0/include
include_directories(/usr/include/gstreamer-1.0 /usr/lib/x86_64-linux-gnu/gstreamer-1.0/include /usr/lib/x86_64-linux-gnu/glib-2.0/include /usr/include/glib-2.0)

link_libraries(gstreamer-1.0 gobject-2.0 glib-2.0)

set(SOURCE_FILES main.cpp)
add_executable(DemoGstreamerCpp ${SOURCE_FILES})