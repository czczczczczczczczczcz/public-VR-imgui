cmake_minimum_required(VERSION 3.22.1)
project(imgui CXX)

add_library(imgui STATIC
    imgui.cpp
    imgui_draw.cpp
    imgui_tables.cpp
    imgui_widgets.cpp
)

target_include_directories(imgui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(imgui PUBLIC
    IMGUI_DISABLE_DEMO_WINDOWS
    IMGUI_DISABLE_DEBUG_TOOLS
)
set_target_properties(imgui PROPERTIES POSITION_INDEPENDENT_CODE ON)
