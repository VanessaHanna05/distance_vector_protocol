CC      := gcc #this is added to know which c compiler to use so anywhere we use $(CC) means gcc
CFLAGS  := -Wall -Wextra -pthread # this is used to define compilation options where we are enabling with Wall and Wextra lots of wanring and pthread to enable thread safe environment
TARGET  := part1 # this is the name of the executable file 
SRC     := part1.c #this is the name of the source c file 

#.PHONY: all build setup routes run-r1 run-r2 run-r3 run-all clean teardown reset # we need all these to be treated as commands and not file names 

all: build # if we run make it will make all and if we run make build it will also do the same 

build: $(TARGET) #Running make build means make sure part1 exists and is up to date

$(TARGET): $(SRC) #to create part1, you need part1.c
	$(CC) $(CFLAGS) $< -o $@

#hmod +x setup_routers.sh Make the script executable.
#./setup_routers.sh Run the script that creates: namespaces r1, r2, r3 veth pairs bridge br0, IP addresses in each namespace

setup:
	chmod +x setup_routers.sh
	./setup_routers.sh

#sudo means that we  need root privileges
#ip netns exec r1 → run the command inside namespace r1
#ip route add 255.255.255.255 dev veth-r1 Add a routing rule for the global broadcast address 255.255.255.255
#to reach 255.255.255.255, send it out via veth-r1
routes:
	sudo ip netns exec r1 ip route add 255.255.255.255 dev veth-r1 
	sudo ip netns exec r2 ip route add 255.255.255.255 dev veth-r2 
	sudo ip netns exec r3 ip route add 255.255.255.255 dev veth-r3

# Run each router individually (use these in three separate terminals if you like)
run-r1: $(TARGET)
#Runs your program inside namespace r1
	sudo ip netns exec r1 ./$(TARGET) 

run-r2: $(TARGET)
#Runs your program inside namespace r2
	sudo ip netns exec r2 ./$(TARGET)

run-r3: $(TARGET)
#Runs your program inside namespace r3
	sudo ip netns exec r3 ./$(TARGET)

#References: https://man7.org/linux/man-pages/man1/tmux.1.html#COMMANDS

# Run all three routers in one terminal using tmux (three panes)
# This uses sudo only once (for tmux), so I get a single password prompt
run-all: build setup routes
	# Ensure tmux is installed; if not, show a message and abort
	@which tmux >/dev/null 2>&1 || { echo "tmux is not installed. Install it with: sudo apt install tmux"; exit 1; }

	# If a tmux session named 'routers' already exists, kill it so we can recreate it
	@sudo tmux has-session -t routers 2>/dev/null && sudo tmux kill-session -t routers 2>/dev/null || true

	# Create a new tmux session named 'routers', detached (-d), and run the three routers in separate panes
	sudo tmux new-session -d -s routers 'ip netns exec r1 ./$(TARGET)' \; 
		split-window -h 'ip netns exec r2 ./$(TARGET)' \; \
		split-window -v 'ip netns exec r3 ./$(TARGET)' \; \
		select-layout tiled \; \
		attach


# Remove only the compiled binary
clean:
	rm -f $(TARGET)

# Tear down 
teardown:
	chmod +x cleanup_routers.sh
	./cleanup_routers.sh

reset: clean teardown
