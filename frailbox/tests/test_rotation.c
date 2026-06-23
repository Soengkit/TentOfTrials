/**
 * @file test_rotation.c
 * @brief Test suite for log rotation retention report (frailbox bounty #3).
 *
 * Creates temporary log files with varied sizes and mtimes, then verifies
 * that log_rotation_report() produces correct retain/prune decisions for
 * each retention policy. Also confirms that raw log content is never
 * exposed in JSON or text output.
 */
#define _GNU_SOURCE
#define _DEFAULT_SOURCE
#include "logger.h"

#include <dirent.h>
#include <unistd.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <utime.h>
#include <sys/stat.h>
#include <errno.h>

static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define ASSERT(cond, msg) do { \
    tests_run++; \
    if (!(cond)) { \
        printf("FAIL: %s (line %d)\n", msg, __LINE__); \
        tests_failed++; \
    } else { \
        printf("PASS: %s\n", msg); \
        tests_passed++; \
    } \
} while (0)

static int create_test_file(const char *dir, const char *name,
                            const char *content, time_t mtime)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", dir, name);
    FILE *f = fopen(path, "w");
    if (!f) return -1;
    fputs(content, f);
    fclose(f);
    struct utimbuf times = { .actime = mtime, .modtime = mtime };
    if (utime(path, &times) != 0) return -1;
    return 0;
}

static void cleanup_dir(const char *dir)
{
    DIR *d = opendir(dir);
    if (!d) return;
    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        if (de->d_name[0] == '.') continue;
        char path[512];
        snprintf(path, sizeof(path), "%s/%s", dir, de->d_name);
        remove(path);
    }
    closedir(d);
    remove(dir);
}

int main(void)
{
    char tmpl[] = "/tmp/rot_test_XXXXXX";
    char *dir = mkdtemp(tmpl);
    if (!dir) { perror("mkdtemp"); return 1; }

    time_t now = time(NULL);

    /* 5 .log files (varied age) + 1 .txt (non-log, should be excluded) */
    create_test_file(dir, "app.log",    "current log line\nSECRET=password123\n", now);
    create_test_file(dir, "debug.log",  "debug data here\n",                      now - 100);
    create_test_file(dir, "app.1.log",  "30-min-old log\n",                      now - 1800);
    create_test_file(dir, "app.2.log",  "2-hour-old log\n",                      now - 7200);
    create_test_file(dir, "old.log",    "1-day-old log\n",                       now - 86400);
    create_test_file(dir, "notes.txt",  "not a log file\n",                      now);

    /* --- Test 1: max_files=2 keeps 2 newest, prunes 3 oldest --- */
    {
        log_rotation_report_t rpt;
        int rc = log_rotation_report(dir, 2, 0, 0, &rpt);
        ASSERT(rc == 0, "max_files=2 report succeeds");
        ASSERT(rpt.count == 5, "found 5 log files (excludes .txt)");
        ASSERT(rpt.retained == 2, "retained 2 files with max_files=2");
        ASSERT(rpt.pruned == 3, "pruned 3 files with max_files=2");

        char json[8192];
        int jlen = log_rotation_report_to_json(&rpt, json, sizeof(json));
        ASSERT(jlen > 0, "JSON output generated");
        ASSERT(strstr(json, "SECRET") == NULL, "JSON does not expose raw log content");
        ASSERT(strstr(json, "password123") == NULL, "JSON does not expose secret values");
        ASSERT(strstr(json, "\"retained\"") != NULL, "JSON has retained field");
        ASSERT(strstr(json, "\"pruned\"") != NULL, "JSON has pruned field");

        log_rotation_report_free(&rpt);
    }

    /* --- Test 2: max_age=3600 prunes files older than 1 hour --- */
    {
        log_rotation_report_t rpt;
        int rc = log_rotation_report(dir, 0, 3600, 0, &rpt);
        ASSERT(rc == 0, "max_age=3600 report succeeds");
        /* Within 1h: app.log(now), debug.log(now-100), app.1.log(now-1800) = 3 */
        /* Over 1h: app.2.log(now-7200), old.log(now-86400) = 2 */
        ASSERT(rpt.retained == 3, "retained 3 files within age limit");
        ASSERT(rpt.pruned == 2, "pruned 2 files exceeding age limit");
        log_rotation_report_free(&rpt);
    }

    /* --- Test 3: no limits -> all retained --- */
    {
        log_rotation_report_t rpt;
        int rc = log_rotation_report(dir, 0, 0, 0, &rpt);
        ASSERT(rc == 0, "no-limits report succeeds");
        ASSERT(rpt.retained == 5, "all 5 files retained with no limits");
        ASSERT(rpt.pruned == 0, "0 files pruned with no limits");
        log_rotation_report_free(&rpt);
    }

    /* --- Test 4: text output format --- */
    {
        log_rotation_report_t rpt;
        log_rotation_report(dir, 3, 0, 0, &rpt);
        char text[8192];
        int tlen = log_rotation_report_to_text(&rpt, text, sizeof(text));
        ASSERT(tlen > 0, "text output generated");
        ASSERT(strstr(text, "Retained:") != NULL, "text contains retained count");
        ASSERT(strstr(text, "Pruned:") != NULL, "text contains pruned count");
        ASSERT(strstr(text, "SECRET") == NULL, "text does not expose raw content");
        log_rotation_report_free(&rpt);
    }

    /* --- Test 5: combined age + count policy --- */
    {
        log_rotation_report_t rpt;
        int rc = log_rotation_report(dir, 4, 3600, 0, &rpt);
        ASSERT(rc == 0, "combined policy report succeeds");
        /* Age prunes 2 (app.2.log, old.log), count allows 4 but only 3 left */
        ASSERT(rpt.retained == 3, "retained 3 with combined age+count");
        ASSERT(rpt.pruned == 2, "pruned 2 with combined age+count");
        log_rotation_report_free(&rpt);
    }

    /* --- Test 6: NULL and error handling --- */
    {
        log_rotation_report_t rpt;
        ASSERT(log_rotation_report(NULL, 0, 0, 0, &rpt) == -1, "NULL dir returns -1");
        ASSERT(log_rotation_report(dir, 0, 0, 0, NULL) == -1, "NULL report returns -1");
        ASSERT(log_rotation_report("/tmp/nonexistent_rot_dir_xyz", 0, 0, 0, &rpt) == -1,
               "nonexistent dir returns -1");
    }

    /* --- Test 7: JSON entries have required fields --- */
    {
        log_rotation_report_t rpt;
        log_rotation_report(dir, 0, 0, 0, &rpt);
        char json[16384];
        log_rotation_report_to_json(&rpt, json, sizeof(json));
        ASSERT(strstr(json, "\"filename\"") != NULL, "JSON has filename field");
        ASSERT(strstr(json, "\"size\"") != NULL, "JSON has size field");
        ASSERT(strstr(json, "\"mtime\"") != NULL, "JSON has mtime field");
        ASSERT(strstr(json, "\"decision\"") != NULL, "JSON has decision field");
        ASSERT(strstr(json, "\"reason\"") != NULL, "JSON has reason field");
        log_rotation_report_free(&rpt);
    }

    cleanup_dir(dir);

    printf("\n=== Rotation Report Tests ===\n");
    printf("Total: %d  Passed: %d  Failed: %d\n", tests_run, tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
