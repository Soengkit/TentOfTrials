#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <errno.h>

#include "arena.h"
#include "sandbox.h"

#define VERSION "0.1.0"
#define DEFAULT_REGION_SIZE (1024 * 1024 * 64)

static volatile sig_atomic_t running = 1;

typedef enum self_test_format {
    SELF_TEST_TEXT = 0,
    SELF_TEST_JSON = 1,
} self_test_format_t;

typedef struct self_test_result {
    const char *name;
    int passed;
    double duration_ms;
    char failure_reason[160];
} self_test_result_t;

typedef int (*self_test_fn)(const char *inject_failure, char *reason, size_t reason_size);

typedef struct self_test_case {
    const char *name;
    self_test_fn run;
} self_test_case_t;

enum {
    OPT_SELF_TEST = 1000,
    OPT_SELF_TEST_FORMAT,
    OPT_SELF_TEST_INJECT_FAILURE,
};

static void handle_signal(int sig) {
    (void)sig;
    running = 0;
}

static void print_banner(void) {
    fprintf(stdout,
        "╔══════════════════════════════════════════╗\n"
        "║       frailbox  -  Sandbox Framework       ║\n"
        "║        Tent of Trials v%s             ║\n"
        "╚══════════════════════════════════════════╝\n\n",
        VERSION);
}

static void print_config(const sandbox_config_t *config) {
    static const char *type_names[] = {
        [SANDBOX_NONE]       = "none",
        [SANDBOX_SECCOMP]    = "seccomp",
        [SANDBOX_NAMESPACE]  = "namespace",
        [SANDBOX_CAPABILITY] = "capability",
        [SANDBOX_CHROOT]    = "chroot",
        [SANDBOX_LANDLOCK]   = "landlock",
    };

    fprintf(stdout, "  sandbox type:       %s\n", type_names[config->type]);
    fprintf(stdout, "  memory limit:       %lu MB\n",
            (unsigned long)(config->memory_limit_bytes / (1024 * 1024)));
    fprintf(stdout, "  cpu limit:          %lu ns\n",
            (unsigned long)config->cpu_limit_ns);
    fprintf(stdout, "  max processes:      %u\n", config->max_processes);
    fprintf(stdout, "  max open fds:       %u\n", config->max_open_fds);
    fprintf(stdout, "  network:            %s\n", config->enable_network ? "enabled" : "disabled");
    fprintf(stdout, "  ptrace:             %s\n", config->enable_ptrace ? "enabled" : "disabled");
    fprintf(stdout, "  rules:              %zu\n", config->rule_count);
    fprintf(stdout, "  chroot:             %s\n",
            config->chroot_path[0] ? config->chroot_path : "(none)");
    fprintf(stdout, "  work dir:           %s\n",
            config->work_dir[0] ? config->work_dir : "(default)");
    fprintf(stdout, "\n");
}

static double elapsed_ms(const struct timespec *start, const struct timespec *end) {
    time_t sec = end->tv_sec - start->tv_sec;
    long nsec = end->tv_nsec - start->tv_nsec;

    if (nsec < 0) {
        sec--;
        nsec += 1000000000L;
    }

    return ((double)sec * 1000.0) + ((double)nsec / 1000000.0);
}

static int should_inject_failure(const char *inject_failure, const char *test_name) {
    return inject_failure && strcmp(inject_failure, test_name) == 0;
}

static int fail_reason(char *reason, size_t reason_size, const char *message) {
    snprintf(reason, reason_size, "%s", message);
    return 0;
}

static int self_test_arena_allocator(
        const char *inject_failure, char *reason, size_t reason_size) {
    if (should_inject_failure(inject_failure, "arena_allocator")) {
        return fail_reason(reason, reason_size,
                           "fixture failure requested for arena_allocator");
    }

    arena_t *arena = arena_create(4096, ARENA_ZERO_INIT);
    if (!arena) {
        return fail_reason(reason, reason_size, "arena_create returned NULL");
    }

    unsigned char *bytes = arena_alloc(arena, 64);
    unsigned char *zeroed = arena_calloc(arena, 16, 2);
    void *aligned = arena_alloc_aligned(arena, 128, 64);

    if (!bytes || !zeroed || !aligned) {
        arena_destroy(arena);
        return fail_reason(reason, reason_size, "arena allocation returned NULL");
    }

    for (size_t i = 0; i < 32; i++) {
        if (zeroed[i] != 0) {
            arena_destroy(arena);
            return fail_reason(reason, reason_size, "arena_calloc returned non-zero data");
        }
    }

    arena_stats_t stats = arena_get_stats(arena);
    if (stats.allocation_count != 3) {
        arena_destroy(arena);
        return fail_reason(reason, reason_size, "allocation count did not match fixture");
    }
    if (!arena_contains(arena, bytes) || arena_total_capacity(arena) < 4096) {
        arena_destroy(arena);
        return fail_reason(reason, reason_size, "arena ownership/capacity check failed");
    }

    arena_destroy(arena);
    return 1;
}

