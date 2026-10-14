#!/usr/bin/env python3
"""pass05_negative_controls.py — the pass-05 detectors, fired on purpose.

Pass 05 introduced a second execution profile, an executable-memory abstraction with a single
write/transition/execute protocol, a capability graph, a read-only second view for the
dual-mapping experiment and a classification for the named POSIX namespace. Every one of those
is a place where a later edit could turn "we measured X" into "we assume X", and the audits and
unit tests are only worth their runtime if they can fail. Each control below reintroduces ONE
such defect in the shape it would really take - the shape a "make it green" edit takes - runs
the detectors, and restores the file byte-for-byte (the script verifies the SHA-256 of every
touched file and rebuilds the affected test binaries at the end).

  A  the safe profile stops deferring the entry                  -> run-selected tests
  B  the entry returns a fabricated 42 without calling the code  -> exec-memory tests
  C  the rewrite is executed without a new icache flush          -> J7 (causal graph audit)
  D  the executable protection is presumed, not observed         -> exec-memory tests
  E  the loader PASS stops carrying the executed value           -> J8
  F  the named POSIX refusal is recorded as a defect of the port -> S6 (SHM backend audit)
  G  an execution path starts requiring the named namespace      -> R2 (SHM requirements audit)
  H  the capability graph lists a dependency twice               -> exec-memory tests
  I  the requested entitlement is recorded as granted            -> E3 (entitlement audit)
  J  an experiment record is classified as an execution record   -> C4 (capability graph audit)

Usage: python3 tools/pass05_negative_controls.py [--skip-build] [--root <dir>]
"""
from __future__ import annotations

import argparse
import hashlib
import pathlib
import subprocess
import sys

POLICY = "Diagnostics/src/phase02_execution_policy.c"
EXEC_MEMORY = "RuntimeCore/src/runtime_exec_memory.c"
DUAL = "RuntimeCore/src/runtime_dual_mapping.c"
HARNESS = "Diagnostics/src/phase02_harness.c"
LOADER = "RuntimeCore/src/runtime_loader.c"
GRAPH = "Diagnostics/src/phase02_capability_graph.c"

JIT_AUDIT = "tools/audit_jit_causal_graph.py"
SHM_AUDIT = "tools/audit_shm_backend.py"
SHM_REQ_AUDIT = "tools/audit_shm_requirements.py"
ENTITLEMENT_AUDIT = "tools/audit_entitlement_config.py"
GRAPH_AUDIT = "tools/audit_capability_graph.py"

RUN_SELECTED = "phase02_run_selected_tests"
EXEC_MEMORY_TESTS = "phase02_exec_memory_tests"


