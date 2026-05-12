cmake_minimum_required(VERSION 3.10)
project(zhr-demo-request-response)

find_package(Threads REQUIRED)

set(VSOMEIP_NAME "vsomeip3")
find_package(${VSOMEIP_NAME} REQUIRED
    PATHS /local_ssd/zhuhairong/pytorch-ssd/zhr/vsomeip/build
    NO_DEFAULT_PATH
)

add_executable(demo_service service.cpp)
target_link_libraries(demo_service PRIVATE ${VSOMEIP_LIBRARIES} Threads::Threads)

add_executable(demo_client client.cpp)
target_link_libraries(demo_client PRIVATE ${VSOMEIP_LIBRARIES} Threads::Threads)
