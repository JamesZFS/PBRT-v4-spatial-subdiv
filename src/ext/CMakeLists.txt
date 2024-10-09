
cmake_minimum_required (VERSION 3.12)

if (MSVC)
  add_definitions(/D _CRT_SECURE_NO_WARNINGS /Dstrdup=_strdup
    /wd4018 /wd4100 /wd4101 /wd4127 /wd4146 /wd4232 /wd4242 /wd4244 /wd4245 /wd4267 /wd4305 /wd4309 
    /wd4310 /wd4334 /wd4456 /wd4464 /wd4668 /wd4701 /wd4703 /wd4711 /wd4756 /wd4820 /wd5045 /wd5250)
endif ()

###########################################################################
# stb

set (STB_INCLUDE ${CMAKE_CURRENT_SOURCE_DIR}/stb PARENT_SCOPE)

###########################################################################
# filesystem

set (FILESYSTEM_INCLUDE ${CMAKE_CURRENT_SOURCE_DIR}/filesystem PARENT_SCOPE)

###########################################################################
# libdeflate

add_subdirectory (libdeflate)

set (LIBDEFLATE_INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/libdeflate PARENT_SCOPE)
set (LIBDEFLATE_LIBRARIES deflate::deflate PARENT_SCOPE)

set_property (TARGET deflate PROPERTY FOLDER "ext")

###########################################################################
# zlib

find_package (ZLIB)
if (NOT ZLIB_FOUND)
  # Build zlib
  set (ZLIB_BUILD_STATIC_LIBS ON CACHE BOOL " " FORCE)
  set (ZLIB_BUILD_SHARED_LIBS OFF CACHE BOOL " " FORCE)
  add_subdirectory (zlib)

  set (ZLIB_LIBRARIES zlibstatic)
  set (ZLIB_INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/zlib ${CMAKE_CURRENT_BINARY_DIR}/zlib)

  # try to make openexr happy about this...
  set (ZLIB_LIBRARY zlibstatic)
  set (ZLIB_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/zlib ${CMAKE_CURRENT_BINARY_DIR}/zlib)
  set (ZLIB_FOUND TRUE)

  set_property (TARGET zlibstatic PROPERTY FOLDER "ext")

  add_library (ZLIB::ZLIB ALIAS zlibstatic)
  include_directories(${ZLIB_INCLUDE_DIRS})  # yuck, but so openexr/ptex can find zlib.h...
endif ()

set (ZLIB_INCLUDE_DIRS ${ZLIB_INCLUDE_DIRS} PARENT_SCOPE)
set (ZLIB_LIBRARIES ${ZLIB_LIBARIES} PARENT_SCOPE)

###########################################################################
# OpenEXR

find_package (OpenEXR 2.3) # 2.3 is the first version compatible with C++17

if (OPENEXR_FOUND)
  message (STATUS "Found OpenEXR version ${OPENEXR_VERSION}")
  set (OPENEXR_INCLUDE ${OPENEXR_INCLUDES}/OpenEXR ${ILMBASE_INCLUDES}/OpenEXR PARENT_SCOPE)
  if (WIN32)
    set (OPENEXR_LIBS ${OPENEXR_LIBRARIES} ${ZLIB_LIBRARY} PARENT_SCOPE)
  else ()
    set (OPENEXR_LIBS ${OPENEXR_LIBRARIES} PARENT_SCOPE)
  endif ()
else ()
  message (STATUS "OpenEXR not found; building it from scratch.")

  SET (CMAKE_POLICY_DEFAULT_CMP0077 NEW)

  set (ILMBASE_NAMESPACE_VERSIONING OFF CACHE BOOL " " FORCE)
  set (OPENEXR_NAMESPACE_VERSIONING OFF CACHE BOOL " " FORCE)
  set (OPENEXR_BUILD_SHARED_LIBS    OFF CACHE BOOL " " FORCE)
  set (ILMBASE_BUILD_SHARED_LIBS    OFF CACHE BOOL " " FORCE)
  set (PYILMBASE_ENABLE             OFF CACHE BOOL " " FORCE)
  set (OPENEXR_BUILD_UTILS          OFF CACHE BOOL " " FORCE)

  add_subdirectory (openexr)

  set_property (TARGET IexMath IlmThread Half
                Iex Imath IlmImf HalfTest IexTest
                IlmImfExamples IlmImfTest IlmImfUtil IlmImfUtilTest ImathTest
                PROPERTY FOLDER "ext/OpenEXR")

  set (OPENEXR_INCLUDE
    ${CMAKE_CURRENT_SOURCE_DIR}/openexr/IlmBase/Imath
    ${CMAKE_CURRENT_SOURCE_DIR}/openexr/IlmBase/Half
    ${CMAKE_CURRENT_SOURCE_DIR}/openexr/IlmBase/Iex
    ${CMAKE_CURRENT_SOURCE_DIR}/openexr/OpenEXR/IlmImf
    ${CMAKE_CURRENT_BINARY_DIR}/openexr/IlmBase/config
    ${CMAKE_CURRENT_BINARY_DIR}/openexr/OpenEXR/config
  PARENT_SCOPE
  )

  if (WIN32)
    set (OPENEXR_LIBS OpenEXR::IlmImf IlmBase::Imath IlmBase::Half ${ZLIB_LIBRARY} PARENT_SCOPE)
  else ()
    set (OPENEXR_LIBS OpenEXR::IlmImf IlmBase::Imath IlmBase::Half PARENT_SCOPE)
  endif ()
