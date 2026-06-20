CC = gcc
CFLAGS = -Wall -Wextra -Wpedantic -std=c2x -O2 -g
CFLAGS += -D_FORTIFY_SOURCE=3
CFLAGS += -fstack-protector-strong
CFLAGS += -fPIE
LDFLAGS = -pie -z relro -z now

SRCDIR = src
INCDIR = include
BUILDDIR = build

SRCS = $(wildcard $(SRCDIR)/*.c) main.c
OBJS = $(patsubst %.c, $(BUILDDIR)/%.o, $(SRCS))
DEPS = $(OBJS:.o=.d)

TARGET = frailbox
SELFTEST_TARGET = $(BUILDDIR)/frailbox_selftest

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -I$(INCDIR) $^ -o $@ $(LDFLAGS)

$(BUILDDIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -I$(INCDIR) -MMD -MP -c $< -o $@

$(SELFTEST_TARGET): tests/selftest_summary.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -I$(INCDIR) $< -o $@ $(LDFLAGS)

-include $(DEPS)

clean:
	rm -rf $(BUILDDIR) $(TARGET)

distclean: clean
	rm -rf *.o *.d

test: $(TARGET)
	./$(TARGET) --sandbox-type seccomp --memory-limit 64 --verbose

selftest: $(SELFTEST_TARGET)
	@./$(SELFTEST_TARGET) --fixture tests/fixtures/selftest_success.fixture

selftest-json: $(SELFTEST_TARGET)
	@./$(SELFTEST_TARGET) --json --fixture tests/fixtures/selftest_success.fixture

selftest-json-fixtures: $(SELFTEST_TARGET)
	@./$(SELFTEST_TARGET) --json --fixture tests/fixtures/selftest_success.fixture >/tmp/frailbox-selftest-success.json
	@set +e; ./$(SELFTEST_TARGET) --json --fixture tests/fixtures/selftest_failure.fixture >/tmp/frailbox-selftest-failure.json; status=$$?; if [ "$$status" -ne 1 ]; then exit 1; fi
	@grep -q '"status": "pass"' /tmp/frailbox-selftest-success.json
	@grep -q '"status": "fail"' /tmp/frailbox-selftest-failure.json

valgrind: $(TARGET)
	valgrind --leak-check=full --show-leak-kinds=all ./$(TARGET)

.PHONY: all clean distclean test selftest selftest-json selftest-json-fixtures valgrind
