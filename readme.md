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