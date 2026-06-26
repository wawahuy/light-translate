cmake_minimum_required(VERSION 3.14)
project(light_translate CXX)

set(APP_NAME "light_translate")

if(NOT DEFINED APP_VERSION)
  set(APP_VERSION "v0.0.0-dev")
endif()
add_compile_definitions(APP_VERSION="${APP_VERSION}")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

option(WITH_MKL        "Compile demo with MKL/OpenBlas support, default use MKL."       ON)
option(WITH_STATIC_LIB "Compile demo with static/shared library, default use static."   ON)
option(USE_FREETYPE "Enable FreeType support" OFF)

SET(PADDLE_LIB "" CACHE PATH "Location of libraries")
SET(OPENCV_DIR "" CACHE PATH "Location of libraries")

macro(safe_set_static_flag)
    foreach(flag_var
        CMAKE_CXX_FLAGS CMAKE_CXX_FLAGS_DEBUG CMAKE_CXX_FLAGS_RELEASE
        CMAKE_CXX_FLAGS_MINSIZEREL CMAKE_CXX_FLAGS_RELWITHDEBINFO)
      if(${${flag_var}} MATCHES "/MD")
        string(REGEX REPLACE "/MD" "/MT" ${flag_var} "${${flag_var}}")
      endif()
    endforeach(flag_var)
endmacro()

if (WITH_MKL)
    ADD_DEFINITIONS(-DUSE_MKL)
endif()

if(NOT DEFINED PADDLE_LIB)
  message(FATAL_ERROR "please set PADDLE_LIB with -DPADDLE_LIB=/path/paddle/lib")
endif()

if(NOT DEFINED OPENCV_DIR)
    message(FATAL_ERROR "please set OPENCV_DIR with -DOPENCV_DIR=/path/opencv")
endif()


if (WIN32)
  include_directories("${PADDLE_LIB}/paddle/include")
  link_directories("${PADDLE_LIB}/paddle/lib")
  set(CMAKE_CONFIGURATION_TYPES "Debug;Release" CACHE STRING "" FORCE)
  set(OpenCV_DIR "${OPENCV_DIR}/x64/vc16/lib") 
  find_package(OpenCV REQUIRED )
  if(USE_FREETYPE)
    if(NOT "opencv_freetype" IN_LIST OpenCV_LIBS)
      message(FATAL_ERROR "OpenCV was not compiled with the freetype module (opencv_freetype) !")
    endif()
    add_definitions(-DUSE_FREETYPE)
  endif()  

else ()
  set(OpenCV_DIR "${OPENCV_DIR}/lib64/cmake/opencv4") 
  find_package(OpenCV REQUIRED)
  if(USE_FREETYPE)
    if(NOT "opencv_freetype" IN_LIST OpenCV_LIBS)
      message(FATAL_ERROR "OpenCV was not compiled with the freetype module (opencv_freetype) !")
    endif()
    add_definitions(-DUSE_FREETYPE)
  endif()  

  include_directories("${PADDLE_LIB}/paddle/include")
  link_directories("${PADDLE_LIB}/paddle/lib")
endif ()
include_directories(${OpenCV_INCLUDE_DIRS})

