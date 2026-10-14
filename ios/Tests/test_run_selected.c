/*
 * test_run_selected.c — the selected-suite regression (pass 04).
 *
 * PHASE_02_RECONSTRUCTED_POC (see runtime_platform.h).
 *
 * Why this file exists. IPHONE13_PHYSICAL_RUN_03: the app opened, `Run Selected` closed it,
 * and no host test had ever exercised the path that button takes. Every earlier host suite ran
 * one suite in one process and stopped there. This test runs the selections through the SAME
 * entry points the bridge uses — phase02_run_suite / phase02_run_all — the way the bridge uses
 * them (a fresh phase02_log_t per press, one report out, log freed), and checks the things a
 * crash would violate:
 *
 *   - each of the nine selections starts, returns, and produces a valid report;
 *   - a second press works (no one-shot state);
 *   - the sequence memory -> cpu -> threads -> memory -> jit -> cpu -> all leaves no residue:
 *     each press reports exactly what the same suite reported on its own;
 *   - ten sequential reruns of memory, of cpu and of threads each report the same numbers
 *     (accumulating counters, stale buffers and unreset guards all break this);
 *   - the flight recorder writes a journal that names its last checkpoint, and the write-ahead
 *     report is a real report on disk before the risky step;
 *   - the execution policy answers the platform matrix correctly, including the iOS row that
 *     no host can produce (pure function, so it is testable everywhere).
 *
 * It asserts behaviour, never a platform verdict: no line here claims anything about iOS.
 */
#include "phase02_execution_policy.h"
#include "phase02_harness.h"
#include "phase02_log.h"
#include "phase02_progress.h"
#include "runtime_platform.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

static unsigned g_checks = 0u;
static unsigned g_failures = 0u;

#define CHECK(condition, name)                                                   \
    do {                                                                         \
        g_checks++;                                                              \
        if ((condition) != 0) {                                                  \
            printf("ok   %s\n", (name));                                         \
        } else {                                                                 \
            g_failures++;                                                        \
            printf("FAIL %s (%s:%d)\n", (name), __FILE__, __LINE__);             \
        }                                                                        \
    } while (0)

/* Every selection the button can produce, plus "all". */
static const char *const g_selections[] = { "memory", "jit", "cpu", "threads", "signals",
                                            "fs",     "ipc", "loader", "all" };

typedef struct run_totals {
    unsigned records;
    unsigned pass;
    unsigned fail;
    unsigned blocked;
    unsigned unsupported;
    unsigned untested;
    unsigned not_applicable;
    char     summary[64];
    size_t   text_length;
    int      had_text;
} run_totals_t;

static char g_workdir[512];

/* The full report of the most recent bridge-equivalent run. Static (not on the stack): the
 * report can be the whole 192 KiB log. */
static char g_last_report[PHASE02_LOG_CAP];
static int  g_last_report_valid = 0;

/* Mirrors Phase02Bridge -runSuite:workdir: exactly (same calls, same order), minus the parts
 * that are Objective-C. If the bridge ever drifts from this sequence, the bridge audit and this
 * test disagree — which is the point of writing it here. */
static int bridge_equivalent_run(const char *selection, const char *journal_path,
                                 run_totals_t *totals)
{
    phase02_log_t *log;
    char summary[192];
    char summary_line[256];

    memset(totals, 0, sizeof(*totals));
    if (journal_path != NULL) {
        if (phase02_progress_begin(journal_path) != 0) {
            return -1;
        }
    }
    log = phase02_log_new();
    if (log == NULL) {
        if (journal_path != NULL) {
            phase02_progress_end();
        }
        return -1;
    }
    phase02_log_init(log, "PHASE_02_RECONSTRUCTED_POC - iOS runtime PoC diagnostics");
    if (phase02_platform_summary(summary, sizeof(summary)) < 0) {
        (void)snprintf(summary, sizeof(summary), "platform=<unavailable>");
    }
    phase02_log_line(log, "# device: %s", summary);
    phase02_log_line(log, "# suite: %s", selection);
    if (strcmp(selection, "all") == 0) {
        (void)phase02_run_all(log, g_workdir);
    } else {
        (void)phase02_run_suite(selection, log, g_workdir);
    }
    (void)phase02_log_summary_line(summary_line, sizeof(summary_line), log);
    phase02_log_blank(log);
    phase02_log_line(log, "# %s", summary_line);

    totals->records = phase02_log_status_count(log, RT_PASS) +
                      phase02_log_status_count(log, RT_FAIL) +
                      phase02_log_status_count(log, RT_BLOCKED) +
                      phase02_log_status_count(log, RT_UNSUPPORTED) +
                      phase02_log_status_count(log, RT_UNTESTED) +
                      phase02_log_status_count(log, RT_NOT_APPLICABLE);
    totals->pass = phase02_log_status_count(log, RT_PASS);
    totals->fail = phase02_log_status_count(log, RT_FAIL);
    totals->blocked = phase02_log_status_count(log, RT_BLOCKED);
    totals->unsupported = phase02_log_status_count(log, RT_UNSUPPORTED);
    totals->untested = phase02_log_status_count(log, RT_UNTESTED);
    totals->not_applicable = phase02_log_status_count(log, RT_NOT_APPLICABLE);
    (void)snprintf(totals->summary, sizeof(totals->summary), "%s",
                   strstr(summary_line, "summary=") != NULL
                       ? strstr(summary_line, "summary=") + 8
                       : "?");
    totals->text_length = phase02_log_length(log);
    totals->had_text = (phase02_log_text(log) != NULL && phase02_log_text(log)[0] != '\0') ? 1 : 0;
    {
        const char *text = phase02_log_text(log);
        if (text != NULL && totals->text_length < sizeof(g_last_report)) {
            memcpy(g_last_report, text, totals->text_length + 1u);
            g_last_report_valid = 1;
        } else {
            g_last_report_valid = 0;
        }
    }

    phase02_log_free(log);
    if (journal_path != NULL) {
        phase02_progress_end();
    }
    return 0;
}

