```makefile
CC = gcc
CFLAGS = -Wall -Wextra -pthread
TARGET = part2
TARGET_TEST = part2_test

all: $(TARGET)

$(TARGET): part2.c
	$(CC) $(CFLAGS) part2.c -o $(TARGET)

test: part2.c
	$(CC) $(CFLAGS) -DTEST_PART2 part2.c -o $(TARGET_TEST)

clean:
	rm -f $(TARGET) $(TARGET_TEST)
