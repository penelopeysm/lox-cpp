CXX := clang++
CXXFLAGS := -std=c++20 -Wall -Wextra -Werror
TARGET := loxc

.PHONY: clean all

all: $(TARGET)

clean:
	rm -f $(OBJS) $(TARGET)

SRCS := src/main.cpp

OBJS := $(SRCS:.cpp=.o)
%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^