/* Returns the status string of `record` from the captured report, or "" when the record is
 * absent. This is how the deferred/probe assertions read a run the same way a human reads the
 * report: by the status that is written next to the record. */
static const char *record_status(const char *record, char *out, size_t cap)
{
    char needle[96];
    const char *line;

    out[0] = '\0';
    if (record == NULL || g_last_report_valid == 0) {
        return out;
    }
    (void)snprintf(needle, sizeof(needle), "TEST=%s STATUS=", record);
    line = strstr(g_last_report, needle);
    if (line == NULL) {
        return out;
    }
    line += strlen(needle);
    {
        size_t length = strcspn(line, " \n");
        if (length + 1u > cap) {
            length = cap - 1u;
        }
        memcpy(out, line, length);
        out[length] = '\0';
    }
    return out;
}

static int same_totals(const run_totals_t *a, const run_totals_t *b)
{
    return a->records == b->records && a->pass == b->pass && a->fail == b->fail &&
           a->blocked == b->blocked && a->unsupported == b->unsupported &&
           a->untested == b->untested && a->not_applicable == b->not_applicable &&
           strcmp(a->summary, b->summary) == 0;
}

static void describe(const char *label, const run_totals_t *totals)
{
    printf("     %-9s records=%u pass=%u fail=%u blocked=%u unsupported=%u untested=%u n/a=%u "
           "summary=%s\n",
           label, totals->records, totals->pass, totals->fail, totals->blocked,
           totals->unsupported, totals->untested, totals->not_applicable, totals->summary);
}

/* ---------------------------------------------------- every selection, once */

static void test_every_selection_runs(void)
{
    run_totals_t totals;
    size_t index;

    for (index = 0u; index < (sizeof(g_selections) / sizeof(g_selections[0])); index++) {
        const char *selection = g_selections[index];
        char label[64];
        int rc = bridge_equivalent_run(selection, NULL, &totals);

        (void)snprintf(label, sizeof(label), "run_selected(%s) returns a report", selection);
        CHECK(rc == 0 && totals.had_text != 0, label);
        (void)snprintf(label, sizeof(label), "run_selected(%s) records at least one test", selection);
        CHECK(totals.records > 0u, label);
        (void)snprintf(label, sizeof(label), "run_selected(%s) reports no failures", selection);
        CHECK(totals.fail == 0u, label);
        (void)snprintf(label, sizeof(label), "run_selected(%s) has a summary", selection);
        CHECK(totals.summary[0] != '\0' && strcmp(totals.summary, "?") != 0, label);
        describe(selection, &totals);
    }
    CHECK(phase02_progress_enabled() == 0, "the recorder is off again after a plain run");
}

/* ------------------------------------- a second press, and the exact sequence */

static void test_second_press_and_sequence(void)
{
    run_totals_t baseline[9];
    run_totals_t again;
    run_totals_t totals;
    static const char *const sequence[] = { "memory", "cpu", "threads", "memory",
                                            "jit",    "cpu", "all" };
    size_t index;

    /* Baselines: each suite on its own, in a fresh process-like press. */
    for (index = 0u; index < (sizeof(g_selections) / sizeof(g_selections[0])); index++) {
        CHECK(bridge_equivalent_run(g_selections[index], NULL, &baseline[index]) == 0 &&
                  baseline[index].fail == 0u,
              "baseline run for the sequence comparison");
    }

    /* Second press of the same selection must produce the same numbers: no one-shot state. */
    CHECK(bridge_equivalent_run("cpu", NULL, &again) == 0 && same_totals(&baseline[2], &again),
          "pressing cpu twice reports identical numbers");
    CHECK(bridge_equivalent_run("threads", NULL, &again) == 0 && same_totals(&baseline[3], &again),
          "pressing threads twice reports identical numbers");

    /* The required sequence, each step compared with its own baseline. */
    for (index = 0u; index < (sizeof(sequence) / sizeof(sequence[0])); index++) {
        const char *selection = sequence[index];
        size_t baseline_index = 0u;
        size_t probe;
        char label[96];

        for (probe = 0u; probe < (sizeof(g_selections) / sizeof(g_selections[0])); probe++) {
            if (strcmp(g_selections[probe], selection) == 0) {
                baseline_index = probe;
                break;
            }
        }
        CHECK(bridge_equivalent_run(selection, NULL, &totals) == 0, "sequence step runs");
        (void)snprintf(label, sizeof(label),
                       "sequence step %s matches its baseline (no residue from the previous "
                       "step)", selection);
        CHECK(same_totals(&baseline[baseline_index], &totals), label);
        describe(selection, &totals);
    }
}

