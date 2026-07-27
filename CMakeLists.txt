cmake_minimum_required(VERSION 3.15)
project(MyProject VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- 1. Fetch GoogleTest ---
include(FetchContent)
if(POLICY CMP0135)
  cmake_policy(SET CMP0135 NEW)
endif()

FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/heads/main.zip
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# --- 2. Create the Storage Library (Now with Common!) ---
# Add your common source files here if they have .cpp files, 
# otherwise, they just need the include path below.
add_library(storage_logic
    storage/page.cpp
    storage/disk_manager.cpp
    storage/slotted_page.cpp
    storage/freelist_page.cpp
    storage/disk_scheduler.cpp
)

add_library(type_logic
	type/type.cpp
  type/column.cpp
  type/schema.cpp
  type/tuple.cpp
)

add_library(index_logic
	index/b_plus_tree.cpp
)

add_library(buffer_logic
    buffer/arc_replacer.cpp
)

# This is the "Magic Fix": It tells CMake that the project root 
# is where searches for "common/..." or "storage/..." should start.
target_include_directories(storage_logic PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(type_logic PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(index_logic PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/storage)
target_include_directories(buffer_logic PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
# --- 3. Main Executable ---
add_executable(storage_viz tests/storage_viz.cpp)
target_link_libraries(storage_viz PRIVATE storage_logic)

# --- 4. Testing Setup ---
enable_testing()
add_executable(storage_tests
	tests/storage_test.cpp
	tests/page.cpp
    tests/slotted_page.cpp
    tests/disk_manager.cpp
    tests/storage/disk_scheduler.cpp
)

add_executable(type_tests
  tests/type/boolean_type.cpp
  tests/type/column.cpp
  tests/type/schema.cpp
  tests/type/numeric_type.cpp
  tests/type/float_type.cpp
  tests/type/varchar_type.cpp
  tests/type/tuple.cpp
)

add_executable(index_tests
  tests/index/b_plus_tree.cpp
  tests/index/b_plus_tree_splits.cpp
)

add_executable(buffer_tests
  tests/buffer/arc_replacer.cpp
)

target_link_libraries(storage_tests
    PRIVATE
    gtest_main
    storage_logic
)

target_link_libraries(type_tests
  PRIVATE
  gtest_main
  type_logic
)

target_link_libraries(index_tests
  PRIVATE
  gtest_main
  index_logic
  storage_logic
  type_logic
)

target_link_libraries(buffer_tests
  PRIVATE
  gtest_main
  buffer_logic
)

include(GoogleTest)
gtest_discover_tests(storage_tests)
gtest_discover_tests(type_tests)
gtest_discover_tests(index_tests)
gtest_discover_tests(buffer_tests)