if (WIN32)
    add_definitions("/DGOOGLE_GLOG_DLL_DECL=")
    if(WITH_MKL)
        set(FLAG_OPENMP "/openmp")
    endif()
    if(MSVC)
        add_compile_options(/utf-8)
    endif()
    set(CMAKE_C_FLAGS_DEBUG   "${CMAKE_C_FLAGS_DEBUG} /bigobj /MTd ${FLAG_OPENMP}")
    set(CMAKE_C_FLAGS_RELEASE  "${CMAKE_C_FLAGS_RELEASE} /bigobj /MT ${FLAG_OPENMP}")
    set(CMAKE_CXX_FLAGS_DEBUG  "${CMAKE_CXX_FLAGS_DEBUG} /bigobj /MTd ${FLAG_OPENMP}")
    set(CMAKE_CXX_FLAGS_RELEASE   "${CMAKE_CXX_FLAGS_RELEASE} /bigobj /MT ${FLAG_OPENMP}")
    if (WITH_STATIC_LIB)
        safe_set_static_flag()
        add_definitions(-DSTATIC_LIB)
	add_definitions(-DYAML_CPP_STATIC_DEFINE)
    endif()
    message("cmake c debug flags " ${CMAKE_C_FLAGS_DEBUG})
    message("cmake c release flags " ${CMAKE_C_FLAGS_RELEASE})
    message("cmake cxx debug flags " ${CMAKE_CXX_FLAGS_DEBUG})
    message("cmake cxx release flags " ${CMAKE_CXX_FLAGS_RELEASE})
else()
    if(WITH_MKL)
        set(FLAG_OPENMP "-fopenmp")
    endif()
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -O3 ${FLAG_OPENMP} -std=c++11")
    set(CMAKE_STATIC_LIBRARY_PREFIX "")
    message("cmake cxx flags" ${CMAKE_CXX_FLAGS})
endif()

include_directories("${PADDLE_LIB}/third_party/install/protobuf/include")
include_directories("${PADDLE_LIB}/third_party/install/glog/include")
include_directories("${PADDLE_LIB}/third_party/install/gflags/include")
include_directories("${PADDLE_LIB}/third_party/install/xxhash/include")
include_directories("${PADDLE_LIB}/third_party/install/zlib/include")
include_directories("${PADDLE_LIB}/third_party/install/onnxruntime/include")
include_directories("${PADDLE_LIB}/third_party/install/paddle2onnx/include")
include_directories("${PADDLE_LIB}/third_party/install/yaml-cpp/include")
include_directories("${PADDLE_LIB}/third_party/install/openvino/include")
include_directories("${PADDLE_LIB}/third_party/install/tbb/include")
include_directories("${PADDLE_LIB}/third_party/boost")
include_directories("${PADDLE_LIB}/third_party/eigen3")
include_directories("${PADDLE_LIB}/paddle/include/") 
include_directories("${CMAKE_SOURCE_DIR}/")

link_directories("${PADDLE_LIB}/third_party/install/zlib/lib")
link_directories("${PADDLE_LIB}/third_party/install/protobuf/lib")
link_directories("${PADDLE_LIB}/third_party/install/glog/lib")
link_directories("${PADDLE_LIB}/third_party/install/gflags/lib")
link_directories("${PADDLE_LIB}/third_party/install/xxhash/lib")
link_directories("${PADDLE_LIB}/third_party/install/onnxruntime/lib")
link_directories("${PADDLE_LIB}/third_party/install/paddle2onnx/lib")
link_directories("${PADDLE_LIB}/third_party/install/yaml-cpp/lib")
link_directories("${PADDLE_LIB}/third_party/install/openvino/intel64")
link_directories("${PADDLE_LIB}/third_party/install/tbb/lib")
link_directories("${PADDLE_LIB}/paddle/lib")

if(WITH_MKL)
  include_directories("${PADDLE_LIB}/third_party/install/mklml/include")
  if (WIN32)
    set(MATH_LIB ${PADDLE_LIB}/third_party/install/mklml/lib/mklml.lib
            ${PADDLE_LIB}/third_party/install/mklml/lib/libiomp5md.lib)
  else ()
    set(MATH_LIB ${PADDLE_LIB}/third_party/install/mklml/lib/libmklml_intel${CMAKE_SHARED_LIBRARY_SUFFIX}
            ${PADDLE_LIB}/third_party/install/mklml/lib/libiomp5${CMAKE_SHARED_LIBRARY_SUFFIX})
    execute_process(COMMAND cp -r ${PADDLE_LIB}/third_party/install/mklml/lib/libmklml_intel${CMAKE_SHARED_LIBRARY_SUFFIX} /usr/lib)
  endif ()
  set(MKLDNN_PATH "${PADDLE_LIB}/third_party/install/onednn")
  if(EXISTS ${MKLDNN_PATH})
    include_directories("${MKLDNN_PATH}/include")
    if (WIN32)
      set(MKLDNN_LIB ${MKLDNN_PATH}/lib/mkldnn.lib)
    else ()
      set(MKLDNN_LIB ${MKLDNN_PATH}/lib/libdnnl.so.3)
    endif ()
  endif()
