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
LOGGER_RETENTION_FIXTURE = $(BUILDDIR)/logger_retention_report_fixture

TARGET = frailbox

.PHONY: all clean logger-retention-fixtures

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -I$(INCDIR) $^ -o $@ $(LDFLAGS)

$(BUILDDIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -I$(INCDIR) -MMD -MP -c $< -o $@

-include $(DEPS)

clean:
	rm -rf $(BUILDDIR) $(TARGET)

distclean: clean
	rm -rf *.o *.d

$(LOGGER_RETENTION_FIXTURE): tests/logger_retention_report_fixture.c $(SRCDIR)/logger.c $(INCDIR)/logger.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -I$(INCDIR) $< $(SRCDIR)/logger.c -o $@ $(LDFLAGS) -pthread

logger-retention-fixtures: $(LOGGER_RETENTION_FIXTURE)
	./$(LOGGER_RETENTION_FIXTURE)

test: $(TARGET) logger-retention-fixtures
	./$(TARGET) --sandbox-type seccomp --memory-limit 64 --verbose

valgrind: $(TARGET)
	valgrind --leak-check=full --show-leak-kinds=all ./$(TARGET)

.PHONY: all clean distclean logger-retention-fixtures test valgrind
