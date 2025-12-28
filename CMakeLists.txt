cmake_minimum_required(VERSION 3.15)
project(monitor_mouse LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(SRC src/monitor_mouse.cpp)

add_executable(monitor_mouse ${SRC})
if (MSVC)
    target_link_libraries(monitor_mouse PRIVATE setupapi user32)
else()
    target_link_libraries(monitor_mouse PRIVATE setupapi user32)
endif()

add_executable(monitor_keyboard src/monitor_keyboard.cpp)
target_link_libraries(monitor_keyboard PRIVATE setupapi user32)

add_executable(monitor_dns src/monitor_dns.cpp)
# Use vendored headers if available (repo root `include`)
target_include_directories(monitor_dns PRIVATE ${CMAKE_SOURCE_DIR}/include)
if (MSVC)
    # Npcap/WinPcap: link wpcap, Packet, iphlpapi and ws2_32
    target_link_libraries(monitor_dns PRIVATE wpcap Packet iphlpapi ws2_32)
else()
    target_link_libraries(monitor_dns PRIVATE pcap iphlpapi ws2_32)
endif()