endif ()

###########################################################################
# ptex

set (PTEX_BUILD_SHARED_LIBS OFF CACHE BOOL " " FORCE)

set (CMAKE_MACOSX_RPATH 1)
if (WIN32)
  add_definitions (/DPTEX_STATIC)
endif ()

add_subdirectory (ptex)

set_property (TARGET Ptex_static ptxinfo halftest ftest rtest wtest PROPERTY FOLDER "ext/ptex")

set (PTEX_INCLUDE ${CMAKE_CURRENT_SOURCE_DIR}/ptex/src/ptex PARENT_SCOPE)

###########################################################################
# double-conversion

add_subdirectory (double-conversion)

set (DOUBLE_CONVERSION_INCLUDE ${CMAKE_CURRENT_SOURCE_DIR}/double-conversion PARENT_SCOPE)

set_property (TARGET double-conversion cctest PROPERTY FOLDER "ext")

###########################################################################
# nanovdb

set (NANOVDB_INCLUDE ${CMAKE_CURRENT_SOURCE_DIR}/openvdb/nanovdb PARENT_SCOPE)

###########################################################################
# FLIP

set (FLIP_INCLUDE ${CMAKE_CURRENT_SOURCE_DIR}/flip PARENT_SCOPE)

add_library (flip_lib STATIC ${CMAKE_CURRENT_SOURCE_DIR}/flip/flip.cpp)

set_property (TARGET flip_lib PROPERTY FOLDER "ext")

###########################################################################
# utf8proc

set (UTF8PROC_INCLUDE ${CMAKE_CURRENT_SOURCE_DIR}/utf8proc PARENT_SCOPE)

add_subdirectory (utf8proc)

###########################################################################
# qoi

set (QOI_INCLUDE ${CMAKE_CURRENT_SOURCE_DIR}/qoi PARENT_SCOPE)

###########################################################################
# glfw / glad

set (GLFW_INCLUDE ${CMAKE_CURRENT_SOURCE_DIR}/glfw/include PARENT_SCOPE)
set (GLAD_INCLUDE ${CMAKE_CURRENT_SOURCE_DIR}/glad/include PARENT_SCOPE)

