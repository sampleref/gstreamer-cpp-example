cmake_minimum_required(VERSION 3.8)
project(DemoCpp)
find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)
find_library(AVCODEC_LIBRARY avcodec)
find_library(AVFORMAT_LIBRARY avformat)
find_library(AVUTIL_LIBRARY avutil)
find_library(AVFILTER_LIBRARY avfilter)
find_library(SWRESAMPLE_LIBRARY swresample)
find_library(SWSCALE_LIBRARY swscale)
pkg_check_modules(GSTLIBS REQUIRED
        gobject-2.0
        glib-2.0
        gstreamer-webrtc-1.0
        gstreamer-sdp-1.0
        gstreamer-pbutils-1.0
        gst-editing-services-1.0
        libsoup-2.4
        json-glib-1.0)

set(CMAKE_CXX_STANDARD 11)
#set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Werror")
#set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address -g -OX")
#set (CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -fno-omit-frame-pointer -fsanitize=address -g -O1")
#set (CMAKE_LINKER_FLAGS_DEBUG "${CMAKE_LINKER_FLAGS_DEBUG} -fno-omit-frame-pointer -fsanitize=address -g -O1")

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
set(SOURCE_FILES_WEBRTC_1_N rtsp_webrtc_1_n.cpp)
set(SOURCE_FILES_MULTIPIPELINE multi_pipeline.cpp)
set(SOURCE_FILES_EDITOR gst_editor.cpp)

link_directories(${GSTLIBS_LIBRARY_DIRS})

add_executable(mainapp ${SOURCE_FILES})
add_executable(rtsp2webrtc ${SOURCE_FILES_WEBRTC})
add_executable(rtsp2webrtc_1_n ${SOURCE_FILES_WEBRTC_1_N})
add_executable(multi_pipeline ${SOURCE_FILES_MULTIPIPELINE})
add_executable(gst_editor ${SOURCE_FILES_EDITOR})

target_link_libraries(mainapp ${GSTLIBS_LIBRARIES} ${AVCODEC_LIBRARY} ${AVFORMAT_LIBRARY} ${AVUTIL_LIBRARY} ${AVFILTER_LIBRARY})
target_link_libraries(rtsp2webrtc ${GSTLIBS_LIBRARIES})
target_link_libraries(rtsp2webrtc_1_n ${GSTLIBS_LIBRARIES} ${CMAKE_THREAD_LIBS_INIT})
target_link_libraries(multi_pipeline ${GSTLIBS_LIBRARIES} ${CMAKE_THREAD_LIBS_INIT})
target_link_libraries(gst_editor ${GSTLIBS_LIBRARIES} ${CMAKE_THREAD_LIBS_INIT})