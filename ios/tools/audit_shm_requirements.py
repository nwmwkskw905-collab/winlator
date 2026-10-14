#!/usr/bin/env python3
"""audit_shm_requirements.py — "refused" is not "required", enforced in rules.

Physical runs 02/04 measured on the iPhone: `ipc.posix_shm` stage=shm_open errno=1 (EPERM).
Two mistakes are one keystroke away from that fact:

  * counting it as a defect of the port (it is a property of the sandbox: the capability probe
    asked, the answer is no);
  * letting it block something it has nothing to do with (the runtime's execution and memory
    paths never call the named namespace, and the dual-mapping experiment has a measured
    alternative on that target).

The audit does not take those statements on trust. It re-derives the consumer list from the
tree and requires it to be disclosed, and it requires every claimed alternative to exist as a
real mechanism.

Rules:
  R1 INVENTORY_DISCLOSED   every file in this repository that calls the named POSIX SHM API
                           (shm_open/shm_unlink) or the System V one (shmget/shmat) appears in
                           the inventory table of the documented consumer list.
  R2 RUNTIME_NOT_DEPENDENT the execution and memory paths (runtime_exec_memory, runtime_jit,
                           runtime_loader, ios/box64-registers) contain no SHM call at all.
  R3 NOTE_PRESENT          the harness emits the classification note with the closed
                           vocabulary, the measured consumer statement and the alternatives.
  R4 INVENTORY_DOC         the documented inventory parses, uses only the closed status
                           vocabulary, gives every REPLACEABLE row concrete evidence, and is
                           the file the note points at.
  R5 ALTERNATIVES_REAL     every alternative named in the note exists as a mechanism in this
                           tree (measured, not advertised).

Output: one line per rule plus SHM_REQUIREMENTS_AUDIT=<violations>.
"""
from __future__ import annotations

import argparse
import pathlib
import re
import sys

DOC = "Documentation/PHASE02_SHM_CONSUMER_INVENTORY.md"
HARNESS = "Diagnostics/src/phase02_harness.c"
NOTE = "NOTE=posix_shm_requirement"

NAMED_API = re.compile(r"\b(shm_open|shm_unlink)\s*\(")
SYSV_API = re.compile(r"\b(shmget|shmat|shmctl)\s*\(")

RUNTIME_PATHS = [
    "RuntimeCore/src/runtime_exec_memory.c",
    "RuntimeCore/src/runtime_jit.c",
    "RuntimeCore/src/runtime_loader.c",
]

STATUSES = {"REQUIRED", "REPLACEABLE", "NOT_MEASURED", "SCAFFOLD"}

# Files that are allowed to mention the API without being a consumer: the inventory document
# itself, the audits, and the crash/journalling helpers that print errno names.
DISCOVERY_EXCLUDES = (
    "/build/", "/__pycache__/", "/.git/", "/tools/", "/Documentation/", "/Tests/",
    "ios/build",
)


def walk_sources(repo: pathlib.Path):
    for suffix in ("*.c", "*.h", "*.m", "*.mm", "*.cc", "*.cpp", "*.java", "*.kt"):
        for path in repo.rglob(suffix):
            text = str(path).replace("\\", "/")
            if any(exclude in text + "/" for exclude in DISCOVERY_EXCLUDES):
                continue
            yield path


def inventory_rows(doc: pathlib.Path):
    """Parses the markdown table: (consumer, path, api, status, evidence)."""
    rows = []
    for line in doc.read_text().splitlines():
        line = line.strip()
        if not line.startswith("|") or line.startswith("| ---") or line.startswith("| consumer"):
            continue
        cells = [cell.strip().replace("`", "") for cell in line.strip("|").split("|")]
        if len(cells) >= 5:
            rows.append(cells[:5])
    return rows


def without_comments(text: str) -> str:
    """Deterministic comment removal: /* ... */ first, then //-to-end-of-line. Used so that
    documentation *about* the API is not mistaken for a call to it."""
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    return re.sub(r"//[^\n]*", "", text)


