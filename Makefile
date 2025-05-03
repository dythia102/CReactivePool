# Compiler and flags
CC = gcc
CFLAGS = -Wall -Wextra -Iinclude
LDFLAGS = -pthread

# Add debug flags if DEBUG=1
ifeq ($(DEBUG),1)
CFLAGS += -g -DDEBUG
endif

# Source and object files
SRC = src/object_pool.c
OBJ = $(SRC:.c=.o)
COMMON_SRC = tests/common.c
COMMON_OBJ = $(COMMON_SRC:.c=.o)
EXAMPLE_SRC = examples/example_pool.c
EXAMPLE_OBJ = $(EXAMPLE_SRC:.c=.o)
EXAMPLE_BIN = bin/example_pool

# Find all test source files
TEST_SRCS = $(wildcard tests/test_*.c)
TEST_BINS = $(patsubst tests/%.c, bin/%, $(TEST_SRCS))

# Default target
all: $(EXAMPLE_BIN) $(TEST_BINS)

# Link example binary
$(EXAMPLE_BIN): $(OBJ) $(EXAMPLE_OBJ)
	$(CC) $(OBJ) $(EXAMPLE_OBJ) -o $@ $(LDFLAGS)

# Link each test binary
bin/test_%: tests/test_%.o $(OBJ) $(COMMON_OBJ)
	$(CC) $< $(OBJ) $(COMMON_OBJ) -o $@ $(LDFLAGS)

# Compile source to object files
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Compile common object
$(COMMON_OBJ): $(COMMON_SRC)
	$(CC) $(CFLAGS) -c $< -o $@

# Clean build artifacts
clean:
	rm -f $(OBJ) $(COMMON_OBJ) $(EXAMPLE_OBJ) tests/*.o bin/*

# New target to run all tests with Valgrind
valgrind-tests:
	@for test in bin/test_*; do \
		echo "Running $$test with Valgrind..."; \
		valgrind --leak-check=full ./$$test; \
	done

debug:
	$(MAKE) DEBUG=1 all
	
.PHONY: all clean