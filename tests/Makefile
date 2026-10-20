CXX ?= g++
CXXFLAGS ?= -std=c++17 -O3 -Wall -Wextra -pthread -I../app/src/main/cpp/include
LDFLAGS ?= -ldl -lpthread

SRCS = test_engine.cpp \
       ../app/src/main/cpp/src/game_profiles.cpp \
       ../app/src/main/cpp/src/touch_input.cpp \
       ../app/src/main/cpp/src/audio_engine.cpp \
       ../app/src/main/cpp/src/renderer.cpp \
       ../app/src/main/cpp/src/core_host.cpp

TARGET = test_engine

all: $(TARGET)
	./$(TARGET)

$(TARGET): $(SRCS)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(SRCS) $(LDFLAGS)

clean:
	rm -f $(TARGET)

.PHONY: all clean
