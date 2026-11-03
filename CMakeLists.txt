cmake_minimum_required(VERSION 3.21)
project(AesCaras VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# CommonLibSSE-NG (solo AE: el servidor usa Skyrim 1.6.1170)
set(ENABLE_SKYRIM_SE OFF CACHE BOOL "" FORCE)
set(ENABLE_SKYRIM_AE ON CACHE BOOL "" FORCE)
set(ENABLE_SKYRIM_VR OFF CACHE BOOL "" FORCE)
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(extern/CommonLibSSE-NG EXCLUDE_FROM_ALL)

find_package(spdlog CONFIG REQUIRED)

add_library(AesCaras SHARED src/main.cpp)
target_link_libraries(AesCaras PRIVATE CommonLibSSE::CommonLibSSE spdlog::spdlog)
target_compile_definitions(AesCaras PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
if (MSVC)
	target_compile_options(AesCaras PRIVATE /utf-8 /permissive- /Zc:preprocessor /EHsc)
endif()
