CC      = gcc
CFLAGS  = -Wall -Wextra -pthread

ROUTER_BONUS = router_bonus
ROUTER_MAIN  = part3.c

all: $(ROUTER_BONUS)

# Build the bonus router (Part3 + part2_bonus.c)
$(ROUTER_BONUS): $(ROUTER_MAIN) part2_bonus.c
	$(CC) $(CFLAGS) $(ROUTER_MAIN) part2_bonus.c -o $(ROUTER_BONUS)

# Convenience target to run the namespace testbed
run_bonus: $(ROUTER_BONUS)
	bash run_bonus.sh

clean:
	rm -f $(ROUTER_BONUS)

.PHONY: all clean run_bonus
