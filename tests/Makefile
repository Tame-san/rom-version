CXX ?= g++
CXXFLAGS ?= -std=c++17 -O3 -Wall -Wextra -pthread -I../core-native/src/main/cpp/include
LDFLAGS ?= -ldl -lpthread

SRCS = test_engine.cpp \
       ../core-native/src/main/cpp/src/game_profiles.cpp \
       ../core-native/src/main/cpp/src/touch_input.cpp \
       ../core-native/src/main/cpp/src/audio_engine.cpp \
       ../core-native/src/main/cpp/src/renderer.cpp \
       ../core-native/src/main/cpp/src/core_host.cpp

TARGET = test_engine

all: $(TARGET)
	./$(TARGET)

$(TARGET): $(SRCS)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(SRCS) $(LDFLAGS)

clean:
	rm -f $(TARGET)

.PHONY: all clean
