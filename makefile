CC      = gcc
CFLAGS  = -Wall -Wextra -pthread

TARGET          = part2
TARGET_TEST     = part2_test
ROUTER_CORE     = router_core
ROUTER_BONUS    = router_bonus
ROUTER_MAIN     = part3.c   # IMPORTANT FIX

all: $(ROUTER_CORE) $(ROUTER_BONUS) $(TARGET)

# --- Part 2 standalone test program ---

$(TARGET): part2.c
	$(CC) $(CFLAGS) -DTEST_PART2 part2.c -o $(TARGET)

$(TARGET_TEST): part2.c
	$(CC) $(CFLAGS) -DTEST_PART2 part2.c -o $(TARGET_TEST)

test: $(TARGET_TEST)

# --- Integrated routers (Part 2 + Part 3) ---

$(ROUTER_CORE): $(ROUTER_MAIN) part2.c
	$(CC) $(CFLAGS) $(ROUTER_MAIN) part2.c -o $(ROUTER_CORE)

$(ROUTER_BONUS): $(ROUTER_MAIN) part2_bonus.c
	$(CC) $(CFLAGS) $(ROUTER_MAIN) part2_bonus.c -o $(ROUTER_BONUS)

# --- Run with namespace testbeds ---

run_core: $(ROUTER_CORE)
	bash run_core.sh

run_bonus: $(ROUTER_BONUS)
	bash run_bonus.sh

clean:
	rm -f $(TARGET) $(TARGET_TEST) $(ROUTER_CORE) $(ROUTER_BONUS)

.PHONY: all test clean run_core run_bonus