/* ------------------------------------------------ an impossible selection */

static void test_invalid_selection(void)
{
    run_totals_t totals;

    /* The picker cannot produce this, but the API can be called with anything: an unknown name
     * must be reported and must not disturb the process (this is what a stale or misspelled
     * selection would do on the device). */
    CHECK(bridge_equivalent_run("no_such_suite", NULL, &totals) == 0 && totals.had_text != 0,
          "an unknown selection still returns a report");
    CHECK(totals.fail == 0u && totals.unsupported == 1u && totals.records == 1u,
          "an unknown selection is one UNSUPPORTED record and nothing else");
    {
        phase02_log_t *log = phase02_log_new();
        CHECK(log != NULL, "an unknown selection does not need a log to be rejected");
        if (log != NULL) {
            CHECK(phase02_run_suite("no_such_suite", log, g_workdir) == RT_UNSUPPORTED,
                  "an unknown selection is UNSUPPORTED, not a crash and not a FAIL");
            phase02_log_free(log);
        }
    }
    CHECK(bridge_equivalent_run("cpu", NULL, &totals) == 0 && totals.fail == 0u,
          "a valid selection still works after an invalid one");
}

/* ------------------------------------------------- ten reruns of each core */

static void test_ten_sequential_reruns(void)
{
    static const char *const subjects[] = { "memory", "cpu", "threads" };
    size_t subject;

    for (subject = 0u; subject < (sizeof(subjects) / sizeof(subjects[0])); subject++) {
        run_totals_t first;
        unsigned iteration;

        CHECK(bridge_equivalent_run(subjects[subject], NULL, &first) == 0 && first.fail == 0u,
              "rerun baseline");
        for (iteration = 1u; iteration <= 10u; iteration++) {
            run_totals_t totals;
            char label[96];
            CHECK(bridge_equivalent_run(subjects[subject], NULL, &totals) == 0, "rerun executes");
            (void)snprintf(label, sizeof(label),
                           "%s rerun %u/%u reports the baseline numbers", subjects[subject],
                           iteration, 10u);
            CHECK(same_totals(&first, &totals), label);
        }
        printf("     %s rerun baseline: ", subjects[subject]);
        describe("", &first);
    }
}

/* --------------------------------------------- flight recorder and artefacts */

#define TEST_PATH_CAP 640u   /* > workdir + name, so no path can be silently shortened */

static void test_journal_and_write_ahead(void)
{
    char directory[TEST_PATH_CAP];
    char journal[TEST_PATH_CAP];
    char report[TEST_PATH_CAP];
    int built;
    char last[PHASE02_PROGRESS_EVENT_MAX + 1u];
    run_totals_t totals;
    FILE *file;
    char line[512];
    int saw_suite_enter = 0;
    int saw_suite_exit = 0;
    int saw_test_line = 0;
    struct stat info;

    built = snprintf(directory, sizeof(directory), "%s/journal-case", g_workdir);
    CHECK(built > 0 && (size_t)built < sizeof(directory), "the case directory path fits");
    (void)mkdir(directory, 0700);
    CHECK(phase02_progress_join(directory, "phase02-run-journal.txt", journal, sizeof(journal)) == 0,
          "journal path is built without truncation");
    CHECK(phase02_progress_join(directory, "phase02-report-ahead.txt", report, sizeof(report)) == 0,
          "write-ahead path is built without truncation");

    CHECK(bridge_equivalent_run("cpu", journal, &totals) == 0 && totals.fail == 0u,
          "a journalled run still produces the normal report");
    CHECK(phase02_progress_enabled() == 0, "the recorder is closed after the run");

    /* The journal survives the process (that is the whole point) and names its last event. */
    CHECK(phase02_progress_last_event(journal, last, sizeof(last)) == 0, "the journal is readable");
    CHECK(strcmp(last, "JOURNAL_END") == 0, "the journal's last event is JOURNAL_END");

    file = fopen(journal, "rb");
    CHECK(file != NULL, "the journal file exists");
    if (file != NULL) {
        while (fgets(line, (int)sizeof(line), file) != NULL) {
            if (strstr(line, "SUITE_ENTER suite=cpu") != NULL) {
                saw_suite_enter = 1;
            }
            if (strstr(line, "SUITE_EXIT suite=cpu") != NULL) {
                saw_suite_exit = 1;
            }
        }
        (void)fclose(file);
    }
    CHECK(saw_suite_enter != 0, "the journal names the suite it entered");
    CHECK(saw_suite_exit != 0, "the journal names the suite it left");

    /* The write-ahead report is a real report on disk, written after the suite finished. */
    CHECK(stat(report, &info) == 0 && info.st_size > 0, "the write-ahead report exists and is not empty");
    file = fopen(report, "rb");
    CHECK(file != NULL, "the write-ahead report is readable");
    if (file != NULL) {
        while (fgets(line, (int)sizeof(line), file) != NULL) {
            if (strstr(line, "[PHASE02] TEST=cpu.") != NULL ||
                strstr(line, "[PHASE02] TEST=") != NULL) {
                saw_test_line = 1;
            }
        }
        (void)fclose(file);
    }
    CHECK(saw_test_line != 0, "the write-ahead report contains the measurements");

    /* Negative cases: both readers must reject what is not a journal. */
    CHECK(phase02_progress_last_event("/nonexistent/phase02-journal.txt", last, sizeof(last)) == -1,
          "reading a missing journal fails instead of inventing an event");
    CHECK(phase02_progress_last_event(journal, last, 3u) == -1,
          "reading into a too-small buffer fails instead of truncating an event");
    (void)unlink(journal);
    (void)unlink(report);
}