else()
  if (WIN32)
    set(MATH_LIB ${PADDLE_LIB}/third_party/install/openblas/lib/openblas${CMAKE_STATIC_LIBRARY_SUFFIX})
  else ()
    set(MATH_LIB ${PADDLE_LIB}/third_party/install/openblas/lib/libopenblas${CMAKE_STATIC_LIBRARY_SUFFIX})
  endif ()
endif()

# Note: libpaddle_inference_api.so/a must put before libpaddle_inference.so/a
if(WITH_STATIC_LIB)
  if(WIN32)
    set(DEPS
        ${PADDLE_LIB}/paddle/lib/paddle_inference${CMAKE_STATIC_LIBRARY_SUFFIX})
  else()
    set(DEPS
        ${PADDLE_LIB}/paddle/lib/libpaddle_inference${CMAKE_STATIC_LIBRARY_SUFFIX})
  endif()
else()
  if(WIN32)
    set(DEPS
        ${PADDLE_LIB}/paddle/lib/paddle_inference${CMAKE_SHARED_LIBRARY_SUFFIX})
  else()
    set(DEPS
        ${PADDLE_LIB}/paddle/lib/libpaddle_inference${CMAKE_SHARED_LIBRARY_SUFFIX})
  endif()
endif(WITH_STATIC_LIB)

if (NOT WIN32)
    set(DEPS ${DEPS}
        ${MATH_LIB} ${MKLDNN_LIB}
        glog gflags protobuf z xxhash
        )
    if(EXISTS "${PADDLE_LIB}/third_party/install/snappystream/lib")
        set(DEPS ${DEPS} snappystream)
    endif()
    if (EXISTS "${PADDLE_LIB}/third_party/install/snappy/lib")
        set(DEPS ${DEPS} snappy)
    endif()
else()
    set(DEPS ${DEPS}
        ${MATH_LIB} ${MKLDNN_LIB}
        glog gflags_static libprotobuf xxhash)
    set(DEPS ${DEPS} libcmt shlwapi)
    if (EXISTS "${PADDLE_LIB}/third_party/install/snappy/lib")
        set(DEPS ${DEPS} snappy)
    endif()
    if(EXISTS "${PADDLE_LIB}/third_party/install/snappystream/lib")
        set(DEPS ${DEPS} snappystream)
    endif()
endif(NOT WIN32)

if (EXISTS "${PADDLE_LIB}/third_party/install/yaml-cpp/lib")
  set(DEPS ${DEPS} yaml-cpp)
endif()

if (NOT WIN32)
    set(EXTERNAL_LIB "-ldl -lrt -lgomp -lz -lm -lpthread")
    set(DEPS ${DEPS} ${EXTERNAL_LIB})
endif()

set(THIRD_PARTY_PATH ${CMAKE_CURRENT_LIST_DIR}/third_party)
function(download_and_decompress url filename decompress_dir)
  if(NOT EXISTS "${filename}" AND NOT EXISTS "${decompress_dir}")
    message("Downloading file from ${url} to ${filename} ...")
    file(DOWNLOAD ${url} "${filename}.tmp" SHOW_PROGRESS)
    file(RENAME "${filename}.tmp" ${filename})
  endif()
  if(NOT EXISTS ${decompress_dir})
    file(MAKE_DIRECTORY ${decompress_dir})
    message("Decompress file ${filename} ...")
    execute_process(COMMAND ${CMAKE_COMMAND} -E tar -xf ${filename} WORKING_DIRECTORY ${decompress_dir})
  endif()
