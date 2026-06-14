cmake_minimum_required(VERSION 3.20)
project(liveplayer C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# 强制使用 Unicode 版 WinAPI
add_definitions(-DUNICODE -D_UNICODE)

# 用户可通过 -DMPV_ROOT=<path> 指定 libmpv 头文件目录，要求目录下包含 include/mpv/client.h
# 运行时通过 LoadLibraryW 动态加载 libmpv-2.dll，无需链接 mpv.lib
set(MPV_ROOT "" CACHE PATH "Path to libmpv headers (contains include/mpv/client.h)")

if(NOT MPV_ROOT)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/mpv/include/mpv/client.h")
        set(MPV_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/mpv")
    endif()
endif()

if(MPV_ROOT)
    message(STATUS "Using libmpv headers from: ${MPV_ROOT}")
    include_directories("${MPV_ROOT}/include")
else()
    message(WARNING "MPV_ROOT not set and default ./mpv/include/mpv/client.h not found. Build may fail. Specify -DMPV_ROOT=<path>.")
endif()

set(SOURCES
    src/main.c
    src/mpv_dynamic.c
    src/resource.rc
)

add_executable(${PROJECT_NAME} WIN32 ${SOURCES})

target_link_libraries(${PROJECT_NAME} PRIVATE
    user32
    shell32
    comctl32
    gdi32
)