/* ------------------------------------------------ profiles (SAFE vs EXECUTION_PROBE)
 *
 * The mission of pass 05 is to answer the execution questions on the device without risking
 * the run. The two profiles are how: SAFE measures everything that can be measured without
 * entering written memory (including the R-X request and what the kernel reports afterwards),
 * EXECUTION_PROBE takes the entry. Both are exercised here through the same entry points the
 * button uses, on a simulated iOS-class target, so the matrix is not a claim in a document but
 * a run of the real code.
 */

static void test_profiles(void)
{
    run_totals_t totals;
    char status[32];

    /* --- API level: the mode is explicit, named and readable --------------------------- */
    CHECK(strcmp(phase02_profile_mode_name(PHASE02_PROFILE_SAFE), "safe") == 0 &&
              strcmp(phase02_profile_mode_name(PHASE02_PROFILE_EXECUTION_PROBE),
                     "execution-probe") == 0,
          "both profiles have distinct names");
    phase02_profile_request_mode(PHASE02_PROFILE_EXECUTION_PROBE);
    CHECK(phase02_profile_mode() == PHASE02_PROFILE_EXECUTION_PROBE,
          "an explicit request selects the execution-probe profile");
    phase02_profile_request_mode(PHASE02_PROFILE_SAFE);
    CHECK(phase02_profile_mode() == PHASE02_PROFILE_SAFE && phase02_profile_mode_requested() == 1,
          "an explicit request selects the safe profile");

    /* --- SAFE on an iOS-class target: measure, but do not enter ------------------------ */
    (void)setenv("PHASE02_TARGET_OVERRIDE", "ios-device", 1);
    (void)unsetenv("PHASE02_ALLOW_EXECUTION");
    (void)unsetenv("PHASE02_PROFILE");
    phase02_profile_request_mode(PHASE02_PROFILE_SAFE);
    CHECK(phase02_execution_allowed(PHASE02_EXEC_OP_ENTER_WRITTEN_MEMORY) == 0 &&
              phase02_execution_allowed(PHASE02_EXEC_OP_OBJECT_EXEC_VIEW) == 0,
          "SAFE defers both fenced operations on an iOS-class target");

    CHECK(bridge_equivalent_run("jit", NULL, &totals) == 0, "the SAFE jit run completes");
    CHECK(record_status("jit.rx_observed", status, sizeof(status)) != NULL && status[0] != '\0',
          "SAFE still measures the R-X request and its observation");
    CHECK(strcmp(record_status("jit.execute_return_42", status, sizeof(status)), "BLOCKED") == 0,
          "SAFE never reports execution: execute_return_42 is BLOCKED");
    CHECK(strcmp(record_status("jit.execute_return_4242", status, sizeof(status)), "BLOCKED") == 0,
          "SAFE never reports the rewritten execution either");
    CHECK(strcmp(record_status("jit.make_executable", status, sizeof(status)), "BLOCKED") == 0,
          "SAFE does not claim the payload is executable: the entry is what is deferred");
    CHECK(g_last_report_valid != 0 && strstr(g_last_report, "deferred") != NULL,
          "the deferral names itself in the report");
    CHECK(strstr(g_last_report, "NOTE=capability_graph") != NULL,
          "every report carries the capability graph");
    CHECK(strstr(g_last_report, "NOTE=execmem_capabilities") != NULL,
          "every report carries the measured executable-memory capabilities");

    CHECK(bridge_equivalent_run("loader", NULL, &totals) == 0 &&
              strcmp(record_status("loader.run_valid_module", status, sizeof(status)),
                     "BLOCKED") == 0,
          "SAFE does not enter the module's entry point (the module is still validated)");
    CHECK(strstr(g_last_report, "image validated and accepted") != NULL &&
              strstr(g_last_report, "NOT rejected") != NULL,
          "and it says the module was not rejected");

    /* --- EXECUTION_PROBE on the same target: the entry is taken ----------------------- */
    phase02_profile_request_mode(PHASE02_PROFILE_EXECUTION_PROBE);
    CHECK(phase02_execution_allowed(PHASE02_EXEC_OP_ENTER_WRITTEN_MEMORY) == 1,
          "EXECUTION_PROBE allows the entry attempt");
    CHECK(bridge_equivalent_run("jit", NULL, &totals) == 0 && totals.fail == 0u,
          "the probe jit run completes on a target that can execute");
    CHECK(strcmp(record_status("jit.execute_return_42", status, sizeof(status)), "PASS") == 0,
          "EXECUTION_PROBE reports the real execution (42 returned)");
    CHECK(strstr(g_last_report, "returned 42 (expected 42)") != NULL,
          "and the report carries the returned value");
    CHECK(strcmp(record_status("jit.execute_return_4242", status, sizeof(status)), "PASS") == 0 &&
              strstr(g_last_report, "returned 4242 (expected 4242)") != NULL,
          "the rewritten payload really returned 4242");
    CHECK(bridge_equivalent_run("loader", NULL, &totals) == 0 &&
              strcmp(record_status("loader.run_valid_module", status, sizeof(status)), "PASS") == 0,
          "the loader enters the module through the same abstraction and gets its value back");
    CHECK(strstr(g_last_report, "entry returned 7") != NULL,
          "the loader's value is the one the image was built to return");
    printf("# EXECUTION_PROBE jit: records=%u pass=%u blocked=%u summary=%s\n", totals.records,
           totals.pass, totals.blocked, totals.summary);

    /* --- the two halves of the dual-mapping experiment stay separate ------------------- */
    phase02_profile_request_mode(PHASE02_PROFILE_SAFE);
    CHECK(bridge_equivalent_run("memory", NULL, &totals) == 0,
          "the SAFE memory run completes");
    CHECK(strcmp(record_status("memory.dual_mapping_views", status, sizeof(status)), "PASS") == 0,
          "the safe half (two live views, RW + R, aliasing) is measured and passes here");
    /* On a host the named POSIX object exists, so the iOS backend (and with it the policy
     * deferral) is never reached: the record is a real PASS here. On the device the named path
     * is refused first, and the executable view is then a policy deferral — which the report
     * says in words. The invariant asserted is the honest one in both cases: a BLOCKED status
     * exactly when the executable view was not requested. */
    {
        const char *dual_status = record_status("memory.dual_mapping_rw_rx", status,
                                                sizeof(status));
        int deferred_in_words = (strstr(g_last_report, "was NOT requested") != NULL) ? 1 : 0;
        int blocked = (strcmp(dual_status, "BLOCKED") == 0) ? 1 : 0;
        int passed = (strcmp(dual_status, "PASS") == 0) ? 1 : 0;
        CHECK(blocked != 0 || passed != 0,
              "the executable-view record has one of the two honest outcomes");
        CHECK(blocked == deferred_in_words,
              "a deferred executable view is reported as BLOCKED and says so (and a granted "
              "one is not reported as deferred)");
        printf("# memory.dual_mapping_rw_rx=%s (executable view deferred in words: %d, host "
               "named-POSIX path available: %d)\n", dual_status, deferred_in_words,
               strstr(g_last_report, "named POSIX object refused") == NULL);
    }

    /* --- and nothing leaks back to the host profile ----------------------------------- */
    (void)unsetenv("PHASE02_TARGET_OVERRIDE");
    phase02_profile_request_mode(PHASE02_PROFILE_SAFE);
    CHECK(phase02_execution_allowed(PHASE02_EXEC_OP_ENTER_WRITTEN_MEMORY) == 1,
          "back on the host profile the attempt is allowed again (no sticky state)");
}

