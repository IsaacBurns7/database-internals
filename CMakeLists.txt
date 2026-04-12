# 1. Define the minimum version and project name
cmake_minimum_required(VERSION 3.15)
project(MyProject VERSION 1.0.0 LANGUAGES CXX)

# 2. Set the C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 3. Create an executable
# This tells CMake: "Take these files and make a program called 'MyApp'"
add_executable(Storage
	main.cpp
	storage/page.cpp
	storage/disk_manager.cpp
	storage/slotted_page.cpp
	storage/freelist_page.cpp
)

# 4. Define include directories
# 'PUBLIC' means anyone linking to MyApp also needs these headers.
# 'PRIVATE' means only MyApp needs them to compile.
target_include_directories(Storage PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
