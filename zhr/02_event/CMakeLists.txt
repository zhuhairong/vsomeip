cmake_minimum_required(VERSION 3.10)
project(zhr-demo-event)

find_package(Threads REQUIRED)

set(VSOMEIP_NAME "vsomeip3")
find_package(${VSOMEIP_NAME} REQUIRED
    PATHS /local_ssd/zhuhairong/pytorch-ssd/zhr/vsomeip/build
    NO_DEFAULT_PATH
)

add_executable(publisher publisher.cpp)
target_link_libraries(publisher PRIVATE ${VSOMEIP_LIBRARIES} Threads::Threads)

add_executable(subscriber subscriber.cpp)
target_link_libraries(subscriber PRIVATE ${VSOMEIP_LIBRARIES} Threads::Threads)
