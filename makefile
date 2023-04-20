CC=gcc
CFLAGS=-Werror -Wextra -pedantic -Wall -Wswitch-enum -ggdb -std=c11
RELEASE=-O3 -Werror -Wextra -pedantic -Wall -Wswitch-enum -std=c11
DEFS=-DUSE_SLY_ALLOC -DNO_READLINE
LFLAGS=
TARGET=sly
CSOURCE=$(shell find -name "*.c")
OBJECTS=$(CSOURCE:%.c=%.o)

.PHONY: all deps test clean release release-test

all: deps $(TARGET)

test: $(TARGET)
	./$< test/*

clean:
	rm -f $(OBJECTS) $(TARGET) $(TARGET).d

deps:
	@$(CC) -MM *.c > $(TARGET).d

$(OBJECTS):
	$(CC) -c $(CFLAGS) $(DEFS) -o $@ $(@:%.o=%.c)

$(TARGET): $(OBJECTS)
	$(CC) -o $@ $^

release:
	$(CC) $(RELEASE) $(DEFS) -o $(TARGET) $(CSOURCE)

release-test: release
	./$(TARGET) test/*

-include $(TARGET).d
