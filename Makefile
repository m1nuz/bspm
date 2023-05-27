# Compiler and flags
CXX := g++
CXXFLAGS := -std=c++23 -Wall -Wextra

# Source files and output
SRCS := bspm.cpp
OBJS := $(addprefix build/,$(SRCS:.cpp=.o))
TARGET := build/bspm

# Build target
$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) $(OBJS) -o $(TARGET)

# Compile source files
build/%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Create build directory if it doesn't exist
$(shell mkdir -p build)

# Clean objects and executable
clean:
	rm -rf build

.PHONY: clean