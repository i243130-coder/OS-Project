# ============================================================================
#  Makefile - Traffic Intersection Simulator Build System
# ============================================================================
#  Usage:
#    make          - Build the simulator (with SFML GUI)
#    make clean    - Remove build artifacts
#    make run      - Build and run with default 15 vehicles
#    make run20    - Build and run with 20 vehicles
#    make debug    - Build with debug symbols
# ============================================================================

CC       = gcc
CXX      = g++
CFLAGS   = -Wall -Wextra -std=gnu11
CXXFLAGS = -Wall -Wextra -std=c++17
LDFLAGS  = -lpthread -lrt -lsfml-graphics -lsfml-window -lsfml-system -lstdc++

# Source files
C_SRCS   = main.c vehicle.c parking.c controller.c display.c
CXX_SRCS = gui_display.cpp
C_OBJS   = $(C_SRCS:.c=.o)
CXX_OBJS = $(CXX_SRCS:.cpp=.o)
OBJS     = $(C_OBJS) $(CXX_OBJS)
HEADERS  = common.h vehicle.h parking.h controller.h display.h gui_display.h

# Output binary
TARGET   = traffic_sim

# ──────────── Targets ────────────

.PHONY: all clean run run20 debug

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)
	@echo ""
	@echo "  ✅ Build successful: ./$(TARGET)"
	@echo "  Usage: ./$(TARGET) [num_vehicles]  (default: 15)"
	@echo ""

%.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.cpp $(HEADERS)
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)
	@echo "  🧹 Cleaned build artifacts."

run: $(TARGET)
	./$(TARGET) 15

run20: $(TARGET)
	./$(TARGET) 20

debug: CFLAGS += -g -O0 -DDEBUG
debug: CXXFLAGS += -g -O0 -DDEBUG
debug: clean $(TARGET)
	@echo "  🐛 Debug build ready. Use: gdb ./$(TARGET)"
