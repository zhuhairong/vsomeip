cmake_minimum_required(VERSION 3.10)
project(zhr-demo-field)

find_package(Threads REQUIRED)

set(VSOMEIP_NAME "vsomeip3")
find_package(${VSOMEIP_NAME} REQUIRED
    PATHS /local_ssd/zhuhairong/pytorch-ssd/zhr/vsomeip/build
    NO_DEFAULT_PATH
)

add_executable(field_service service.cpp)
target_link_libraries(field_service PRIVATE ${VSOMEIP_LIBRARIES} Threads::Threads)

add_executable(field_client client.cpp)
target_link_libraries(field_client PRIVATE ${VSOMEIP_LIBRARIES} Threads::Threads)
