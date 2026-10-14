/*
 * phase02_execution_policy.h — when may this target be asked to run freshly written memory?
 *
 * PHASE_02_RECONSTRUCTED_POC (pass 04, from IPHONE13_PHYSICAL_RUN_03).
 *
 * The measured fact that shapes everything here:
 *
 *   On iOS 26 on TXM/SPTM hardware (A15 and later — the iPhone 13 is A15) the platform strips
 *   the execute permission from a page whose protection was changed by the process itself,
 *   `mprotect(PROT_READ|PROT_EXEC)` can return success while the page stays non-executable,
 *   `MAP_JIT` is refused without Apple's `dynamic-codesigning` entitlement, and no public API
 *   lets a third-party app grant itself execute permission. Entering such a page is what the
 *   previous physical run did — and the whole process disappeared at that moment.
 *
 * Consequences, and they are policy decisions, not test results:
 *
 *   * the diagnostics run must be able to FINISH on such a target. A suite that cannot be
 *     executed to the end produces no evidence at all, which is worse than a documented
 *     deferral;
 *   * executing memory this process wrote, and creating an executable view of an app-owned
 *     object, are therefore DEFERRED by default on an iOS target, and recorded as BLOCKED with
 *     this policy named as the reason — never as PASS, and never as a platform verdict we did
 *     not measure;
 *   * the same operations stay ATTEMPTED on every other target (Linux, macOS, and the iOS
 *     target when the attempt is explicitly opted in), so no capability claim is weakened: the
 *     JIT chain and the loader still prove execution by executing, wherever executing is
 *     allowed to be attempted.
 *
 * The opt-in exists so a DEDICATED physical run can still try, while writing the report to
 * disk beforehand (see phase02_progress_write_ahead): if that run terminates, the measurements
 * up to the attempt are already saved and the journal names the checkpoint that was entered.
 * Sources, in order of precedence:
 *   1. the environment variable PHASE02_ALLOW_EXECUTION ("1"/"0");
 *   2. the compile-time PHASE02_IOS_ALLOW_EXECUTION_ATTEMPT (0 by default);
 *   3. the target default (attempt on non-iOS targets, defer on iOS).
 * The environment variable PHASE02_TARGET_OVERRIDE (ios-device | ios-simulator | apple-host |
 * portable) simulates another target's PROFILE so the fenced iOS rows can be exercised on a
 * host; the policy summary then reports profile-source=override-simulation-not-device-evidence,
 * because a simulated profile is not a measurement.
 *
 * The decisions are pure functions so every combination — including the iOS one, which no
 * host can reproduce — is unit-testable on any machine.
 */
#ifndef PHASE02_EXECUTION_POLICY_H
#define PHASE02_EXECUTION_POLICY_H

#ifdef __cplusplus
extern "C" {
#endif

/* The two kinds of operation the policy governs. They are separate because a platform may
 * allow one and refuse the other (iOS refuses both, macOS allows both). */
typedef enum {
    PHASE02_EXEC_OP_ENTER_WRITTEN_MEMORY = 0,  /* call into a buffer this process made executable */
    PHASE02_EXEC_OP_OBJECT_EXEC_VIEW    = 1   /* second, executable view of an app-owned object */
} phase02_exec_op_t;

/*
 * The two instrumentation profiles (pass 05). They change what the run is allowed to
 * ATTEMPT; they never change what a result means, and the report always states which profile
 * produced it.
 *
 *   SAFE              the default. Every operation that this target cannot be shown to
 *                     support is measured as far as measurement goes, and the attempt to
 *                     enter memory the process made executable is left to a dedicated run.
 *                     A full report is produced on any target, including the iPhone.
 *   EXECUTION_PROBE   for a run whose purpose is to answer the execution questions. The
 *                     attempt is taken, the checkpoints immediately before it are already on
 *                     disk (journal + write-ahead report), and the answers come back as
 *                     measurements: values returned by real calls, or a fault, or the
 *                     journal's last line. Nothing is assumed about the outcome.
 *
 * Selection, in order of precedence: the environment variable PHASE02_PROFILE
 * ("execution-probe" | "safe"), a programmatic request (the CLI flag / the bridge's
 * environment), the compile-time PHASE02_IOS_ALLOW_EXECUTION_ATTEMPT, then SAFE.
 */
typedef enum {
    PHASE02_PROFILE_SAFE = 0,
    PHASE02_PROFILE_EXECUTION_PROBE = 1,
    PHASE02_PROFILE_COUNT = 2
} phase02_profile_mode_t;

typedef struct phase02_execution_profile {
    const char *name;                          /* "ios-device", "macos", "linux", ... */
    int         is_ios_target;                 /* 1 for the iPhone target and simulator */
    int         platform_allows_enter;         /* 0 on iOS/TXM hardware (see the header note) */
    int         platform_allows_object_view;   /* 0 on iOS */
    const char *platform_evidence;             /* why the flags have these values */
} phase02_execution_profile_t;

/* ---- pure decisions (no state, no I/O: unit-tested for every platform combination) ---- */

/* 1 = attempt the operation, 0 = defer it. */
int phase02_execution_decide(int platform_allows, int attempt_enabled);

/* "ATTEMPT" / "DEFERRED". */
const char *phase02_execution_decision_name(int decision);

const char *phase02_profile_mode_name(phase02_profile_mode_t mode);

/* The mode this run is in (defaults to SAFE; see the note above for precedence). */
phase02_profile_mode_t phase02_profile_mode(void);

/* Requested explicitly (programmatic, e.g. --execution-probe), or 0. */
void phase02_profile_request_mode(phase02_profile_mode_t mode);
int  phase02_profile_mode_requested(void);

/* Human-readable reason for a decision about `operation` on `profile`. Never NULL, never
 * claims a platform verdict that was not measured. */
const char *phase02_execution_reason(const phase02_execution_profile_t *profile,
                                     phase02_exec_op_t operation, int decision);

/* ---- the profile of the target this binary is running on ---- */
const phase02_execution_profile_t *phase02_execution_profile_current(void);

/* ---- the attempt switch ---- */
/* Reads the environment first, then the compile-time default, then the target default. */
int  phase02_execution_attempt_enabled(void);
/* Test/diagnostic override (does not touch the environment). */
void phase02_execution_request_attempt(int enable);

/* Convenience: 1 when the operation may be attempted right now on this target. */
int phase02_execution_allowed(phase02_exec_op_t operation);

/* One-line summary for the report: "target=ios-device enter=DEFERRED object-view=DEFERRED
 * attempt-opt-in=0 profile=safe profile-source=native". The profile is always named: a
 * report can never be read without knowing whether the attempt was allowed. */
int phase02_execution_policy_summary(char *out, unsigned long cap);

#ifdef __cplusplus
}
#endif

#endif /* PHASE02_EXECUTION_POLICY_H */
