CC=gcc
WARNINGS=-Wmissing-prototypes -Werror -Wextra -pedantic -Wall -Wswitch-enum
CFLAGS=-ggdb -std=c11
CFLAGS += $(WARNINGS)
RELEASE=-O3 -std=c11
RELEASE += $(WARNINGS)
DEFS=-D _POSIX_C_SOURCE=200809L
LFLAGS=-lm -lgc
TARGET=sly
CSOURCE=$(shell find -name "*.c")
OBJECTS=$(CSOURCE:%.c=%.o)

.PHONY: all deps test clean release release-test

all: deps $(TARGET)

test: $(TARGET)
	./$< test/*.sly

clean:
	rm -f $(OBJECTS) $(TARGET) $(TARGET).d

deps:
	@$(CC) -MM *.c > $(TARGET).d

$(OBJECTS):
	$(CC) -c $(CFLAGS) $(DEFS) -o $@ $(@:%.o=%.c)

$(TARGET): $(OBJECTS)
	$(CC) $(LFLAGS) -o $@ $^

release:
	$(CC) $(RELEASE) $(DEFS) -o $(TARGET) $(CSOURCE)

release-test: release
	./$(TARGET) test/*

-include $(TARGET).d
