#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MAX_TESTS 16
#define NAME_SIZE 96
#define REASON_SIZE 256
#define LINE_SIZE 512

typedef struct selftest_result {
    char name[NAME_SIZE];
    int passed;
    double duration_ms;
    char failure_reason[REASON_SIZE];
} selftest_result_t;

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1000000.0;
}

static void copy_string(char *dst, size_t dst_size, const char *src) {
    if (dst_size == 0) {
        return;
    }
    snprintf(dst, dst_size, "%s", src ? src : "");
}

static char *trim(char *value) {
    while (*value && isspace((unsigned char)*value)) {
        value++;
    }

    char *end = value + strlen(value);
    while (end > value && isspace((unsigned char)*(end - 1))) {
        end--;
    }
    *end = '\0';
    return value;
}

static void json_string(FILE *out, const char *value) {
    fputc('"', out);
    for (const unsigned char *p = (const unsigned char *)(value ? value : ""); *p; p++) {
        switch (*p) {
        case '\\':
            fputs("\\\\", out);
            break;
        case '"':
            fputs("\\\"", out);
            break;
        case '\n':
            fputs("\\n", out);
            break;
        case '\r':
            fputs("\\r", out);
            break;
        case '\t':
            fputs("\\t", out);
            break;
        default:
            if (*p < 0x20) {
                fprintf(out, "\\u%04x", *p);
            } else {
                fputc(*p, out);
            }
            break;
        }
    }
    fputc('"', out);
}

static selftest_result_t run_memory_smoke(void) {
    selftest_result_t result = {0};
    double start = now_ms();
    copy_string(result.name, sizeof(result.name), "memory_smoke");
    result.passed = 1;

    unsigned char *buffer = calloc(32, 1);
    if (!buffer) {
        result.passed = 0;
        copy_string(result.failure_reason, sizeof(result.failure_reason), "calloc returned NULL");
    } else {
        for (size_t i = 0; i < 32; i++) {
            buffer[i] = (unsigned char)i;
        }
        for (size_t i = 0; i < 32; i++) {
            if (buffer[i] != (unsigned char)i) {
                result.passed = 0;
                copy_string(result.failure_reason, sizeof(result.failure_reason), "memory roundtrip mismatch");
                break;
            }
        }
        free(buffer);
    }

    result.duration_ms = now_ms() - start;
    return result;
}

static selftest_result_t run_fixture(const char *path) {
    selftest_result_t result = {0};
    double start = now_ms();
    copy_string(result.name, sizeof(result.name), "fixture");
    result.passed = 0;

    FILE *file = fopen(path, "r");
    if (!file) {
        snprintf(result.failure_reason, sizeof(result.failure_reason),
                 "could not open fixture: %s", strerror(errno));
        result.duration_ms = now_ms() - start;
        return result;
    }

    char expected[32] = "";
    char line[LINE_SIZE];
    while (fgets(line, sizeof(line), file)) {
        char *clean = trim(line);
        if (*clean == '\0' || *clean == '#') {
            continue;
        }
        char *equals = strchr(clean, '=');
        if (!equals) {
            continue;
        }
        *equals = '\0';
        char *key = trim(clean);
        char *value = trim(equals + 1);
        if (strcmp(key, "name") == 0) {
            copy_string(result.name, sizeof(result.name), value);
        } else if (strcmp(key, "expect") == 0) {
            copy_string(expected, sizeof(expected), value);
        } else if (strcmp(key, "reason") == 0) {
            copy_string(result.failure_reason, sizeof(result.failure_reason), value);
        }
    }
    fclose(file);

    if (strcmp(expected, "pass") == 0) {
        result.passed = 1;
        result.failure_reason[0] = '\0';
    } else if (strcmp(expected, "fail") == 0) {
        result.passed = 0;
        if (result.failure_reason[0] == '\0') {
            copy_string(result.failure_reason, sizeof(result.failure_reason), "fixture requested failure");
        }
    } else {
        result.passed = 0;
        copy_string(result.failure_reason, sizeof(result.failure_reason), "fixture missing expect=pass or expect=fail");
    }

    result.duration_ms = now_ms() - start;
    return result;
}

static void print_text_summary(const selftest_result_t *results, size_t count) {
    size_t passed = 0;
    for (size_t i = 0; i < count; i++) {
        passed += results[i].passed ? 1U : 0U;
        printf("%s: %s (%.3fms)", results[i].name,
               results[i].passed ? "PASS" : "FAIL",
               results[i].duration_ms);
        if (!results[i].passed && results[i].failure_reason[0] != '\0') {
            printf(" - %s", results[i].failure_reason);
        }
        putchar('\n');
    }
    printf("RESULTS: %zu passed, %zu failed out of %zu\n", passed, count - passed, count);
}

static void print_json_summary(const selftest_result_t *results, size_t count) {
    size_t passed = 0;
    for (size_t i = 0; i < count; i++) {
        passed += results[i].passed ? 1U : 0U;
    }

    printf("{\n");
    printf("  \"total\": %zu,\n", count);
    printf("  \"passed\": %zu,\n", passed);
    printf("  \"failed\": %zu,\n", count - passed);
    printf("  \"tests\": [\n");
    for (size_t i = 0; i < count; i++) {
        printf("    {\n");
        printf("      \"name\": ");
        json_string(stdout, results[i].name);
        printf(",\n");
        printf("      \"status\": \"%s\",\n", results[i].passed ? "pass" : "fail");
        printf("      \"duration_ms\": %.3f,\n", results[i].duration_ms);
        printf("      \"failure_reason\": ");
        if (results[i].passed) {
            printf("null\n");
        } else {
            json_string(stdout, results[i].failure_reason);
            putchar('\n');
        }
        printf("    }%s\n", i + 1 == count ? "" : ",");
    }
    printf("  ]\n");
    printf("}\n");
}

int main(int argc, char **argv) {
    int json = 0;
    const char *fixture = NULL;
    selftest_result_t results[MAX_TESTS];
    size_t count = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--json") == 0) {
            json = 1;
        } else if (strcmp(argv[i], "--fixture") == 0 && i + 1 < argc) {
            fixture = argv[++i];
        } else {
            fprintf(stderr, "usage: %s [--json] [--fixture PATH]\n", argv[0]);
            return 2;
        }
    }

    results[count++] = run_memory_smoke();
    if (fixture) {
        results[count++] = run_fixture(fixture);
    }

    if (json) {
        print_json_summary(results, count);
    } else {
        print_text_summary(results, count);
    }

    for (size_t i = 0; i < count; i++) {
        if (!results[i].passed) {
            return 1;
        }
    }
    return 0;
}
