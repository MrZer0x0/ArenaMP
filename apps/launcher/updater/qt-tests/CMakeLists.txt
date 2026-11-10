cmake_minimum_required(VERSION 3.16)
project(ArenaUpdaterUiRegression LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/bin")
find_package(Qt5 5.10 REQUIRED COMPONENTS Widgets)
get_filename_component(ARENA_SOURCE_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../.." ABSOLUTE)

# This tiny standalone fixture is used only by the controller regression test.
# Production U006 uses the separately built native Qt arena-updater target.
add_executable(arena-updater worker_fixture.cpp)
add_executable(arena-updater-ui-test
    test_updatecontroller.cpp
    "${ARENA_SOURCE_ROOT}/apps/launcher/updatecontroller.cpp"
    "${ARENA_SOURCE_ROOT}/components/config/buildmanifest.cpp")
target_include_directories(arena-updater-ui-test PRIVATE "${ARENA_SOURCE_ROOT}")
target_link_libraries(arena-updater-ui-test PRIVATE Qt5::Widgets)
add_dependencies(arena-updater-ui-test arena-updater)

enable_testing()
add_test(NAME updater-ui COMMAND arena-updater-ui-test)
set_tests_properties(updater-ui PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 30)
