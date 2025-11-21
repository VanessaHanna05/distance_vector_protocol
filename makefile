CC      := gcc
CFLAGS  := -Wall -Wextra -pthread
TARGET  := part1
SRC     := part1.c

.PHONY: all build setup routes run-r1 run-r2 run-r3 run-all clean teardown reset

# Default target: build the program
all: build

build: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $< -o $@

# Set up namespaces, veths, bridge, and IP addresses (uses your existing script)
setup:
	chmod +x setup_routers.sh
	./setup_routers.sh

# Add broadcast routes for 255.255.255.255 inside each namespace
routes:
	sudo ip netns exec r1 ip route add 255.255.255.255 dev veth-r1 || true
	sudo ip netns exec r2 ip route add 255.255.255.255 dev veth-r2 || true
	sudo ip netns exec r3 ip route add 255.255.255.255 dev veth-r3 || true

# Run each router individually (use these in three separate terminals if you like)
run-r1: $(TARGET)
	sudo ip netns exec r1 ./$(TARGET)

run-r2: $(TARGET)
	sudo ip netns exec r2 ./$(TARGET)

run-r3: $(TARGET)
	sudo ip netns exec r3 ./$(TARGET)

# Run all three routers in one terminal using tmux (three panes)
# This uses sudo only once (for tmux), so you get a single password prompt.
run-all: build setup routes
	@which tmux >/dev/null 2>&1 || { echo "tmux is not installed. Install it with: sudo apt install tmux"; exit 1; }
	sudo tmux new-session -d -s routers 'ip netns exec r1 ./$(TARGET)' \; \
		split-window -h 'ip netns exec r2 ./$(TARGET)' \; \
		split-window -v 'ip netns exec r3 ./$(TARGET)' \; \
		select-layout tiled \; \
		attach

# Remove only the compiled binary
clean:
	rm -f $(TARGET)

# Tear down the virtual network (requires your cleanup_routers.sh)
teardown:
	chmod +x cleanup_routers.sh
	./cleanup_routers.sh

# Convenience: rebuild and reset everything
reset: clean teardown
