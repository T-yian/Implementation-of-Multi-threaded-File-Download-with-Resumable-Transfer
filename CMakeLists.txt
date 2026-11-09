cmake_minimum_required(VERSION 3.16)
project(downloader CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(downloader
    main.cpp
    http_client.cpp
    download_manager.cpp
    progress.cpp
    progress_display.cpp
    file_util.cpp
)

find_package(CURL REQUIRED)
target_link_libraries(downloader PRIVATE CURL::libcurl)

find_package(Threads REQUIRED)
target_link_libraries(downloader PRIVATE Threads::Threads)
