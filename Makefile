CC = gcc
CFLAGS = -Wall -Wextra -Iinclude
LDFLAGS =

SRC = src/object_pool.c
OBJ = $(SRC:.c=.o)

EXAMPLE_SRC = examples/test_pool.c
EXAMPLE_OBJ = $(EXAMPLE_SRC:.c=.o)
EXAMPLE_BIN = test_pool

TEST_SRC = tests/test_pool.c
TEST_OBJ = $(TEST_SRC:.c=.o)
TEST_BIN = test_pool_tests

all: $(EXAMPLE_BIN) $(TEST_BIN)

$(EXAMPLE_BIN): $(OBJ) $(EXAMPLE_OBJ)
	$(CC) $(OBJ) $(EXAMPLE_OBJ) -o $@ $(LDFLAGS)

$(TEST_BIN): $(OBJ) $(TEST_OBJ)
	$(CC) $(OBJ) $(TEST_OBJ) -o $@ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ) $(EXAMPLE_OBJ) $(TEST_OBJ) $(EXAMPLE_BIN) $(TEST_BIN)

.PHONY: all clean