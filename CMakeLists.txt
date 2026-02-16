cmake_minimum_required(VERSION 3.20)
project(GameWindow VERSION 0.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# ---- External dependencies ----

# GLFW submodule
add_subdirectory(vendor/glfw)

# GLAD (static)
add_library(glad STATIC
  vendor/glad/src/glad.c
)
target_include_directories(glad PUBLIC vendor/glad/include)

# GLM (header-only)
include_directories(vendor/glm)

# STB (header-only)
include_directories(vendor/stb)

# ---- App ----
add_executable(Sandbox src/sandbox_main.cpp src/noise.cpp)

target_link_libraries(Sandbox PRIVATE glad glfw ${CMAKE_DL_LIBS})

target_include_directories(Sandbox PRIVATE
  ${CMAKE_SOURCE_DIR}/vendor/glad/include
  ${CMAKE_SOURCE_DIR}/vendor/glm
  ${CMAKE_SOURCE_DIR}/vendor/stb
)

if (MSVC)
  target_compile_definitions(Sandbox PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