/* ---------------------------------------------- §11: the checkpoint sequence, from the journal
 *
 * Registering a checkpoint is not enough. Run 03 disappeared without saying where, and the
 * answer to "where" has to be in the journal BEFORE the fatal step, in the right order, and
 * absent when the step was never taken. This reads the real journal of a real run and asserts
 * both directions:
 *
 *   SAFE profile  : the whole measurement chain is journalled, the entry is NOT - the record
 *                   that the entry was deferred is the last thing before the gap;
 *   PROBE profile : the same measurement chain, then the entry checkpoints in order, then the
 *                   rewrite set.
 */
static long journal_position(const char *path, const char *needle)
{
    static char buffer[65536];
    FILE *file = fopen(path, "rb");
    size_t got;
    char *found;

    if (file == NULL) {
        return -1;
    }
    got = fread(buffer, 1u, sizeof(buffer) - 1u, file);
    (void)fclose(file);
    buffer[got] = '\0';
    found = strstr(buffer, needle);
    return (found != NULL) ? (long)(found - buffer) : -1;
}

static void test_checkpoint_sequence(void)
{
    char directory[TEST_PATH_CAP];
    char safe_journal[TEST_PATH_CAP];
    char probe_journal[TEST_PATH_CAP];
    char event[96];
    run_totals_t totals;
    int built;
    int index;
    static const char *const measurement_chain[] = {
        "JIT_EXECUTION_PROBE_ENTER", "JIT_MAPPING_CREATED", "JIT_PAYLOAD_WRITTEN",
        "JIT_ICACHE_SYNCED", "JIT_RX_REQUEST_ENTER", "JIT_RX_REQUEST_RESULT", "JIT_RX_OBSERVED"
    };
    static const char *const entry_chain[] = {
        "JIT_ENTER_WRITTEN_MEMORY_BEGIN", "JIT_FUNCTION_ENTRY_ABOUT_TO_CALL",
        "JIT_FUNCTION_ENTRY_RETURNED", "JIT_RETURN_VALUE value=42", "JIT_REWRITE_RW",
        "JIT_REWRITE_WRITTEN", "JIT_REWRITE_ICACHE_SYNCED", "JIT_REWRITE_RX",
        "JIT_REWRITE_ENTRY", "JIT_REWRITE_RETURN_VALUE value=4242"
    };

    built = snprintf(directory, sizeof(directory), "%s/checkpoint-case", g_workdir);
    CHECK(built > 0 && (size_t)built < sizeof(directory), "the checkpoint work directory fits");
    CHECK(mkdir(directory, 0700) == 0 || errno == EEXIST, "the checkpoint directory exists");
    CHECK(phase02_progress_join(directory, "safe-journal.txt", safe_journal,
                                sizeof(safe_journal)) == 0 &&
              phase02_progress_join(directory, "probe-journal.txt", probe_journal,
                                    sizeof(probe_journal)) == 0,
          "both journal paths are built");

    (void)setenv("PHASE02_TARGET_OVERRIDE", "ios-device", 1);
    (void)unsetenv("PHASE02_ALLOW_EXECUTION");
    (void)unsetenv("PHASE02_PROFILE");

    /* --- SAFE: measure everything, enter nothing ------------------------------------- */
    phase02_profile_request_mode(PHASE02_PROFILE_SAFE);
    (void)unlink(safe_journal);
    CHECK(bridge_equivalent_run("jit", safe_journal, &totals) == 0 && totals.fail == 0u,
          "the safe profile produces a complete report with no failures");
    for (index = 0; index < (int)(sizeof(measurement_chain) / sizeof(measurement_chain[0]));
         index++) {
        (void)snprintf(event, sizeof(event), "event=%s", measurement_chain[index]);
        CHECK(journal_position(safe_journal, event) >= 0,
              "the safe profile journals the whole measurement chain");
    }
    CHECK(journal_position(safe_journal, "JIT_ENTER_DEFERRED_POLICY") >= 0,
          "the safe profile records that the entry was deferred by policy");
    for (index = 0; index < (int)(sizeof(entry_chain) / sizeof(entry_chain[0])); index++) {
        (void)snprintf(event, sizeof(event), "event=%s", entry_chain[index]);
        CHECK(journal_position(safe_journal, event) < 0,
              "the safe profile never reaches dynamic entry (no entry checkpoint at all)");
    }
    CHECK(journal_position(safe_journal, "JIT_FUNCTION_ENTRY_ABOUT_TO_CALL") < 0 &&
              journal_position(safe_journal, "event=JOURNAL_END") >
                  journal_position(safe_journal, "JIT_ENTER_DEFERRED_POLICY"),
          "the journal reaches its end after the deferral: the run was not cut short");

    /* --- EXECUTION_PROBE: the same chain, then the entry, in order -------------------- */
    phase02_profile_request_mode(PHASE02_PROFILE_EXECUTION_PROBE);
    (void)unlink(probe_journal);
    CHECK(bridge_equivalent_run("jit", probe_journal, &totals) == 0 && totals.fail == 0u,
          "the probe profile produces a complete report with no failures");
    {
        static const char *const ordered[] = {
            "JIT_EXECUTION_PROBE_ENTER", "JIT_MAPPING_CREATED", "JIT_PAYLOAD_WRITTEN",
            "JIT_ICACHE_SYNCED", "JIT_RX_REQUEST_ENTER", "JIT_RX_REQUEST_RESULT",
            "JIT_RX_OBSERVED", "JIT_ENTER_WRITTEN_MEMORY_BEGIN",
            "JIT_FUNCTION_ENTRY_ABOUT_TO_CALL", "JIT_FUNCTION_ENTRY_RETURNED",
            "JIT_RETURN_VALUE value=42", "JIT_REWRITE_RW", "JIT_REWRITE_WRITTEN",
            "JIT_REWRITE_ICACHE_SYNCED", "JIT_REWRITE_RX", "JIT_REWRITE_ENTRY",
            "JIT_REWRITE_RETURN_VALUE value=4242"
        };
        long previous = -1;
        for (index = 0; index < (int)(sizeof(ordered) / sizeof(ordered[0])); index++) {
            long position;
            (void)snprintf(event, sizeof(event), "event=%s", ordered[index]);
            position = journal_position(probe_journal, event);
            CHECK(position >= 0, "the probe profile journals every mandatory checkpoint");
            CHECK(position > previous,
                  "the mandatory checkpoints appear in the order the code takes them");
            previous = position;
        }
    }
    printf("# checkpoint journals: safe=%d entry checkpoints, probe=%d (order verified)\n",
           (int)(sizeof(measurement_chain) / sizeof(measurement_chain[0])), 17);

    (void)unsetenv("PHASE02_TARGET_OVERRIDE");
    phase02_profile_request_mode(PHASE02_PROFILE_SAFE);
}

