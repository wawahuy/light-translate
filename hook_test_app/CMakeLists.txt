cmake_minimum_required(VERSION 3.14)
project(hook_test_app CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

include_directories(
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}/third_party/imgui-1.92.8"
    "${CMAKE_SOURCE_DIR}/third_party/imgui-1.92.8/backends"
)

set(IMGUI_SOURCES
    "${CMAKE_SOURCE_DIR}/third_party/imgui-1.92.8/imgui.cpp"
    "${CMAKE_SOURCE_DIR}/third_party/imgui-1.92.8/imgui_draw.cpp"
    "${CMAKE_SOURCE_DIR}/third_party/imgui-1.92.8/imgui_tables.cpp"
    "${CMAKE_SOURCE_DIR}/third_party/imgui-1.92.8/imgui_widgets.cpp"
    "${CMAKE_SOURCE_DIR}/third_party/imgui-1.92.8/backends/imgui_impl_win32.cpp"
    "${CMAKE_SOURCE_DIR}/third_party/imgui-1.92.8/backends/imgui_impl_dx11.cpp"
    "${CMAKE_SOURCE_DIR}/third_party/imgui-1.92.8/backends/imgui_impl_opengl3.cpp"
)

set(APP_SOURCES
    main.cpp
    dx11_renderer.cpp
    opengl_renderer.cpp
)

set(APP_HEADERS
    renderer_common.h
    dx11_renderer.h
    opengl_renderer.h
)

add_executable(hook_test_app WIN32 ${APP_SOURCES} ${APP_HEADERS} ${IMGUI_SOURCES})

set_target_properties(hook_test_app PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/Release"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/Debug"
)


target_link_libraries(hook_test_app PRIVATE
    d3d11
    dxgi
    d3dcompiler
    opengl32
    gdi32
    user32
    shell32
)

target_compile_definitions(hook_test_app PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)