def sha256(path: pathlib.Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def patch(text: str, old: str, new: str, what: str) -> str:
    assert text.count(old) == 1, f"anchor not unique for {what}: {old[:70]!r}"
    return text.replace(old, new, 1)


class Runner:
    def __init__(self, root: pathlib.Path, skip_build: bool):
        self.root = root
        self.skip_build = skip_build
        self.audit_runs = 0
        self.test_runs = 0

    def audit(self, tool: str):
        self.audit_runs += 1
        proc = subprocess.run([sys.executable, str(self.root / tool)], cwd=self.root,
                              capture_output=True, text=True)
        return proc.returncode, proc.stdout + proc.stderr

    def build(self, target: str):
        return subprocess.run(["cmake", "--build", "build/host", "--target", target],
                              cwd=self.root, capture_output=True, text=True)

    def tests(self, target: str):
        if self.skip_build:
            return None, "(builds skipped)"
        self.test_runs += 1
        build = self.build(target)
        if build.returncode != 0:
            return ["<the target does not build>"], build.stdout + build.stderr
        run = subprocess.run([str(self.root / "build/host" / target)], cwd=self.root,
                             capture_output=True, text=True)
        failures = [line.split("FAIL ", 1)[1] for line in run.stdout.splitlines()
                    if line.startswith("FAIL ")]
        return failures, run.stdout


class Controls:
    def __init__(self, runner: Runner):
        self.runner = runner
        self.results = []

    def record(self, name: str, detected: bool, detail: str):
        self.results.append((name, detected))
        print(f"{'ok  ' if detected else 'FAIL'} control {name}: {detail}")

    def control(self, name: str, path: str, mutate, expect: str, detector: str,
                test_target: str | None = None):
        target = self.runner.root / path
        before = sha256(target)
        original = target.read_text()
        try:
            target.write_text(mutate(original))
            if test_target is not None:
                failures, log = self.runner.tests(test_target)
                if failures is None:
                    self.record(name, False, "tests skipped; control not evaluated")
                    return
                hit = any(expect in failure for failure in failures)
                self.record(name, hit,
                            (f"{test_target} reported {failures[0]!r}" if hit else
                             f"{test_target} did NOT report {expect!r} (failures: {failures})"))
                return
            rc, output = self.runner.audit(detector)
            hit = expect in output
            line = next((l for l in output.splitlines() if expect in l), "")
            self.record(name, hit,
                        (f"detected by {detector}: {line[:120]!r}" if hit else
                         f"{detector} did NOT report {expect!r} (rc={rc})"))
        finally:
            target.write_text(original)
            assert sha256(target) == before, f"{path} was not restored byte-for-byte"

    def detector_is_green(self, name: str, detector: str):
        rc, output = self.runner.audit(detector)
        self.record(name, rc == 0, f"{detector} {'green' if rc == 0 else 'RED'} after restore")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", default=str(pathlib.Path(__file__).resolve().parent.parent))
    parser.add_argument("--skip-build", action="store_true")
    args = parser.parse_args()
    root = pathlib.Path(args.root).resolve()
    runner = Runner(root, args.skip_build)
    controls = Controls(runner)

    print("== pass 05 negative controls (every detector must fire, then the tree is restored)")

    # A — the safe profile stops deferring the entry.
    controls.control(
        "A_execute_while_deferred", POLICY,
        lambda text: patch(text,
                           "    int platform_allows = (operation == "
                           "PHASE02_EXEC_OP_ENTER_WRITTEN_MEMORY)\n"
                           "                              ? profile->platform_allows_enter\n"
                           "                              : profile->platform_allows_object_view;",
                           "    int platform_allows = 1;\n"
                           "    (void)profile;\n"
                           "    (void)operation;",
                           "profile ignored"),
        "SAFE defers both fenced operations on an iOS-class target", RUN_SELECTED,
        test_target=RUN_SELECTED)

    # B — the entry returns a fabricated 42 without calling the code.
    controls.control(
        "B_fabricated_value_without_call", EXEC_MEMORY,
        lambda text: patch(text,
                           "    target = (void *)((uint8_t *)arena->base + entry_offset);\n"
                           "    memcpy(&fn, &target, sizeof(fn));\n"
                           "    return rt_signal_call_guarded(fn, value_out, fault_addr_out, "
                           "err_out);",
                           "    target = (void *)((uint8_t *)arena->base + entry_offset);\n"
                           "    memcpy(&fn, &target, sizeof(fn));\n"
                           "    (void)fn;\n"
                           "    if (value_out != NULL) {\n"
                           "        *value_out = 42u;      /* fabricated, never executed */\n"
                           "    }\n"
                           "    if (fault_addr_out != NULL) {\n"
                           "        *fault_addr_out = NULL;\n"
                           "    }\n"
                           "    return 0;",
                           "fabricated return value"),
        "the rewritten payload returned 4242 from real execution", EXEC_MEMORY_TESTS,
        test_target=EXEC_MEMORY_TESTS)

    # C — the rewritten payload is executed without a new icache flush.
    controls.control(
        "C_rewrite_icache_sync_removed", HARNESS,
        lambda text: patch(text,
                           "    if (phase02_jit_sync_icache(log, arena, second_len, "
                           "PHASE02_ICACHE_REWRITE) != 0) {\n"
                           "        phase02_jit_release(log, arena, arena_len);\n"
                           "        return;\n"
                           "    }\n",
                           "", "rewrite icache sync"),
        "J7: the rewritten payload is executed without a new icache flush", JIT_AUDIT)

    # D — the executable protection is presumed instead of observed.
    controls.control(
        "D_protection_presumed_not_observed", EXEC_MEMORY,
        lambda text: patch(text,
                           "    return rt_mem_protection_observed(arena->base, arena->len, "
                           "bits_out, err_out);",
                           "    /* Presumed: the caller asked for R-X, so report R-X. */\n"
                           "    *bits_out = RT_MEM_OBSERVED_READ | RT_MEM_OBSERVED_EXEC;\n"
                           "    if (err_out != NULL) {\n"
                           "        *err_out = 0;\n"
                           "    }\n"
                           "    return 0;",
                           "presumed protection"),
        "a fresh arena is not executable (no W+X mapping is ever created)", EXEC_MEMORY_TESTS,
        test_target=EXEC_MEMORY_TESTS)

    # E — the loader PASS stops carrying the executed value.
    controls.control(
        "E_loader_pass_without_value", HARNESS,
        lambda text: patch(text, '"module executed, entry returned %u (expected 7)", value);',
                           '"module executed (value not checked)");', "loader PASS value"),
        "J8: the loader PASS does not carry the value", JIT_AUDIT)

    # F — the named POSIX refusal is recorded as a defect of the port.
    controls.control(
        "F_refusal_recorded_as_port_defect", HARNESS,
        lambda text: patch(text, 'not \\"shared memory is impossible\\"',
                           'the named namespace is required by this port', "refusal wording"),
        "S6: the log does not separate the named namespace from shared memory", SHM_AUDIT)

    # G — an execution path starts requiring the named namespace.
    controls.control(
        "G_execution_path_requires_named_shm", LOADER,
        lambda text: patch(text, "#include \"runtime_loader.h\"",
                           "#include \"runtime_loader.h\"\n"
                           "#include <sys/mman.h>\n"
                           "\n"
                           "static int rt_loader_named_object_probe(void)\n"
                           "{\n"
                           "    int fd = shm_open(\"/rt_loader_scratch\", O_CREAT | O_RDWR, 0600);\n"
                           "    if (fd < 0) {\n"
                           "        return -1;   /* the loader now needs the named namespace */\n"
                           "    }\n"
                           "    (void)shm_unlink(\"/rt_loader_scratch\");\n"
                           "    return close(fd);\n"
                           "}\n",
                           "named namespace requirement"),
        "R2: an execution/memory path calls the named POSIX namespace", SHM_REQ_AUDIT)

    # H — the capability graph lists a dependency twice.
    controls.control(
        "H_dependency_counted_twice", GRAPH,
        lambda text: patch(text,
                           "    { PHASE02_NODE_EFFECTIVE_EXECUTE, PHASE02_NODE_LOADER, "
                           "PHASE02_EDGE_REQUIRED,",
                           "    { PHASE02_NODE_EFFECTIVE_EXECUTE, PHASE02_NODE_LOADER, "
                           "PHASE02_EDGE_REQUIRED,\n"
                           "      \"duplicated on purpose (negative control)\" },\n"
                           "    { PHASE02_NODE_EFFECTIVE_EXECUTE, PHASE02_NODE_LOADER, "
                           "PHASE02_EDGE_REQUIRED,",
                           "duplicated edge"),
        "no dependency is listed twice", EXEC_MEMORY_TESTS,
        test_target=EXEC_MEMORY_TESTS)

    # I — the requested entitlement is recorded as granted.
    controls.control(
        "I_requested_entitlement_as_granted", HARNESS,
        lambda text: patch(text,
                           '"L3_granted_to_signature=NOT observable in-process; read it off the built "',
                           '"L3_granted_to_signature=the entitlement is granted by the profile; '
                           'read it off the built "',
                           "grant claim"),
        "claims a granted capability", ENTITLEMENT_AUDIT)

    # J — an experiment record is classified as an execution-chain record.
    controls.control(
        "J_experiment_record_as_execution", GRAPH,
        lambda text: patch(text,
                           '    return (strncmp(record, "jit.", 4u) == 0 || '
                           'strncmp(record, "loader.", 7u) == 0) ? 1 : 0;',
                           '    return (strncmp(record, "jit.", 4u) == 0 || '
                           'strncmp(record, "loader.", 7u) == 0 ||\n'
                           '            strncmp(record, "memory.", 7u) == 0) ? 1 : 0;',
                           "classifier scope"),
        "an experiment record would be counted as an execution-chain blocker", GRAPH_AUDIT)

    # Every detector must still be green on the restored tree, and the rebuilt test binaries
    # (their sources were mutated and restored) must pass again.
    print("-- restored tree")
    for tool in (JIT_AUDIT, SHM_AUDIT, SHM_REQ_AUDIT, ENTITLEMENT_AUDIT, GRAPH_AUDIT):
        controls.detector_is_green(f"green_after_restore_{pathlib.Path(tool).stem}", tool)
    if not args.skip_build:
        for target in (RUN_SELECTED, EXEC_MEMORY_TESTS):
            failures, log = runner.tests(target)
            ok = failures is not None and not failures
            controls.record(f"green_after_restore_{target}", ok,
                            f"{target}: {'all checks pass' if ok else failures}")

    fired = sum(1 for _, detected in controls.results if detected)
    total = len(controls.results)
    print(f"PASS05_NEGATIVE_CONTROLS={fired}/{total}")
    return 0 if fired == total else 1


if __name__ == "__main__":
    raise SystemExit(main())
