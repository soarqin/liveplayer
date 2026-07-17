cmake_minimum_required(VERSION 3.20)
project(liveplayer C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
add_link_options(
    $<$<AND:$<NOT:$<COMPILE_LANGUAGE:ASM_NASM>>,$<OR:$<C_COMPILER_ID:GNU>,$<C_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>>,$<OR:$<CONFIG:Release>,$<CONFIG:MinSizeRel>>>:-static>
    $<$<AND:$<NOT:$<COMPILE_LANGUAGE:ASM_NASM>>,$<C_COMPILER_ID:GNU>,$<OR:$<CONFIG:Release>,$<CONFIG:MinSizeRel>>>:-static-libgcc>
    $<$<AND:$<NOT:$<COMPILE_LANGUAGE:ASM_NASM>>,$<CXX_COMPILER_ID:GNU>,$<OR:$<CONFIG:Release>,$<CONFIG:MinSizeRel>>>:-static-libstdc++>
)

if(NOT MSVC)
    set(CMAKE_C_LINK_FLAGS "${CMAKE_C_LINK_FLAGS} -static")
endif()

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

# 禁用链接器自动生成 manifest：我们已在 resource.rc 中通过 RT_MANIFEST 嵌入了自定义 manifest
if(MSVC)
    target_link_options(${PROJECT_NAME} PRIVATE "/MANIFEST:NO")
    # 强制 MSVC 以 UTF-8 解析源文件，避免无 BOM 时中文字符串编译乱码
    target_compile_options(${PROJECT_NAME} PRIVATE /utf-8)
else()
    target_link_options(${PROJECT_NAME} PRIVATE "-municode")
    # 强制 GNU/LLVM windres 以 UTF-8 解析资源文件，避免中文对话框乱码
    set(CMAKE_RC_FLAGS "${CMAKE_RC_FLAGS} --codepage=65001")
endif()

target_link_libraries(${PROJECT_NAME} PRIVATE
    user32
    shell32
    comctl32
    gdi32
    uxtheme
)