static int self_test_sandbox_config(
        const char *inject_failure, char *reason, size_t reason_size) {
    if (should_inject_failure(inject_failure, "sandbox_config")) {
        return fail_reason(reason, reason_size,
                           "fixture failure requested for sandbox_config");
    }

    sandbox_config_t config;
    memset(&config, 0, sizeof(config));
    config.type = SANDBOX_NONE;
    config.memory_limit_bytes = 64ULL * 1024ULL * 1024ULL;
    config.cpu_limit_ns = 1000ULL * 1000000ULL;
    config.max_processes = 10;
    config.max_open_fds = 64;

    sandbox_t *sandbox = sandbox_create(&config);
    if (!sandbox) {
        return fail_reason(reason, reason_size, "sandbox_create returned NULL");
    }

    if (sandbox_add_rule(sandbox, CAP_FILE_READ, ACTION_ALLOW) != 0) {
        sandbox_destroy(sandbox);
        return fail_reason(reason, reason_size, "sandbox_add_rule failed");
    }

    if (sandbox->config.rule_count != 1) {
        sandbox_destroy(sandbox);
        return fail_reason(reason, reason_size, "sandbox rule count did not match fixture");
    }

    if (sandbox_apply(sandbox) != 0 || !sandbox_is_active(sandbox)) {
        sandbox_destroy(sandbox);
        return fail_reason(reason, reason_size, "SANDBOX_NONE did not become active");
    }

    sandbox_destroy(sandbox);
    return 1;
}

