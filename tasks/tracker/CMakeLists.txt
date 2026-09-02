cmake_minimum_required(VERSION 3.16)

set(TRACKER_SRC
    Tracker.cpp
)

add_library(tracker OBJECT ${TRACKER_SRC})

target_link_libraries(tracker light_detect tools)
