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
