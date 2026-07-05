cmake_minimum_required(VERSION 3.22.1)

project("cz_imgui")
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory(extern/BNM-Android EXCLUDE_FROM_ALL)
get_property(BNM_INCLUDE_DIRECTORIES TARGET BNM PROPERTY BNM_INCLUDE_DIRECTORIES)

add_subdirectory(extern/imgui EXCLUDE_FROM_ALL)

add_library(${CMAKE_PROJECT_NAME} SHARED
        src/native-lib.cpp
        src/czrender.cpp
        include/XRInput.cpp
)

target_include_directories(${CMAKE_PROJECT_NAME} PUBLIC
    ${BNM_INCLUDE_DIRECTORIES}
    extern/BNM-Android/src/private
    include
    extern/imgui
)

target_link_libraries(${CMAKE_PROJECT_NAME}
        android
        log
        BNM
        imgui
)

target_compile_options(${CMAKE_PROJECT_NAME} PRIVATE
    $<$<CONFIG:Release>:-O3 -ffast-math -funroll-loops -fvisibility=hidden -fvisibility-inlines-hidden>
)

target_link_options(${CMAKE_PROJECT_NAME} PRIVATE
    $<$<CONFIG:Release>:-Wl,-O3 -Wl,--gc-sections>
)