/* -------------------------------------- the iOS-class path, simulated on a host
 *
 * No host can be an iPhone, and nothing below is device evidence. What CAN be done here is
 * decisive for the investigation: put the process in the policy state a physical iPhone 13 is
 * in (execute permission not obtainable) and check that every selection still runs to
 * completion and produces an honest report - BLOCKED where the attempt is deferred, PASS
 * where the measurement is real, and IDENTICAL numbers for the suites that were never on the
 * JIT path (cpu, threads). The run that terminated the whole process is the one where the
 * attempt was taken; the run that produces a report is the one where it is deferred. */

static void test_ios_class_deferral_produces_a_report(void)
{
    run_totals_t platform_baseline[9];
    run_totals_t simulated[9];
    size_t index;

    (void)setenv("PHASE02_TARGET_OVERRIDE", "ios-device", 1);
    (void)unsetenv("PHASE02_ALLOW_EXECUTION");

    CHECK(phase02_execution_allowed(PHASE02_EXEC_OP_ENTER_WRITTEN_MEMORY) == 0,
          "simulated iOS-class target defers entering written memory");
    CHECK(phase02_execution_allowed(PHASE02_EXEC_OP_OBJECT_EXEC_VIEW) == 0,
          "simulated iOS-class target defers the executable object view");
    {
        char summary[256];
        CHECK(phase02_execution_policy_summary(summary, sizeof(summary)) > 0 &&
                  strstr(summary, "profile-source=override-simulation-not-device-evidence") != NULL,
              "the simulated profile is labelled as a simulation, never as device evidence");
        CHECK(strstr(phase02_execution_policy_summary(summary, sizeof(summary)) > 0 ? summary : "",
                     "target=ios-device(override)") != NULL,
              "the simulated profile names the target it stands in for");
    }
    /* The opt-in still opens the attempt - the device must be able to try, deliberately. */
    (void)setenv("PHASE02_ALLOW_EXECUTION", "1", 1);
    CHECK(phase02_execution_allowed(PHASE02_EXEC_OP_ENTER_WRITTEN_MEMORY) == 1,
          "the execution-attempt opt-in still allows the attempt on the iOS-class target");
    (void)unsetenv("PHASE02_ALLOW_EXECUTION");

    for (index = 0u; index < (sizeof(g_selections) / sizeof(g_selections[0])); index++) {
        char label[110];
        (void)snprintf(label, sizeof(label), "iOS-class run_selected(%s) returns a report",
                       g_selections[index]);
        CHECK(bridge_equivalent_run(g_selections[index], NULL, &simulated[index]) == 0 &&
                  simulated[index].had_text != 0,
              label);
        (void)snprintf(label, sizeof(label), "iOS-class run_selected(%s) reports no failures",
                       g_selections[index]);
        CHECK(simulated[index].fail == 0u, label);
    }
    describe("jit(ios)", &simulated[1]);
    describe("all(ios)", &simulated[8]);

    /* The deferred chain is named, record by record: no silent gap, no PASS. */
    {
        phase02_log_t *log = phase02_log_new();
        const char *text = NULL;
        static const char *const expected[] = {
            "TEST=jit.make_executable STATUS=BLOCKED",
            "TEST=jit.execute_return_42 STATUS=BLOCKED",
            "TEST=jit.rewrite_payload STATUS=BLOCKED",
            "TEST=jit.execute_return_4242 STATUS=BLOCKED",
            "TEST=jit.execution_allowed STATUS=BLOCKED",
        };
        CHECK(log != NULL, "a log for the record-level check");
        if (log != NULL) {
            phase02_log_init(log, "ios-class deferral");
            (void)phase02_run_suite("jit", log, g_workdir);
            (void)phase02_run_suite("loader", log, g_workdir);
            text = phase02_log_text(log);
            for (index = 0u; index < (sizeof(expected) / sizeof(expected[0])); index++) {
                CHECK(text != NULL && strstr(text, expected[index]) != NULL, expected[index]);
            }
            CHECK(text != NULL &&
                      strstr(text, "TEST=loader.run_valid_module STATUS=BLOCKED") != NULL,
                  "the loader's execution is deferred, with the module NOT rejected");
            CHECK(text != NULL && strstr(text, "EXECUTION_DEFERRED") == NULL,
                  "the deferral is reported through the report, not as a checkpoint string");
            phase02_log_free(log);
        }
    }

    /* Suites that never touched the JIT path must be identical in both profiles: the strongest
     * statement this host can make about cpu/threads. */
    (void)unsetenv("PHASE02_TARGET_OVERRIDE");
    for (index = 0u; index < (sizeof(g_selections) / sizeof(g_selections[0])); index++) {
        CHECK(bridge_equivalent_run(g_selections[index], NULL, &platform_baseline[index]) == 0,
              "baseline under the native profile");
    }
    {
        size_t cpu_index = 0u;
        size_t threads_index = 0u;
        for (index = 0u; index < (sizeof(g_selections) / sizeof(g_selections[0])); index++) {
            if (strcmp(g_selections[index], "cpu") == 0) {
                cpu_index = index;
            }
            if (strcmp(g_selections[index], "threads") == 0) {
                threads_index = index;
            }
        }
        CHECK(same_totals(&platform_baseline[cpu_index], &simulated[cpu_index]),
              "cpu reports identical numbers with the execution attempt deferred");
        CHECK(same_totals(&platform_baseline[threads_index], &simulated[threads_index]),
              "threads reports identical numbers with the execution attempt deferred");
        CHECK(!same_totals(&platform_baseline[1], &simulated[1]),
              "the jit suite does differ between the two profiles (the gate is really reached)");
    }
    CHECK(phase02_execution_allowed(PHASE02_EXEC_OP_ENTER_WRITTEN_MEMORY) == 1,
          "clearing the simulation restores this host's own profile");
}

