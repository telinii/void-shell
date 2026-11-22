CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -Iinclude
LDLIBS = -lreadline -lm
TARGET = void-shell

SRCS = $(wildcard src/*.c)
OBJS = $(SRCS:src/%.c=build/%.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

build/%.o: src/%.c
	@mkdir -p build
	$(CC) $(CFLAGS) -c $< -o $@

TERM_DIR = term

# Terminal embutido (precisa de GTK4 + VTE para GTK4).
term:
	$(MAKE) -C $(TERM_DIR)
	cp -f $(TERM_DIR)/void-term ./void-term

bundle: $(TARGET) term

run: $(TARGET)
	./$(TARGET)

run-term: term
	./void-term

clean:
	rm -rf build $(TARGET) void-term
	-$(MAKE) -C $(TERM_DIR) clean

.PHONY: all clean run term bundle run-term
