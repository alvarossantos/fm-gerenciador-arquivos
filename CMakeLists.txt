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

include(CTest)
if(BUILD_TESTING)
  find_package(Qt6 REQUIRED COMPONENTS Test)
  add_executable(theme_manager_test tests/theme_manager_test.cpp src/ThemeManager.cpp)
  target_include_directories(theme_manager_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
  target_link_libraries(theme_manager_test PRIVATE Qt6::Widgets Qt6::Test)
  add_test(NAME theme_manager_test COMMAND theme_manager_test)
  set_tests_properties(theme_manager_test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
  add_test(NAME bench_smoke COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/bench_smoke.sh $<TARGET_FILE:fm>)
  add_executable(filetab_test tests/filetab_test.cpp src/FileTab.cpp src/ThumbnailProxy.cpp)
  target_include_directories(filetab_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
  target_link_libraries(filetab_test PRIVATE Qt6::Widgets Qt6::Test)
  add_test(NAME filetab_test COMMAND filetab_test)
  set_tests_properties(filetab_test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
  add_executable(mainwindow_test tests/mainwindow_test.cpp src/MainWindow.cpp src/FileTab.cpp src/ThemeManager.cpp src/ThumbnailProxy.cpp)
  target_include_directories(mainwindow_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
  target_link_libraries(mainwindow_test PRIVATE Qt6::Widgets Qt6::Test)
  add_test(NAME mainwindow_test COMMAND mainwindow_test)
  set_tests_properties(mainwindow_test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()
