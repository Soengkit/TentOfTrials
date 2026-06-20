/*
 * Lightweight fixture for legacy logger rotation retention reporting.
 *
 * The fixture uses synthetic metadata only. It verifies retained and pruned
 * decisions, file name/size/mtime fields, retention reasons, and redaction of
 * secret-like metadata without reading any log file contents.
 */

#define _POSIX_C_SOURCE 200809L

#include "../include/logger.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;

#define CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            fprintf(stderr, "FAIL: %s\n", (msg)); \
            failures++; \
        } \
    } while (0)

static char *capture_report(const log_retention_entry_t *entries, size_t count)
{
    char *buffer = NULL;
    size_t size = 0;
    FILE *out = open_memstream(&buffer, &size);
    if (out == NULL) {
        return NULL;
    }

    if (log_write_retention_report(out, entries, count) != 0) {
        fclose(out);
        free(buffer);
        return NULL;
    }
    fclose(out);
    return buffer;
}

int main(void)
{
    const log_retention_entry_t entries[] = {
        {
            .file_name = "frailbox.log",
            .size_bytes = 1200,
            .mtime = 1710000100,
            .has_mtime = 1,
            .decision = LOG_RETENTION_RETAINED,
            .reason = "active log within retention window",
        },
        {
            .file_name = "frailbox.log.1",
            .size_bytes = 640,
            .mtime = 1710000000,
            .has_mtime = 1,
            .decision = LOG_RETENTION_PRUNED,
            .reason = "older than retention limit",
        },
        {
            .file_name = "service-token-raw.log",
            .size_bytes = 88,
            .mtime = 0,
            .has_mtime = 0,
            .decision = LOG_RETENTION_PRUNED,
            .reason = "password marker in metadata name",
        },
    };

    char *report = capture_report(entries, sizeof(entries) / sizeof(entries[0]));
    CHECK(report != NULL, "retention report should be generated");
    if (report == NULL) {
        return 1;
    }

    CHECK(strstr(report, "\"total\": 3") != NULL,
          "report includes total file count");
    CHECK(strstr(report, "\"retained\": 1") != NULL,
          "report counts retained files");
    CHECK(strstr(report, "\"pruned\": 2") != NULL,
          "report counts pruned files");
    CHECK(strstr(report, "\"file_name\": \"frailbox.log\"") != NULL,
          "report includes retained file name");
    CHECK(strstr(report, "\"size_bytes\": 1200") != NULL,
          "report includes retained file size");
    CHECK(strstr(report, "\"mtime\": 1710000100") != NULL,
          "report includes available mtime");
    CHECK(strstr(report, "\"decision\": \"retained\"") != NULL,
          "report includes retained decision");
    CHECK(strstr(report, "\"decision\": \"pruned\"") != NULL,
          "report includes pruned decision");
    CHECK(strstr(report, "\"reason\": \"older than retention limit\"") != NULL,
          "report includes retention reason");
    CHECK(strstr(report, "\"mtime\": null") != NULL,
          "report uses null when mtime is unavailable");
    CHECK(strstr(report, "service-token-raw.log") == NULL,
          "secret-like file metadata is redacted");
    CHECK(strstr(report, "password marker in metadata name") == NULL,
          "secret-like reason metadata is redacted");
    CHECK(strstr(report, "\"[REDACTED]\"") != NULL,
          "redaction marker is present for secret-like metadata");

    free(report);

    CHECK(log_write_retention_report(NULL, entries, 1) == -1,
          "NULL output stream is rejected");
    CHECK(log_write_retention_report(stdout, NULL, 1) == -1,
          "NULL entries with non-zero count are rejected");

    if (failures != 0) {
        fprintf(stderr, "logger retention report fixture failed: %d failures\n", failures);
        return 1;
    }

    printf("logger retention report fixture passed\n");
    return 0;
}