endfunction()

add_subdirectory(third_party/abseil-cpp)
add_subdirectory(third_party/clipper_ver6.4.2/cpp)
include_directories(${POLYCLIPPING_INCLUDE_DIR})
include_directories("${CMAKE_SOURCE_DIR}/third_party/imgui-1.92.8")
include_directories("${CMAKE_SOURCE_DIR}/third_party/imgui-1.92.8/backends")

set(DEPS ${DEPS} ${OpenCV_LIBS})
set(DEPS ${DEPS} absl::statusor)
set(DEPS ${DEPS} polyclipping)

if(UNIX)
  find_package(Iconv REQUIRED) 
endif()

file(GLOB_RECURSE HEADER_LIST
    "${CMAKE_SOURCE_DIR}/src/*.h"
    "${CMAKE_SOURCE_DIR}/src/*.hpp"
)

list(FILTER HEADER_LIST EXCLUDE REGEX ".*/src/paddleocr/.*")

file(GLOB_RECURSE SRC_LIST
  "./src/*.cc"
  "./src/*.cpp"
) 
list(FILTER SRC_LIST EXCLUDE REGEX ".*/src/updater/.*")
set(SRCS main.cpp)

set(IMGUI_SRC_LIST
  "${CMAKE_SOURCE_DIR}/third_party/imgui-1.92.8/imgui.cpp"
  "${CMAKE_SOURCE_DIR}/third_party/imgui-1.92.8/imgui_draw.cpp"
  "${CMAKE_SOURCE_DIR}/third_party/imgui-1.92.8/imgui_tables.cpp"
  "${CMAKE_SOURCE_DIR}/third_party/imgui-1.92.8/imgui_widgets.cpp"
  "${CMAKE_SOURCE_DIR}/third_party/imgui-1.92.8/backends/imgui_impl_win32.cpp"
  "${CMAKE_SOURCE_DIR}/third_party/imgui-1.92.8/backends/imgui_impl_dx11.cpp"
)

add_executable(${APP_NAME} WIN32 ${SRCS} ${SRC_LIST} ${HEADER_LIST} ${IMGUI_SRC_LIST})
target_link_libraries(${APP_NAME} ${DEPS}
  user32
  gdi32
  gdiplus
  comctl32
  comdlg32
  winhttp
  winmm
  windowsapp
  d3d11
  dxgi
)
target_compile_definitions(${APP_NAME} PRIVATE
  UNICODE
  _UNICODE
  WIN32_LEAN_AND_MEAN
  NOMINMAX
)
target_sources(${APP_NAME} PRIVATE
  light_translate.rc
)
source_group(
  TREE "${CMAKE_SOURCE_DIR}"
  FILES
    ${SRCS}
    ${SRC_LIST}
    ${HEADER_LIST}
    light_translate.rc
)

add_executable(updater WIN32 src/updater/main.cpp)
target_link_libraries(updater user32)

if (WIN32 AND WITH_MKL)
    add_custom_command(TARGET ${APP_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${PADDLE_LIB}/third_party/install/mklml/lib/mklml.dll ./mklml.dll
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${PADDLE_LIB}/third_party/install/mklml/lib/libiomp5md.dll ./libiomp5md.dll
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${PADDLE_LIB}/third_party/install/onednn/lib/mkldnn.dll ./mkldnn.dll
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${PADDLE_LIB}/third_party/install/mklml/lib/mklml.dll ./release/mklml.dll
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${PADDLE_LIB}/third_party/install/mklml/lib/libiomp5md.dll ./release/libiomp5md.dll
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${PADDLE_LIB}/third_party/install/onednn/lib/mkldnn.dll ./release/mkldnn.dll
    )
endif()
