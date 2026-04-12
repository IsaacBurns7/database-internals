# Compiler and Flags
CXX      := g++
CXXFLAGS := -Wall -Wextra -std=c++17 -I.
LDFLAGS  := 

# Target Executable
TARGET   := my_program

# Directories
SRC_DIRS := . common storage

# Find all .cpp files in the specified directories
SRCS := $(shell find $(SRC_DIRS) -maxdepth 1 -name "*.cpp")

# Define object files (e.g., common/utils.cpp -> obj/common/utils.o)
OBJ_DIR := obj
OBJS    := $(SRCS:%.cpp=$(OBJ_DIR)/%.o)

# Default Rule
all: $(TARGET)

# Link the executable
$(TARGET): $(OBJS)
	$(CXX) $(OBJS) -o $@ $(LDFLAGS)

# Compile source files into object files
$(OBJ_DIR)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Clean up build artifacts
clean:
	rm -rf $(OBJ_DIR) $(TARGET)

.PHONY: all clean
