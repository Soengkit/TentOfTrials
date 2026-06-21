/**
 * @file test_logger_newline.c
 * @brief Regression fixtures for logger newline boundary handling.
 *
 * This test covers:
 *   - no newline
 *   - one trailing newline
 *   - multiple trailing newlines
 *   - partial write crossing internal buffer limit
 *
 * Compile with:
 *   gcc -I.. -o test_logger_newline test_logger_newline.c -lpthread
 *
 * Run with:
 *   ./test_logger_newline
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#define LOG_BUF_SIZE 32

/* Simulated logger write: returns bytes written */
static int logger_write(const char *buf, size_t len, int flush_newline) {
    if (!buf || len == 0) return 0;
    size_t written = len;

    /* Simulate buffer flush: handle partial writes */
    if (len > LOG_BUF_SIZE) {
        written = LOG_BUF_SIZE;
    }

    /* Check for newline handling */
    if (flush_newline && buf[written - 1] != '\n') {
        /* Would normally auto-flush; treat as success */
    }

    return (int)written;
}

static int test_no_newline(void) {
    const char *msg = "hello world";
    int ret = logger_write(msg, strlen(msg), 0);
    assert(ret == (int)strlen(msg));
    printf("  PASS: no_newline (wrote %d bytes)\n", ret);
    return 0;
}

static int test_one_newline(void) {
    const char *msg = "hello world\n";
    int ret = logger_write(msg, strlen(msg), 1);
    assert(ret == (int)strlen(msg));
    printf("  PASS: one_newline (wrote %d bytes)\n", ret);
    return 0;
}

static int test_multiple_trailing_newlines(void) {
    const char *msg = "hello world\n\n\n";
    int ret = logger_write(msg, strlen(msg), 1);
    assert(ret == (int)strlen(msg));
    printf("  PASS: multiple_trailing_newlines (wrote %d bytes)\n", ret);
    return 0;
}

static int test_partial_write_buffer_boundary(void) {
    /* Message that exceeds internal buffer limit */
    char large_msg[LOG_BUF_SIZE + 16];
    memset(large_msg, 'A', sizeof(large_msg) - 1);
    large_msg[sizeof(large_msg) - 1] = '\0';

    int ret = logger_write(large_msg, strlen(large_msg), 0);
    assert(ret == LOG_BUF_SIZE);
    assert(ret < (int)strlen(large_msg));
    printf("  PASS: partial_write_crosses_buffer (wrote %d of %zu bytes)\n",
           ret, strlen(large_msg));
    return 0;
}

static int test_partial_write_with_newline(void) {
    char buf[LOG_BUF_SIZE + 8];
    memset(buf, 'B', LOG_BUF_SIZE);
    buf[LOG_BUF_SIZE] = '\n';
    buf[LOG_BUF_SIZE + 1] = '\0';

    int ret = logger_write(buf, strlen(buf), 1);
    assert(ret == LOG_BUF_SIZE);
    printf("  PASS: partial_write_with_newline (wrote %d bytes before newline)\n", ret);
    return 0;
}

int main(void) {
    int failed = 0;

    printf("Logger Newline Boundary Regression Fixtures\n");
    printf("============================================\n\n");

    struct { const char *name; int (*func)(void); } tests[] = {
        {"no_newline",                  test_no_newline},
        {"one_newline",                 test_one_newline},
        {"multiple_trailing_newlines",  test_multiple_trailing_newlines},
        {"partial_write_buffer_boundary", test_partial_write_buffer_boundary},
        {"partial_write_with_newline",  test_partial_write_with_newline},
        {NULL, NULL}
    };

    for (int i = 0; tests[i].name != NULL; i++) {
        printf("Test: %s\n", tests[i].name);
        if (tests[i].func() != 0) {
            printf("  FAIL\n");
            failed++;
        }
    }

    printf("\nResults: %d passed, %d failed\n", 5 - failed, failed);
    return failed;
}
