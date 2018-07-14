#Intellij Clion C Make Options
-D
CMAKE_C_COMPILER=/usr/bin/gcc
-D
CMAKE_CXX_COMPILER=/usr/bin/g++

# https://gstreamer.freedesktop.org/documentation/installing/on-linux.html
# Building applications using GStreamer
# remember to add this string to your gcc command
g++ -Wall main.cpp -o helloworld $(pkg-config --cflags --libs gstreamer-1.0)
./helloworld


g++ -Wall rtsp_webrtc.cpp -o rtsp_webrtc $(pkg-config --libs --cflags gstreamer-1.0 gstreamer-webrtc-1.0 gstreamer-sdp-1.0 libsoup-2.4 json-glib-1.0)
./rtsp_webrtc --peer-id=1234 --server=wss://127.0.0.1:8443