CXX ?= g++
CXXFLAGS ?= -std=c++20 -Wall -Wextra -Wpedantic -O2
LDFLAGS ?=

SRC_DIR := src
BUILD_DIR := build
BIN_DIR := bin
TARGET := $(BIN_DIR)/upstreamd

SOURCES := $(wildcard $(SRC_DIR)/*.cpp)
OBJECTS := $(patsubst $(SRC_DIR)/%.cpp,$(BUILD_DIR)/%.o,$(SOURCES))
DEPS := $(OBJECTS:.o=.d)

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJECTS) | $(BIN_DIR)
	$(CXX) $(OBJECTS) -o $@ $(LDFLAGS)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

clean:
	rm -rf $(BUILD_DIR) $(BIN_DIR)

-include $(DEPS)
