CC=gcc
WARNINGS=-Wmissing-prototypes -Werror -Wextra -pedantic -Wall -Wswitch-enum
CFLAGS=-ggdb -std=c11
CFLAGS += $(WARNINGS)
RELEASE=-O3 -std=c11
RELEASE += $(WARNINGS)
DEFS=-D _POSIX_C_SOURCE=200809L
LFLAGS=-lm -lgc
TARGET=bin/sly
CSOURCE=$(shell find src/ -name "*.c")
OBJECTS=$(CSOURCE:src/%.c=bin/%.o)
DEPENDANCIES=$(CSOURCE:src/%.c=bin/%.d)

.PHONY: all test clean release

all: bin $(DEPENDANCIES) $(TARGET)

bin:
	@mkdir -p bin

test: all
	./$(TARGET) test/test.sly

clean:
	@rm -rf bin

$(DEPENDANCIES):
	@$(CC) -MM $(@:bin/%.d=src/%.c) -MT $(@:%.d=%.o) > $@

$(OBJECTS):
	$(CC) -c $(CFLAGS) $(DEFS) -o $@ $(@:bin/%.o=src/%.c)

release:
	$(CC) $(RELEASE) $(DEFS) -o $(TARGET) $(CSOURCE)

$(TARGET): $(OBJECTS)
	$(CC) $(LFLAGS) -o $@ $^

-include bin/*.d