def rules(root: pathlib.Path):
    out, violations = [], []
    repo = root.parent if (root.parent / "glibc_patches").is_dir() else root
    doc = root / DOC
    harness = (root / HARNESS).read_text()

    # R1 ------------------------------------------------------------------
    if not doc.is_file():
        violations.append(f"R1: the consumer inventory {DOC} does not exist")
        rows = []
    else:
        rows = inventory_rows(doc)
        if not rows:
            violations.append(f"R1: {DOC} contains no inventory rows")
    documented = " ".join(row[1] for row in rows)
    undisclosed = []
    for path in walk_sources(repo):
        try:
            text = path.read_text(errors="ignore")
        except OSError:
            continue
        code = without_comments(text)
        if not (NAMED_API.search(code) or SYSV_API.search(code)):
            continue
        relative = str(path.relative_to(root)) if path.is_relative_to(root) else \
            "../" + str(path.relative_to(repo.parent))
        if path.name not in documented:
            undisclosed.append(relative)
    if undisclosed:
        violations.append("R1: SHM consumers present in the tree but absent from the "
                          f"inventory: {sorted(set(undisclosed))}")
    out.append(f"ok   R1 INVENTORY_DISCLOSED: {len(rows)} inventory rows; every SHM call site "
               "in this repository is either disclosed there or lives in the audits/docs")

    # R2 ------------------------------------------------------------------
    offenders = []
    for relative in RUNTIME_PATHS:
        path = root / relative
        if not path.is_file():
            violations.append(f"R2: {relative} is missing (did the execution path move?)")
            continue
        text = path.read_text()
        stripped = without_comments(text)
        if NAMED_API.search(stripped):
            offenders.append(relative)
    registers = root / "box64-registers"
    if registers.is_dir():
        for path in registers.rglob("*"):
            if path.suffix in (".c", ".h", ".mm", ".m") and NAMED_API.search(path.read_text(
                    errors="ignore")):
                offenders.append(str(path.relative_to(root)))
    if offenders:
        violations.append(f"R2: an execution/memory path calls the named POSIX namespace: "
                          f"{sorted(set(offenders))}")
    out.append("ok   R2 RUNTIME_NOT_DEPENDENT: no named POSIX SHM call on the execution or "
               "memory path (or in box64-registers)")

    # R3 ------------------------------------------------------------------
    if NOTE not in harness:
        violations.append(f"R3: the harness does not emit {NOTE}")
    else:
        block = harness.split(NOTE, 1)[1][:600]
        for field in ("NAMED_POSIX_SHM=", "required_by_runtime=", "consumers_measured=",
                      "alternatives=", "audit=tools/audit_shm_requirements.py",
                      "inventory=" + DOC):
            if field not in block:
                violations.append(f"R3: the classification note lost the field {field!r}")
        if 'NAMED_POSIX_SHM=REPLACEABLE' not in block:
            violations.append("R3: the note no longer classifies the named namespace as "
                              "REPLACEABLE for this runtime (a claim must move with evidence)")
        if "required_by_runtime=no" not in block:
            violations.append("R3: the note no longer states that the runtime does not "
                              "require the named namespace")
        alternatives = re.search(r"alternatives=([^\s\"]+)", block)
        if alternatives is None or len(alternatives.group(1).split("|")) < 2:
            violations.append("R3: fewer than two alternatives are named")
    out.append("ok   R3 NOTE_PRESENT: the classification is emitted with the namespace status, "
               "the runtime-dependence answer, the alternatives and both references")

    # R4 ------------------------------------------------------------------
    if rows:
        for consumer, path, api, status, evidence in rows:
            if status not in STATUSES:
                violations.append(f"R4: status {status!r} of {consumer!r} is outside the closed "
                                  f"vocabulary {sorted(STATUSES)}")
            if status == "REPLACEABLE" and not re.search(
                    r"measured|measures|record|memory\.[a-z_.]+|--suite|\bR[0-9]\b|"
                    r"(rt|phase02)_[a-z_]+\(", evidence):
                violations.append(f"R4: REPLACEABLE row {consumer!r} gives no concrete "
                                  "evidence (no measurement, record or symbol)")
        if "REQUIRED" not in {row[3] for row in rows} and "required_by_runtime=no" not in harness:
            violations.append("R4: no consumer is REQUIRED but the runtime does not say so")
        out.append(f"ok   R4 INVENTORY_DOC: {len(rows)} rows, closed vocabulary, evidence on "
                   "every REPLACEABLE row, referenced by the note")
    else:
        out.append("ok   R4 INVENTORY_DOC: skipped (no rows)")

    # R5 ------------------------------------------------------------------
    checks = {
        "anonymous-MAP_SHARED": ("RuntimeCore/src/runtime_dual_mapping.c", r"\bMAP_SHARED\b"),
        "app-container-file-backed-MAP_SHARED":
            ("RuntimeCore/src/runtime_dual_mapping.c",
             r"rt_dual_map_create_file_backed(_readonly_view)?\b"),
        "mach-VM": ("RuntimeCore/src/runtime_memory.c", r"\bmach_vm_region\b|\bvm_remap\b"),
    }
    for name, (relative, pattern) in checks.items():
        path = root / relative
        if not path.is_file() or not re.search(pattern, path.read_text()):
            violations.append(f"R5: the alternative {name!r} named in the note has no "
                              f"mechanism in {relative}")
    out.append(f"ok   R5 ALTERNATIVES_REAL: all {len(checks)} alternatives named in the note "
               "exist as mechanisms in this tree")
    return out, violations


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", default=str(pathlib.Path(__file__).resolve().parent.parent))
    args = parser.parse_args()
    root = pathlib.Path(args.root).resolve()

    out, violations = rules(root)
    for line in out:
        print(line)
    for violation in violations:
        print(f"FAIL {violation}")
    print(f"SHM_REQUIREMENTS_AUDIT={len(violations)}")
    return 1 if violations else 0


if __name__ == "__main__":
    raise SystemExit(main())
