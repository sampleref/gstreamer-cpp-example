cmake_minimum_required(VERSION 3.8)
project(DemoCpp)
find_package(PkgConfig REQUIRED)
pkg_check_modules(GSTLIBS REQUIRED
        gobject-2.0
        glib-2.0
        gstreamer-webrtc-1.0
        gstreamer-sdp-1.0
        gstreamer-pbutils-1.0
        libsoup-2.4
        json-glib-1.0
        gstreamer-rtsp-server-1.0
        gstreamer-check-1.0)

set(CMAKE_CXX_STANDARD 11)
#set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Werror")

# Following example at https://stackoverflow.com/questions/38959678/is-there-a-simple-gstreamer-example-for-c
# https://gstreamer.freedesktop.org/documentation/installing/on-linux.html

# add extra include directories
# gst-config.h moved to /usr/lib/x86_64-linux-gnu/gstreamer-1.0/include
include_directories(
        /usr/lib/x86_64-linux-gnu/glib-2.0/include
        /usr/include/glib-2.0
        /usr/local/include/gstreamer-1.0
        /usr/local/include/libsoup-2.4
        /usr/local/include/json-glib-1.0)

#[[link_libraries(gstreamer-1.0
        gobject-2.0
        glib-2.0
        gstreamer-webrtc-1.0
        gstreamer-sdp-1.0
        libsoup-2.4
        json-glib-1.0)]]

set(SOURCE_FILES main.cpp)
set(SOURCE_FILES_WEBRTC rtsp_webrtc.cpp)
set(SOURCE_FILES_RTSP rtsp_restream_text.cpp)
set(SOURCE_FILES_RTP_TEST gst_rtp_test.cpp)
set(SOURCE_FILES_RTSP_APPSRC rtsp_stream_appsrc.cpp)

link_directories(${GSTLIBS_LIBRARY_DIRS})

add_executable(mainapp ${SOURCE_FILES})
add_executable(rtsp2webrtc ${SOURCE_FILES_WEBRTC})
add_executable(rtsprestream ${SOURCE_FILES_RTSP})
add_executable(gstrtptest ${SOURCE_FILES_RTP_TEST})
add_executable(rtspstreamappsrc ${SOURCE_FILES_RTSP_APPSRC})

target_link_libraries(mainapp ${GSTLIBS_LIBRARIES})
target_link_libraries(rtsp2webrtc ${GSTLIBS_LIBRARIES})
target_link_libraries(rtsprestream ${GSTLIBS_LIBRARIES})
target_link_libraries(gstrtptest ${GSTLIBS_LIBRARIES})
target_link_libraries(rtspstreamappsrc ${GSTLIBS_LIBRARIES})
