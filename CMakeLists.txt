cmake_minimum_required(VERSION 3.16)
project(fm VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

find_package(Qt6 REQUIRED COMPONENTS Widgets)

add_executable(fm
  src/main.cpp
  src/MainWindow.cpp
  src/FileTab.cpp
  src/ThemeManager.cpp
  src/ThumbnailProxy.cpp
)
target_link_libraries(fm PRIVATE Qt6::Widgets)
target_compile_options(fm PRIVATE -Wall -Wextra)

include(CheckIPOSupported)
check_ipo_supported(RESULT ipo_ok OUTPUT ipo_msg)
if(ipo_ok AND CMAKE_BUILD_TYPE STREQUAL "Release")
  set_property(TARGET fm PROPERTY INTERPROCEDURAL_OPTIMIZATION ON)
endif()

install(TARGETS fm RUNTIME DESTINATION bin)