/* -------------------------------------------------------- execution policy */

static void test_execution_policy(void)
{
    const phase02_execution_profile_t *profile = phase02_execution_profile_current();
    char summary[256];
    char tiny[4];

    /* The matrix a platform gate can produce, including the iOS row that no host can run. */
    CHECK(phase02_execution_decide(0, 0) == 0,
          "policy: a platform that cannot + no opt-in = DEFERRED (the iOS device row)");
    CHECK(phase02_execution_decide(0, 1) == 1,
          "policy: a platform that cannot + explicit opt-in = ATTEMPT (the dedicated run)");
    CHECK(phase02_execution_decide(1, 0) == 1,
          "policy: a platform that can = ATTEMPT even without the opt-in (Linux/macOS)");
    CHECK(phase02_execution_decide(1, 1) == 1, "policy: both = ATTEMPT");
    CHECK(strcmp(phase02_execution_decision_name(0), "DEFERRED") == 0 &&
              strcmp(phase02_execution_decision_name(1), "ATTEMPT") == 0,
          "policy: decisions have their distinct names");

    CHECK(profile != NULL && profile->name != NULL && profile->name[0] != '\0',
          "policy: the current target has a name");
    CHECK(profile != NULL && profile->platform_evidence != NULL &&
              profile->platform_evidence[0] != '\0',
          "policy: the current target states the evidence for its flags");
    CHECK(phase02_execution_reason(profile, PHASE02_EXEC_OP_ENTER_WRITTEN_MEMORY, 0) != NULL &&
              phase02_execution_reason(profile, PHASE02_EXEC_OP_OBJECT_EXEC_VIEW, 0) != NULL &&
              phase02_execution_reason(profile, PHASE02_EXEC_OP_ENTER_WRITTEN_MEMORY, 1) != NULL,
          "policy: every decision has a reason");
    CHECK(strstr(phase02_execution_reason(profile, PHASE02_EXEC_OP_ENTER_WRITTEN_MEMORY, 0),
                 "IPHONE13_PHYSICAL_RUN_03") != NULL,
          "policy: the deferral names the physical run it comes from");

    /* On this host the operations are allowed by default: the host keeps proving execution by
     * executing, exactly as before pass 04. */
    CHECK(phase02_execution_allowed(PHASE02_EXEC_OP_ENTER_WRITTEN_MEMORY) == 1,
          "policy: this host still attempts to enter written memory");
    CHECK(phase02_execution_allowed(PHASE02_EXEC_OP_OBJECT_EXEC_VIEW) == 1,
          "policy: this host still attempts the executable object view");

    /* Explicit requests are honoured both ways and are irrelevant where the platform allows. */
    phase02_execution_request_attempt(0);
    CHECK(phase02_execution_allowed(PHASE02_EXEC_OP_ENTER_WRITTEN_MEMORY) == 1,
          "policy: refusing the opt-in does not disable a platform that allows the operation");
    phase02_execution_request_attempt(1);
    CHECK(phase02_execution_allowed(PHASE02_EXEC_OP_ENTER_WRITTEN_MEMORY) == 1,
          "policy: the opt-in does not disable a platform that allows the operation");
    phase02_execution_request_attempt(0);

    CHECK(phase02_execution_policy_summary(summary, sizeof(summary)) > 0,
          "policy: the summary renders");
    CHECK(strstr(summary, "target=") != NULL && strstr(summary, "enter=") != NULL &&
              strstr(summary, "object-view=") != NULL && strstr(summary, "attempt-opt-in=") != NULL,
          "policy: the summary carries every field the report needs");
    CHECK(phase02_execution_policy_summary(NULL, sizeof(summary)) == -1,
          "policy: a NULL buffer is refused");
    CHECK(phase02_execution_policy_summary(tiny, sizeof(tiny)) == -1 && tiny[0] == '\0',
          "policy: a buffer that cannot hold the summary is refused, not truncated");
}

int main(void)
{
    char template_path[512];

    (void)snprintf(template_path, sizeof(template_path), "%s/phase02-run-selected-XXXXXX",
                   getenv("TMPDIR") != NULL ? getenv("TMPDIR") : "/tmp");
    if (mkdtemp(template_path) == NULL) {
        printf("FAIL could not create a work directory (%s)\n", strerror(errno));
        return 1;
    }
    (void)snprintf(g_workdir, sizeof(g_workdir), "%s", template_path);

    printf("# selected-suite regression: entry points phase02_run_suite/phase02_run_all, "
           "target=%s workdir=%s\n", rt_platform_name(), g_workdir);

    test_every_selection_runs();
    test_second_press_and_sequence();
    test_invalid_selection();
    test_ten_sequential_reruns();
    test_journal_and_write_ahead();
    test_execution_policy();
    test_ios_class_deferral_produces_a_report();
    test_profiles();
    test_checkpoint_sequence();

    printf("== %u checks, %u failures ==\n", g_checks, g_failures);
    (void)rmdir(g_workdir);
    return (g_failures == 0u) ? 0 : 1;
}
