cmake_minimum_required(VERSION 3.8...3.26)


project(BNM)

# Add dependents to liblog.so
set(BNM_LINK_LOG OFF)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(DOBBY_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../Dobby)
macro(SET_OPTION option value)
    set(${option} ${value} CACHE INTERNAL "" FORCE)
endmacro()
SET_OPTION(DOBBY_DEBUG OFF)
SET_OPTION(DOBBY_GENERATE_SHARED OFF)
add_subdirectory(${DOBBY_DIR} dobby)

set(PUBLIC_INCLUDE_DIRS
    include
    external/include
)

set(INCLUDE_DIRS
    include
    external/include
    external
    external/utf8
    src/private
)

add_library(
        BNM
        STATIC
        src/Class.cpp
        src/ClassesManagement.cpp
        src/Coroutine.cpp
        src/Defaults.cpp
        src/Delegates.cpp
        src/EventBase.cpp
        src/Exceptions.cpp
        src/FieldBase.cpp
        src/Hooks.cpp
        src/Image.cpp
        src/Internals.cpp
        src/Loading.cpp
        src/MethodBase.cpp
        src/MonoStructures.cpp
        src/PropertyBase.cpp
        src/UnityStructures.cpp
        src/Utils.cpp
)

target_include_directories(
        BNM
        PUBLIC
        ${INCLUDE_DIRS}
)

target_link_libraries(BNM PUBLIC dobby)

if (${BNM_LINK_LOG})
    target_link_libraries(
            BNM
            PUBLIC
            log
    )
endif ()


set_property(TARGET BNM PROPERTY BNM_INCLUDE_DIRECTORIES ${PUBLIC_INCLUDE_DIRS})