set (GLFW_LIBRARY_TYPE STATIC CACHE STRING "" FORCE)
set (GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set (GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set (GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

add_subdirectory (glfw)
add_subdirectory (glad)

set_property (TARGET glfw PROPERTY FOLDER "ext")
set_property (TARGET glad PROPERTY FOLDER "ext")

###########################################################################
# imgui

set (IMGUI_INCLUDE ${CMAKE_CURRENT_SOURCE_DIR}/imgui)
set (IMGUI_INCLUDE ${IMGUI_INCLUDE} PARENT_SCOPE)

set (IMGUI_SOURCES
        ${IMGUI_INCLUDE}/imgui.cpp
        ${IMGUI_INCLUDE}/imgui_demo.cpp
        ${IMGUI_INCLUDE}/imgui_draw.cpp
        ${IMGUI_INCLUDE}/imgui_tables.cpp
        ${IMGUI_INCLUDE}/imgui_widgets.cpp
        ${IMGUI_INCLUDE}/backends/imgui_impl_glfw.cpp
        ${IMGUI_INCLUDE}/backends/imgui_impl_opengl3.cpp
)

add_library(imgui STATIC ${IMGUI_SOURCES})
target_include_directories(imgui PUBLIC ${IMGUI_INCLUDE})
target_compile_options(imgui PRIVATE -lGL)  # for Linux

###########################################################################
# implot

set (IMPLOT_INCLUDE ${CMAKE_CURRENT_SOURCE_DIR}/implot)
set (IMPLOT_INCLUDE ${IMPLOT_INCLUDE} PARENT_SCOPE)

set (IMPLOT_SOURCES
        ${IMPLOT_INCLUDE}/implot.cpp
        ${IMPLOT_INCLUDE}/implot_demo.cpp
        ${IMPLOT_INCLUDE}/implot_items.cpp
)

add_library(implot STATIC ${IMPLOT_SOURCES})
target_include_directories(implot PUBLIC ${IMPLOT_INCLUDE} ${IMGUI_INCLUDE})
target_link_libraries(implot imgui)

###########################################################################
# ImGuiFileDialog

set (IMGUIFILEDIALOG_INCLUDE ${CMAKE_CURRENT_SOURCE_DIR}/ImGuiFileDialog)
set (IMGUIFILEDIALOG_INCLUDE ${IMGUIFILEDIALOG_INCLUDE} PARENT_SCOPE)

set (IMGUIFILEDIALOG_SOURCES ${IMGUIFILEDIALOG_INCLUDE}/ImGuiFileDialog.cpp)

add_library(ImGuiFileDialog STATIC ${IMGUIFILEDIALOG_SOURCES})
target_include_directories(ImGuiFileDialog PUBLIC ${IMGUIFILEDIALOG_INCLUDE} ${IMGUI_INCLUDE})
target_link_libraries(ImGuiFileDialog imgui)

###########################################################################
# TBB
#if (PBRT_WITH_PATH_GUIDING OR PBRT_WITH_OIDN)
#include(ExternalProject)
#  set(TBB_ARGS "")
#
#  set(TBB_TEST OFF CACHE BOOL "" FORCE)
#  set(TBB_STRICT OFF CACHE BOOL "" FORCE)
#
#  set(TBB_ARGS ${TBB_ARGS}
#    -DCMAKE_INSTALL_PREFIX:PATH=${CMAKE_INSTALL_PREFIX}
#    -DCMAKE_PREFIX_PATH:PATH=${CMAKE_PREFIX_PATH}
#    -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
#    -DTBB_TEST=OFF
#    -DTBB_STRICT=OFF
#  )
#
#  if(APPLE)
#    set(CMAKE_OSX_ARCHITECTURES ${CMAKE_OSX_ARCHITECTURES})
#    set(CMAKE_OSX_DEPLOYMENT_TARGET ${CMAKE_OSX_DEPLOYMENT_TARGET})
#    set(TBB_ARGS ${TBB_ARGS}
#        -DCMAKE_OSX_ARCHITECTURES=${CMAKE_OSX_ARCHITECTURES}
#        -DCMAKE_OSX_DEPLOYMENT_TARGET=${CMAKE_OSX_DEPLOYMENT_TARGET}
#    )
#  endif()
#
#  ExternalProject_Add(TBB
#    PREFIX tbb
#    SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/tbb
#    CMAKE_ARGS
#      ${TBB_ARGS}
#    BUILD_ALWAYS OFF
#  )
#endif()

###########################################################################
# Open Image Denoise
if (PBRT_WITH_OIDN)
#include(ExternalProject)
  find_package (OpenImageDenoise 2.2.0 REQUIRED)
#  if (OIDN_FOUND)
#    message (STATUS "Found Open Image Denoise version ${OIDN_VERSION}")
#  else ()
#    message (STATUS "Open Image Denoise not found; building it from scratch.")
#    message(STATUS "ISPC_EXECUTABLE = ${ISPC_EXECUTABLE}")
#
#    if(NOT ISPC_EXECUTABLE)
#      set(GET_ISPC ON)
#      include(get_ispc)
#    endif()
#    set(OIDN_ARGS "")
#
#    #set(TBB_ROOT ${CMAKE_CURRENT_BINARY_DIR}/tbb)
#    #set(TBB_DIR ${CMAKE_CURRENT_BINARY_DIR}/tbb)
#    set(ISPC_EXECUTABLE ${ISPC_EXECUTABLE})
#    set(OIDN_APPS OFF)
#
#    message(STATUS "TBB_ROOT = ${TBB_ROOT}")
#    message(STATUS "TBB_DIR = ${TBB_DIR}")
#
#    set(OIDN_ARGS ${OIDN_ARGS}
#      -DCMAKE_INSTALL_PREFIX:PATH=${CMAKE_INSTALL_PREFIX}
#      -DCMAKE_PREFIX_PATH:PATH=${CMAKE_PREFIX_PATH}
#      -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
#      -DISPC_EXECUTABLE=${ISPC_EXECUTABLE}
#      -DOIDN_APPS=OFF
#      -DTBB_ROOT=${CMAKE_INSTALL_PREFIX}
#    )
#
#    if(APPLE AND (CMAKE_SYSTEM_PROCESSOR STREQUAL "arm64" OR CMAKE_OSX_ARCHITECTURES MATCHES "arm64"))
#      set(OIDN_DEVICE_METAL ON)
#      set(OIDN_ARGS ${OIDN_ARGS}
#        -DOIDN_DEVICE_METAL=ON
#      )
#    endif()
#
#    if(NOT APPLE)
#      find_package (CUDA)
#      if(CUDA_FOUND)
#        set(OIDN_DEVICE_CUDA ON)
#        set(OIDN_ARGS ${OIDN_ARGS}
#        -DOIDN_DEVICE_CUDA=ON
#      )
#      endif()
#    endif()
#
#    #add_subdirectory (oidn)
#    ExternalProject_Add(OIDN
#      PREFIX oidn
#      SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/oidn
#      BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR}/oidn
#      CMAKE_ARGS
#        ${OIDN_ARGS}
#      BUILD_ALWAYS OFF
#    )
#
#    ExternalProject_Add_StepDependencies(OIDN
#    configure
#      TBB
#      $<$<BOOL:${GET_ISPC}>:ispc>
#    )
#
#    #add_dependencies(OIDN tbb)
#    #add_dependencies(oidn TBB)
#    #set_property (TARGET OpenImageDenoise PROPERTY FOLDER "ext/oidn")
#
#    set (OIDN_INCLUDE_DIR
#      ${CMAKE_CURRENT_SOURCE_DIR}/oidn/include
#      PARENT_SCOPE
#    )
#
#    set (OIDN_LIBRARIES
#      openpgl
#      tbb
#      PARENT_SCOPE
#    )
#  endif()

endif()

###########################################################################
# Open PGL
# Integrate into the current build system
if (PBRT_WITH_PATH_GUIDING)
  # Build OpenPGL from source
  message (STATUS "Building OpenPGL from source...")

  set(OPENPGL_BUILD_STATIC OFF CACHE BOOL "Build OpenPGL as a static library" FORCE)
#  set(OPENPGL_TBB_ROOT ${CMAKE_INSTALL_PREFIX} CACHE PATH "Path to TBB" FORCE)
  set(OPENPGL_EF_RADIANCE_CACHES ON CACHE BOOL "Enable radiance caches" FORCE)
  set(OPENPGL_DIRECTION_COMPRESSION ON CACHE BOOL "Enable direction compression" FORCE)
  set(OPENPGL_RADIANCE_COMPRESSION ON CACHE BOOL "Enable radiance compression" FORCE)

  add_subdirectory (openpgl)
  add_library(OpenPGL ALIAS openpgl)  # Assuming openpgl creates a target named `openpgl`
  set_property (TARGET openpgl PROPERTY FOLDER "ext/openpgl")

  set (OPENPGL_INCLUDE_DIR
          ${CMAKE_CURRENT_SOURCE_DIR}/openpgl/openpgl/include
          PARENT_SCOPE
  )

  set (OPENPGL_LIBRARIES
          openpgl
          tbb
          PARENT_SCOPE
  )
endif()

# Treat OpenPGL as an external project
#if (PBRT_WITH_PATH_GUIDING)
#
#  find_package (openpgl)
#
#  if (OPENPGL_FOUND)
#    message (STATUS "Found OpenPGL version ${OPENPGL_VERSION}")
#  else ()
#    message (STATUS "OpenPGL not found; building it from scratch.")
#
#    set(OPENPGL_ARGS ${OPENPGL_ARGS}
#      -DCMAKE_INSTALL_PREFIX:PATH=${CMAKE_INSTALL_PREFIX}
#      -DCMAKE_PREFIX_PATH:PATH=${CMAKE_PREFIX_PATH}
#      -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
#      -DOPENPGL_BUILD_STATIC=OFF
#      -DOPENPGL_TBB_ROOT=${CMAKE_INSTALL_PREFIX}
#      -DOPENPGL_EF_RADIANCE_CACHES=ON
#      -DOPENPGL_DIRECTION_COMPRESSION=ON
#      -DOPENPGL_RADIANCE_COMPRESSION=ON
#    )
#
#    ExternalProject_Add(OpenPGL
#      PREFIX openpgl
#      SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/openpgl
#      CMAKE_ARGS
#        ${OPENPGL_ARGS}
#      BUILD_ALWAYS ON
#    )
#
#    ExternalProject_Add_StepDependencies(OpenPGL
#    configure
#      TBB
#      OIDN
#    )
#
#    #set(BUILD_STATIC ON CACHE BOOL "" FORCE)
#    #add_subdirectory (openpgl/superbuild)
#    #set_property (TARGET ep_openpgl PROPERTY FOLDER "ext/openpgl")
#
#    set (OPENPGL_INCLUDE_DIR
#      ${CMAKE_INSTALL_PREFIX}/include
#      PARENT_SCOPE
#    )
#
#    set (OPENPGL_LIBRARIES
#      openpgl
#      tbb
#      PARENT_SCOPE
#    )
#  endif()
#
#endif()