static void json_string(FILE *out, const char *value) {
    fputc('"', out);
    for (const unsigned char *p = (const unsigned char *)value; *p; p++) {
        switch (*p) {
        case '"':
            fputs("\\\"", out);
            break;
        case '\\':
            fputs("\\\\", out);
            break;
        case '\b':
            fputs("\\b", out);
            break;
        case '\f':
            fputs("\\f", out);
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

static int run_self_tests(self_test_format_t format, const char *inject_failure) {
    const self_test_case_t tests[] = {
        { "arena_allocator", self_test_arena_allocator },
        { "sandbox_config", self_test_sandbox_config },
    };
    const size_t test_count = sizeof(tests) / sizeof(tests[0]);
    self_test_result_t results[sizeof(tests) / sizeof(tests[0])];
    struct timespec suite_start;
    struct timespec suite_end;
    size_t passed = 0;
    size_t failed = 0;

    clock_gettime(CLOCK_MONOTONIC, &suite_start);

    for (size_t i = 0; i < test_count; i++) {
        struct timespec test_start;
        struct timespec test_end;

        memset(&results[i], 0, sizeof(results[i]));
        results[i].name = tests[i].name;

        clock_gettime(CLOCK_MONOTONIC, &test_start);
        results[i].passed = tests[i].run(
                inject_failure, results[i].failure_reason,
                sizeof(results[i].failure_reason));
        clock_gettime(CLOCK_MONOTONIC, &test_end);
        results[i].duration_ms = elapsed_ms(&test_start, &test_end);

        if (results[i].passed) {
            passed++;
        } else {
            failed++;
        }
    }

    clock_gettime(CLOCK_MONOTONIC, &suite_end);

    if (format == SELF_TEST_JSON) {
        fprintf(stdout, "{");
        fprintf(stdout, "\"summary\":{");
        fprintf(stdout, "\"status\":");
        json_string(stdout, failed == 0 ? "pass" : "fail");
        fprintf(stdout, ",\"total\":%zu,\"passed\":%zu,\"failed\":%zu",
                test_count, passed, failed);
        fprintf(stdout, ",\"duration_ms\":%.3f", elapsed_ms(&suite_start, &suite_end));
        fprintf(stdout, "},\"tests\":[");

        for (size_t i = 0; i < test_count; i++) {
            if (i > 0) {
                fputc(',', stdout);
            }

            fprintf(stdout, "{");
            fprintf(stdout, "\"name\":");
            json_string(stdout, results[i].name);
            fprintf(stdout, ",\"status\":");
            json_string(stdout, results[i].passed ? "pass" : "fail");
            fprintf(stdout, ",\"duration_ms\":%.3f", results[i].duration_ms);
            if (!results[i].passed && results[i].failure_reason[0]) {
                fprintf(stdout, ",\"failure_reason\":");
                json_string(stdout, results[i].failure_reason);
            }
            fprintf(stdout, "}");
        }

        fprintf(stdout, "]}\n");
        return failed == 0 ? 0 : 1;
    }

    fprintf(stdout, "frailbox self-test: %s (%zu passed, %zu failed, %.3f ms)\n",
            failed == 0 ? "PASS" : "FAIL", passed, failed,
            elapsed_ms(&suite_start, &suite_end));
    for (size_t i = 0; i < test_count; i++) {
        fprintf(stdout, "  [%s] %s (%.3f ms)",
                results[i].passed ? "PASS" : "FAIL",
                results[i].name, results[i].duration_ms);
        if (!results[i].passed && results[i].failure_reason[0]) {
            fprintf(stdout, ": %s", results[i].failure_reason);
        }
        fprintf(stdout, "\n");
    }

    return failed == 0 ? 0 : 1;
}

int main(int argc, char *argv[]) {
    int opt;
    int verbose = 0;
    int self_test = 0;
    self_test_format_t self_test_format = SELF_TEST_TEXT;
    const char *self_test_inject_failure = NULL;
    sandbox_type_t sandbox_type = SANDBOX_SECCOMP;
    uint64_t memory_limit_mb = 256;
    uint64_t cpu_limit_ms = 1000;

    static struct option long_options[] = {
        {"sandbox-type",    required_argument, 0, 't'},
        {"memory-limit",    required_argument, 0, 'm'},
        {"cpu-limit",       required_argument, 0, 'c'},
        {"verbose",         no_argument,       0, 'v'},
        {"self-test",       no_argument,       0, OPT_SELF_TEST},
        {"self-test-format", required_argument, 0, OPT_SELF_TEST_FORMAT},
        {"self-test-inject-failure", required_argument, 0, OPT_SELF_TEST_INJECT_FAILURE},
        {"help",            no_argument,       0, 'h'},
        {"version",         no_argument,       0, 'V'},
        {0, 0, 0, 0}
    };

    while ((opt = getopt_long(argc, argv, "t:m:c:vhV", long_options, NULL)) != -1) {
        switch (opt) {
        case 't':
            if (strcmp(optarg, "seccomp") == 0)
                sandbox_type = SANDBOX_SECCOMP;
            else if (strcmp(optarg, "namespace") == 0)
                sandbox_type = SANDBOX_NAMESPACE;
            else if (strcmp(optarg, "none") == 0)
                sandbox_type = SANDBOX_NONE;
            else {
                fprintf(stderr, "unknown sandbox type: %s\n", optarg);
                return 1;
            }
            break;
        case 'm':
            memory_limit_mb = strtoull(optarg, NULL, 10);
            break;
        case 'c':
            cpu_limit_ms = strtoull(optarg, NULL, 10);
            break;
        case 'v':
            verbose = 1;
            break;
        case OPT_SELF_TEST:
            self_test = 1;
            break;
        case OPT_SELF_TEST_FORMAT:
            if (strcmp(optarg, "text") == 0)
                self_test_format = SELF_TEST_TEXT;
            else if (strcmp(optarg, "json") == 0)
                self_test_format = SELF_TEST_JSON;
            else {
                fprintf(stderr, "unknown self-test format: %s\n", optarg);
                return 1;
            }
            break;
        case OPT_SELF_TEST_INJECT_FAILURE:
            self_test_inject_failure = optarg;
            break;
        case 'h':
            fprintf(stdout, "Usage: %s [options]\n\n", argv[0]);
            fprintf(stdout, "Options:\n");
            fprintf(stdout, "  -t, --sandbox-type TYPE   sandbox type (seccomp, namespace, none)\n");
            fprintf(stdout, "  -m, --memory-limit MB     memory limit in megabytes\n");
            fprintf(stdout, "  -c, --cpu-limit MS        CPU time limit in milliseconds\n");
            fprintf(stdout, "  -v, --verbose             verbose output\n");
            fprintf(stdout, "      --self-test           run deterministic self-tests and exit\n");
            fprintf(stdout, "      --self-test-format F  self-test output format (text, json)\n");
            fprintf(stdout, "  -h, --help                show this help\n");
            fprintf(stdout, "  -V, --version             show version\n");
            return 0;
        case 'V':
            fprintf(stdout, "frailbox v%s\n", VERSION);
            return 0;
        default:
            return 1;
        }
    }

    if (self_test) {
        return run_self_tests(self_test_format, self_test_inject_failure);
    }

    if (signal(SIGINT, handle_signal) == SIG_ERR) {
        perror("signal");
        return 1;
    }
    if (signal(SIGTERM, handle_signal) == SIG_ERR) {
        perror("signal");
        return 1;
    }

    print_banner();

    arena_t *arena = arena_create(DEFAULT_REGION_SIZE, ARENA_ZERO_INIT);
    if (!arena) {
        fprintf(stderr, "failed to create arena allocator\n");
        return 1;
    }

    sandbox_config_t config;
    memset(&config, 0, sizeof(config));
    config.type = sandbox_type;
    config.memory_limit_bytes = memory_limit_mb * 1024 * 1024;
    config.cpu_limit_ns = cpu_limit_ms * 1000000;
    config.max_processes = 10;
    config.max_open_fds = 64;
    config.enable_network = 0;
    config.enable_ptrace = 0;

    sandbox_rule_t rule1 = {
        .capability = CAP_FILE_READ,
        .action = ACTION_ALLOW,
        .syscall_number = 0,
        .next = NULL,
    };
    sandbox_rule_t rule2 = {
        .capability = CAP_FILE_WRITE,
        .action = ACTION_DENY,
        .syscall_number = 0,
        .next = NULL,
    };
    rule1.next = &rule2;
    config.rules = &rule1;
    config.rule_count = 2;

    if (verbose) {
        fprintf(stdout, "Sandbox Configuration:\n");
        print_config(&config);
    }

    sandbox_t *sandbox = sandbox_create(&config);
    if (!sandbox) {
        fprintf(stderr, "failed to create sandbox\n");
        arena_destroy(arena);
        return 1;
    }

    if (sandbox_apply(sandbox) != 0) {
        fprintf(stderr, "warning: sandbox_apply failed: %s\n", strerror(errno));
    }

    void *ptr1 = arena_alloc(arena, 1024);
    void *ptr2 = arena_alloc_aligned(arena, 4096, 4096);
    void *ptr3 = arena_calloc(arena, 1, 2048);

    arena_stats_t stats = arena_get_stats(arena);

    if (verbose) {
        fprintf(stdout, "Arena Statistics:\n");
        fprintf(stdout, "  total allocated:     %lu bytes\n",
                (unsigned long)stats.total_allocated);
        fprintf(stdout, "  peak usage:          %lu bytes\n",
                (unsigned long)stats.peak_usage);
        fprintf(stdout, "  current usage:       %lu bytes\n",
                (unsigned long)stats.current_usage);
        fprintf(stdout, "  allocation count:    %lu\n",
                (unsigned long)stats.allocation_count);
        fprintf(stdout, "  region count:        %lu\n",
                (unsigned long)stats.region_count);
        fprintf(stdout, "\n");
    }

    fprintf(stdout, "frailbox: sandbox %s initialized [type=%d, mem=%luMB]\n",
            sandbox_is_active(sandbox) ? "ACTIVE" : "PASSIVE",
            sandbox->config.type,
            (unsigned long)(sandbox->config.memory_limit_bytes / (1024 * 1024)));

    fprintf(stdout, "frailbox: arena allocator running [regions=%lu, used=%lu bytes]\n",
            (unsigned long)stats.region_count,
            (unsigned long)stats.current_usage);

    fprintf(stdout, "frailbox: entering main loop (press Ctrl+C to exit)\n");

    while (running) {
        struct timespec ts = { .tv_sec = 0, .tv_nsec = 100000000 };
        nanosleep(&ts, NULL);
    }

    fprintf(stdout, "\nfrailbox: shutting down...\n");

    sandbox_destroy(sandbox);
    arena_destroy(arena);

    fprintf(stdout, "frailbox: shutdown complete\n");

    (void)ptr1;
    (void)ptr2;
    (void)ptr3;
    return 0;
}
