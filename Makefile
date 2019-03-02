.POSIX:
.PHONY: all clean run

MK_BUILD_DIR = mkdir -p build

include objects.mk

DEPS_CFLAGS = `pkg-config --cflags glew glfw3`
DEPS_LIBS = `pkg-config --libs glew glfw3`
LIBS := $(DEPS_LIBS) -lpthread -lm

CFLAGS := -Wall -Wextra -pedantic -g -std=c99 -Og $(DEPS_CFLAGS)

all: craft
craft: $(OBJECT_FILES)
	$(CC) -o $@ $(OBJECT_FILES) $(LIBS)

clean:
	rm -rf build

run: craft
	./craft localhost

include deps.mk

