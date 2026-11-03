cmake_minimum_required(VERSION 3.20)
project(octonion_fractals LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

find_package(Vulkan REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(GLFW REQUIRED glfw3)

add_executable(octonion_fractals src/main.cpp)
target_include_directories(octonion_fractals PRIVATE ${GLFW_INCLUDE_DIRS})
target_link_libraries(octonion_fractals PRIVATE Vulkan::Vulkan ${GLFW_LIBRARIES})
target_compile_options(octonion_fractals PRIVATE -Wall -Wextra -Wno-unused-parameter)

# Headless shader-pipeline probe — used to bisect driver hangs by attempting
# pipeline creation on one .frag.spv at a time under a wall-clock timeout.
add_executable(test_pipeline src/test_pipeline.cpp)
target_link_libraries(test_pipeline PRIVATE Vulkan::Vulkan)
target_compile_options(test_pipeline PRIVATE -Wall -Wextra)

# Compile shaders to SPIR-V into a sibling 'shaders/' next to the binary so
# the executable can load them with a simple relative path.
find_program(GLSLC glslc REQUIRED)

set(SHADER_DIR ${CMAKE_CURRENT_SOURCE_DIR}/shaders)
set(SHADER_OUT ${CMAKE_CURRENT_BINARY_DIR}/shaders)
file(MAKE_DIRECTORY ${SHADER_OUT})

# Pick up every shader (vert + all frag) automatically; rerun cmake when new ones land.
file(GLOB SHADER_PATHS CONFIGURE_DEPENDS
     ${SHADER_DIR}/*.vert ${SHADER_DIR}/*.frag)
set(SPV_FILES)
foreach(IN_PATH ${SHADER_PATHS})
    get_filename_component(NAME ${IN_PATH} NAME)
    set(OUT ${SHADER_OUT}/${NAME}.spv)
    add_custom_command(
        OUTPUT ${OUT}
        COMMAND ${GLSLC} -I ${SHADER_DIR} ${IN_PATH} -o ${OUT}
        DEPENDS ${IN_PATH} ${SHADER_DIR}/common.glsl ${SHADER_DIR}/font_data.glsl
        COMMENT "glslc ${NAME}"
        VERBATIM)
    list(APPEND SPV_FILES ${OUT})
endforeach()

add_custom_target(shaders ALL DEPENDS ${SPV_FILES})
add_dependencies(octonion_fractals shaders)
