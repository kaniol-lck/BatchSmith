# Copyright (C) 2022 The Qt Company Ltd.
# SPDX-License-Identifier: BSD-3-Clause
#
# ---------------------------------------------------------------------------
# 本文件派生自 qtbase/cmake/FindWrapOpenGL.cmake（Qt 6.7.2），
# 与上游唯一的差异是**删掉了整段解析 AGL framework 的代码**。
# AGL 相关行与 Qt 6.9 的上游修复（codereview.qt-project.org/c/qt/qtbase/+/652022）
# 逐行对应。上游许可（BSD-3-Clause）与版权声明按原样保留。
# ---------------------------------------------------------------------------
#
# 为什么要覆盖 Qt 自带的同名模块
#
#   Qt 6.9 之前，非商业发行版把下面这段留在 FindWrapOpenGL.cmake 里：
#
#       find_library(WrapOpenGL_AGL NAMES AGL)
#       if(WrapOpenGL_AGL)
#           set(__opengl_agl_fw_path "${WrapOpenGL_AGL}")
#       endif()
#       if(NOT __opengl_agl_fw_path)
#           set(__opengl_agl_fw_path "-framework AGL")     # ← 问题所在
#       endif()
#       target_link_libraries(WrapOpenGL::WrapOpenGL INTERFACE ${__opengl_agl_fw_path})
#
#   AGL 是 Carbon 时代的 OpenGL 垫片，早已废弃，并已从 macOS 26（Tahoe）的
#   SDK 中移除。在旧 SDK 上 find_library 能成功、拿到真实路径，所以这个缺陷
#   潜伏多年无人察觉；一旦 SDK 里不再有 AGL，find_library 返回 NOTFOUND，
#   上面那个 if 就把字面量 "-framework AGL" 写进了链接接口，
#   于是**任何链接 Qt6::Gui 的程序**都在链接期失败：
#
#       ld: framework 'AGL' not found
#
#   注意失败点与我们自己的代码无关 —— 报错的是 Qt 的公开链接依赖。
#   Qt 官方在 6.9 里把整段删除；这里做同样的删除，使 6.7.2 也能正常链接。
#
# 为什么这个覆盖会生效
#
#   Qt6Config.cmake 是用 list(APPEND ...) 把 Qt 自己的模块目录追加进
#   CMAKE_MODULE_PATH 的，而顶层 CMakeLists.txt 用 list(PREPEND ...) 追加
#   cmake/，于是本文件排在 Qt 自带副本之前，
#   find_package(WrapOpenGL)（模块模式）会优先找到它。
#   ⚠️ 顺序是这个方案的全部依仗：顶层 CMakeLists 里那句 PREPEND 必须在
#   find_package(Qt6) 之前，且不能改成 APPEND。find_package(Qt6) 之后的
#   断言就是用来兜住这个前提的。
#
# 安全性与副作用
#
#   本项目不使用 AGL，也没有任何代码引用其符号 —— OpenGL 本身仍在 SDK 中，
#   只是不再随附这个 Carbon 垫片。非 Apple 平台上本文件与上游逐字一致
#   （AGL 段本来就包在 if(APPLE) 内），因此 Linux / Windows 行为不变。

# We can't create the same interface imported target multiple times, CMake will complain if we do
# that. This can happen if the find_package call is done in multiple different subdirectories.
if(TARGET WrapOpenGL::WrapOpenGL)
    set(WrapOpenGL_FOUND ON)
    return()
endif()

set(WrapOpenGL_FOUND OFF)

find_package(OpenGL ${WrapOpenGL_FIND_VERSION})

if (OpenGL_FOUND)
    set(WrapOpenGL_FOUND ON)

    add_library(WrapOpenGL::WrapOpenGL INTERFACE IMPORTED)
    if(APPLE)
        # CMake 3.27 and older:
        # On Darwin platforms FindOpenGL sets IMPORTED_LOCATION to the absolute path of the library
        # within the framework. This ends up as an absolute path link flag, which we don't want,
        # because that makes our .prl files un-relocatable.
        # Extract the framework path instead, and use that in INTERFACE_LINK_LIBRARIES,
        # which CMake ends up transforming into a relocatable -framework flag.
        # See https://gitlab.kitware.com/cmake/cmake/-/issues/20871 for details.
        #
        # CMake 3.28 and above:
        # IMPORTED_LOCATION is the absolute path the the OpenGL.framework folder.
        get_target_property(__opengl_fw_lib_path OpenGL::GL IMPORTED_LOCATION)
        if(__opengl_fw_lib_path AND NOT __opengl_fw_lib_path MATCHES "/([^/]+)\\.framework$")
            get_filename_component(__opengl_fw_path "${__opengl_fw_lib_path}" DIRECTORY)
        endif()

        if(NOT __opengl_fw_path)
            # Just a safety measure in case if no OpenGL::GL target exists.
            set(__opengl_fw_path "-framework OpenGL")
        endif()

        # 上游在此处还有 AGL 的 find_library 与第二个 target_link_libraries，
        # 已按 Qt 6.9 的修复删除。详见文件头的说明。
        target_link_libraries(WrapOpenGL::WrapOpenGL INTERFACE ${__opengl_fw_path})
    else()
        target_link_libraries(WrapOpenGL::WrapOpenGL INTERFACE OpenGL::GL)
    endif()
elseif(UNIX AND NOT APPLE AND NOT CMAKE_SYSTEM_NAME STREQUAL "Integrity")
    # Requesting only the OpenGL component ensures CMake does not mark the package as
    # not found if neither GLX nor libGL are available. This allows finding OpenGL
    # on an X11-less Linux system.
    find_package(OpenGL ${WrapOpenGL_FIND_VERSION} COMPONENTS OpenGL)
    if (OpenGL_FOUND)
        set(WrapOpenGL_FOUND ON)
        add_library(WrapOpenGL::WrapOpenGL INTERFACE IMPORTED)
        target_link_libraries(WrapOpenGL::WrapOpenGL INTERFACE OpenGL::OpenGL)
    endif()
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(WrapOpenGL DEFAULT_MSG WrapOpenGL_FOUND)
