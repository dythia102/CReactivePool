CC = gcc
CFLAGS = -Wall -Wextra -Iinclude
LDFLAGS = -luv

# Source and object files
SRC = src/object_pool.c
OBJ = $(SRC:.c=.o)

EXAMPLE_SRC = examples/example_pool.c
EXAMPLE_OBJ = $(EXAMPLE_SRC:.c=.o)
EXAMPLE_BIN = bin/example_pool

TEST_SRC = tests/test_object_pool.c
TEST_OBJ = $(TEST_SRC:.c=.o)
TEST_BIN = bin/test_object_pool

# Default target
all: $(EXAMPLE_BIN) $(TEST_BIN)

# Link example binary
$(EXAMPLE_BIN): $(OBJ) $(EXAMPLE_OBJ)
	$(CC) $(OBJ) $(EXAMPLE_OBJ) -o $@ $(LDFLAGS)

# Link test binary
$(TEST_BIN): $(OBJ) $(TEST_OBJ)
	$(CC) $(OBJ) $(TEST_OBJ) -o $@ $(LDFLAGS)

# Compile source to object files
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Clean build artifacts
clean:
	rm -f $(OBJ) $(EXAMPLE_OBJ) $(TEST_OBJ) $(EXAMPLE_BIN) $(TEST_BIN)

.PHONY: all clean