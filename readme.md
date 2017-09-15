C Make Options - Clion
-D
CMAKE_C_COMPILER=/usr/bin/gcc
-D
CMAKE_CXX_COMPILER=/usr/bin/g++

g++ -Wall main.cpp -o helloworld $(pkg-config --cflags --libs gstreamer-1.0)
./helloworld