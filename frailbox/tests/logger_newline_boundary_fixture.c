/*
 * Regression fixture for legacy logger line-boundary behavior.
 *
 * The legacy logger preserves caller-supplied trailing newlines and appends
 * one record boundary of its own. Truncated records must still end with a
 * newline so the following record does not get glued to the same physical line.
 */

#define _POSIX_C_SOURCE 200809L

#include "../include/logger.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int failures = 0;

#define CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            fprintf(stderr, "FAIL: %s\n", (msg)); \
            failures++; \
        } \
    } while (0)

static char *read_file(const char *path)
{
    FILE *fp = fopen(path, "rb");
    if (fp == NULL) {
        return NULL;
    }

    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return NULL;
    }

    long len = ftell(fp);
    if (len < 0) {
        fclose(fp);
        return NULL;
    }
    rewind(fp);

    char *buf = (char *)calloc((size_t)len + 1, 1);
    if (buf == NULL) {
        fclose(fp);
        return NULL;
    }

    size_t got = fread(buf, 1, (size_t)len, fp);
    fclose(fp);
    buf[got] = '\0';
    return buf;
}

static void write_boundary_cases(void)
{
    char long_payload[512];
    memset(long_payload, 'X', sizeof(long_payload) - 1);
    long_payload[sizeof(long_payload) - 1] = '\0';

    log_message(LOG_LEVEL_INFO, "fixture", 10,
                "case=no-newline payload=alpha");
    log_message(LOG_LEVEL_INFO, "fixture", 20,
                "case=one-newline payload=beta\n");
    log_message(LOG_LEVEL_INFO, "fixture", 30,
                "case=multi-newline payload=gamma\n\n");
    log_message(LOG_LEVEL_INFO, "fixture", 40,
                "case=over-limit payload=%s", long_payload);
    log_message(LOG_LEVEL_INFO, "fixture", 50,
                "case=after-boundary payload=omega");
}

static void assert_boundary_cases(const char *log_path)
{
    char *contents = read_file(log_path);
    CHECK(contents != NULL, "fixture log should be readable");
    if (contents == NULL) {
        return;
    }

    CHECK(strstr(contents, "case=no-newline payload=alpha\n") != NULL,
          "message without caller newline gets one record boundary");
    CHECK(strstr(contents, "case=one-newline payload=beta\n\n") != NULL,
          "single caller newline is preserved before record boundary");
    CHECK(strstr(contents, "case=multi-newline payload=gamma\n\n\n") != NULL,
          "multiple caller trailing newlines are preserved before record boundary");
    CHECK(strstr(contents, "... [TRUNCATED]\n [INFO] [newline-boundary]") != NULL,
          "truncated message ends with newline before the next record prefix");
    CHECK(strstr(contents, "... [TRUNCATED] [INFO] [newline-boundary]") == NULL,
          "truncated message must not join the next record on one line");
    CHECK(strstr(contents, "case=after-boundary payload=omega\n") != NULL,
          "message after truncated record is still emitted");

    free(contents);
}

int main(void)
{
    char log_path[] = "/tmp/frailbox_logger_newline_boundary_XXXXXX";
    int fd = mkstemp(log_path);
    if (fd < 0) {
        perror("mkstemp");
        return 1;
    }
    close(fd);

    setenv("LOG_FILE", log_path, 1);
    setenv("LOG_LEVEL", "none", 1);
    setenv("LOG_MODULE", "newline-boundary", 1);
    setenv("LOG_NO_TIMESTAMPS", "1", 1);
    unsetenv("LOG_SOURCE_INFO");

    if (log_init() != 0) {
        fprintf(stderr, "FAIL: log_init failed\n");
        unlink(log_path);
        return 1;
    }
    log_set_level(LOG_LEVEL_INFO);

    write_boundary_cases();
    log_shutdown();

    assert_boundary_cases(log_path);
    unlink(log_path);

    if (failures != 0) {
        fprintf(stderr, "logger newline boundary fixture failed: %d failures\n", failures);
        return 1;
    }

    printf("logger newline boundary fixture passed\n");
    return 0;
}
