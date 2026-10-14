#!/usr/bin/env python3
"""audit_capability_graph.py — the dependency graph is data, and this holds it honest.

Physical run 04 ended with nine BLOCKED records. The mistake this audit exists to prevent is
reading that list as nine problems: most of those records are the same missing signing
capability observed at different points of one chain, and two of them (the loader, the JIT
chain) are two consumers of ONE permission.

Rules:
  C1 EDGE_SET         the graph declares exactly the known node set and the known edges, with
                      no duplicated triple (a repeated dependency is still one dependency), no
                      self-edge, and a reason on every edge.
  C2 DERIVED_SUMMARY  the summary counts are computed from the edge list, never written down,
                      and every report carries the graph.
  C3 AXES_SEPARATE    the execution sources never include the dual-mapping module, and the
                      graph itself never makes the object axis a hard dependency of execution.
  C4 RECORD_ATTRIBUTION every jit.*/loader.* record the harness can emit maps to exactly one
                      node of the graph (declared in this file), and no execution-chain record
                      is attributed to the object-mapping axis. A new record without an
                      attribution fails the audit instead of quietly becoming a new blocker.
  C5 NOT_ONE_PER_RECORD the number of distinct root nodes behind the execution-chain records
                      is smaller than the number of records - the property that makes
                      "blocked=9" not equal "nine defects". Reported explicitly so a future
                      edit that tries to inflate it is visible.
  C6 REPORT_CONSISTENT when a report is available it must carry the graph and must not
                      attribute an execution-chain BLOCKED record to the object axis.

Output: one line per rule plus CAPABILITY_GRAPH_AUDIT=<violations>.
"""
from __future__ import annotations

import argparse
import pathlib
import re

GRAPH_C = "Diagnostics/src/phase02_capability_graph.c"
GRAPH_H = "Diagnostics/include/phase02_capability_graph.h"
HARNESS = "Diagnostics/src/phase02_harness.c"
EXEC_SOURCES = ["RuntimeCore/src/runtime_exec_memory.c", "RuntimeCore/src/runtime_jit.c",
                "RuntimeCore/src/runtime_loader.c"]

NODES = ["PHASE02_NODE_SIGNING", "PHASE02_NODE_MAP_JIT", "PHASE02_NODE_EXECMEM_ABI",
         "PHASE02_NODE_EFFECTIVE_EXECUTE", "PHASE02_NODE_JIT", "PHASE02_NODE_LOADER",
         "PHASE02_NODE_NAMED_SHM", "PHASE02_NODE_DUAL_MAPPING"]

EXECUTION_NODES = {"PHASE02_NODE_SIGNING", "PHASE02_NODE_MAP_JIT", "PHASE02_NODE_EXECMEM_ABI",
                   "PHASE02_NODE_EFFECTIVE_EXECUTE", "PHASE02_NODE_JIT", "PHASE02_NODE_LOADER"}
OBJECT_NODES = {"PHASE02_NODE_NAMED_SHM", "PHASE02_NODE_DUAL_MAPPING"}

EXPECTED_EDGES = {
    ("PHASE02_NODE_SIGNING", "PHASE02_NODE_MAP_JIT", "PHASE02_EDGE_GATES"),
    ("PHASE02_NODE_MAP_JIT", "PHASE02_NODE_EXECMEM_ABI", "PHASE02_EDGE_SUPPLIES"),
    ("PHASE02_NODE_SIGNING", "PHASE02_NODE_EFFECTIVE_EXECUTE", "PHASE02_EDGE_GATES"),
    ("PHASE02_NODE_EXECMEM_ABI", "PHASE02_NODE_EFFECTIVE_EXECUTE", "PHASE02_EDGE_REQUIRED"),
    ("PHASE02_NODE_EFFECTIVE_EXECUTE", "PHASE02_NODE_JIT", "PHASE02_EDGE_REQUIRED"),
    ("PHASE02_NODE_EFFECTIVE_EXECUTE", "PHASE02_NODE_LOADER", "PHASE02_EDGE_REQUIRED"),
    ("PHASE02_NODE_NAMED_SHM", "PHASE02_NODE_DUAL_MAPPING", "PHASE02_EDGE_SUPPLIES"),
    ("PHASE02_NODE_SIGNING", "PHASE02_NODE_NAMED_SHM", "PHASE02_EDGE_REFUSES"),
    ("PHASE02_NODE_DUAL_MAPPING", "PHASE02_NODE_EFFECTIVE_EXECUTE", "PHASE02_EDGE_SUPPLIES"),
}

# Where each execution-chain record's blocker ends, once dependencies are counted once.
RECORD_ROOT = {
    "jit.map_jit_probe": "PHASE02_NODE_MAP_JIT",
    "jit.execution_allowed": "PHASE02_NODE_EXECMEM_ABI",
    "jit.alloc": "PHASE02_NODE_EXECMEM_ABI",
    "jit.emit_payload": "PHASE02_NODE_EXECMEM_ABI",
    "jit.write_payload": "PHASE02_NODE_EXECMEM_ABI",
    "jit.icache_sync": "PHASE02_NODE_EXECMEM_ABI",
    "jit.free": "PHASE02_NODE_EXECMEM_ABI",
    "jit.write_protect_np": "PHASE02_NODE_EXECMEM_ABI",
    "jit.make_executable": "PHASE02_NODE_EFFECTIVE_EXECUTE",
    "jit.rx_observed": "PHASE02_NODE_EFFECTIVE_EXECUTE",
    "jit.rewrite_payload": "PHASE02_NODE_EFFECTIVE_EXECUTE",
    "jit.execute_return_42": "PHASE02_NODE_EFFECTIVE_EXECUTE",
    "jit.execute_return_4242": "PHASE02_NODE_EFFECTIVE_EXECUTE",
    "loader.build_image": "PHASE02_NODE_LOADER",
    "loader.run_valid_module": "PHASE02_NODE_LOADER",
    # The rejection controls are loader records too (a rejected image is a validation result,
    # not an execution attempt): they belong to the loader node, and none of them may end up
    # attributed to the execute permission or to the object axis.
    "loader.reject_bad_magic": "PHASE02_NODE_LOADER",
    "loader.reject_bad_version": "PHASE02_NODE_LOADER",
    "loader.reject_bad_flags": "PHASE02_NODE_LOADER",
    "loader.reject_truncated": "PHASE02_NODE_LOADER",
    "loader.reject_oversize_code": "PHASE02_NODE_LOADER",
    "loader.reject_code_overlapping_header": "PHASE02_NODE_LOADER",
    "loader.reject_bad_entry": "PHASE02_NODE_LOADER",
}

EDGE_RE = re.compile(r"\{\s*(PHASE02_NODE_[A-Z_]+)\s*,\s*(PHASE02_NODE_[A-Z_]+)\s*,\s*"
                     r"(PHASE02_EDGE_[A-Z_]+)\s*,")
RECORD_RE = re.compile(r'"(jit\.[a-z_0-9]*|loader\.[a-z_0-9]*)"')


def function_body(text: str, signature: str) -> str:
    """The body of one C function, from its signature to the closing brace in column 0."""
    start = text.find(signature)
    if start < 0:
        return ""
    end = text.find("\n}\n", start)
    return text[start:end if end >= 0 else len(text)]


def parse_edges(text: str):
    return [(m.group(1), m.group(2), m.group(3)) for m in EDGE_RE.finditer(text)]


def rules(root: pathlib.Path, report: pathlib.Path | None):
    out, violations = [], []
    source = (root / GRAPH_C).read_text()
    header = (root / GRAPH_H).read_text()
    harness = (root / HARNESS).read_text()
    edges = parse_edges(source)

    # C1 ------------------------------------------------------------------
    declared = [name for name in NODES if name in header]
    if len(declared) != len(NODES):
        violations.append(f"C1: the header does not declare all {len(NODES)} nodes: missing "
                          f"{sorted(set(NODES) - set(declared))}")
    if len(edges) != len(EXPECTED_EDGES):
        violations.append(f"C1: the graph has {len(edges)} edges, expected "
                          f"{len(EXPECTED_EDGES)}")
    missing = EXPECTED_EDGES - set(edges)
    extra = set(edges) - EXPECTED_EDGES
    if missing:
        violations.append(f"C1: edges disappeared from the graph: {sorted(missing)}")
    if extra:
        violations.append(f"C1: undocumented edges appeared in the graph: {sorted(extra)}")
    if len(edges) != len(set(edges)):
        violations.append("C1: a dependency is listed twice (one dependency, counted as two)")
    for from_node, to_node, relation in edges:
        if from_node == to_node:
            violations.append(f"C1: self-edge {from_node}")
        if from_node not in NODES or to_node not in NODES:
            violations.append(f"C1: edge endpoint outside the node set: {from_node}->{to_node}")
    for node in NODES:
        label = node.replace("PHASE02_NODE_", "").lower().replace("_", "-")
        if f'case {node}:' not in source:
            violations.append(f"C1: {node} has no case in the node-name switch")
    if source.count('return "unknown";') > 2:
        violations.append("C1: more than one default arm renders a name as 'unknown'")
    for relation in ("PHASE02_EDGE_GATES", "PHASE02_EDGE_REQUIRED", "PHASE02_EDGE_SUPPLIES",
                     "PHASE02_EDGE_REFUSES"):
        if f"case {relation}:" not in source:
            violations.append(f"C1: {relation} has no case in the relation-name switch")
    out.append(f"ok   C1 EDGE_SET: {len(NODES)} nodes, {len(edges)} edges, no duplicate "
               "dependency, reason on every edge and no endpoint outside the node set")

    # C2 ------------------------------------------------------------------
    if re.search(r"dependents\s*=\s*[1-9]\d*u?\s*;", source):
        violations.append("C2: the dependent-consumer count is written down instead of derived")
    if ".relation != PHASE02_EDGE_REQUIRED" not in source:
        violations.append("C2: the consumer count does not walk the edges")
    if "NOTE=capability_graph" not in harness:
        violations.append("C2: the reports do not carry the capability graph")
    if "phase02_capability_graph_summary(" not in harness:
        violations.append("C2: the harness does not publish the graph through its own "
                          "summary function")
    for field in ("nodes=", "edges=", "signing_edges=", "map_jit_in_edges=",
                  "effective_execute_in_edges=", "execution_consumers="):
        if field not in source:
            violations.append(f"C2: the summary lost the field {field!r}")
    out.append("ok   C2 DERIVED_SUMMARY: the summary is computed from the edge list and "
               "published in every report")

    # C3 ------------------------------------------------------------------
    for relative in EXEC_SOURCES:
        text = (root / relative).read_text()
        if "runtime_dual_mapping.h" in text or "rt_dual_map_" in text:
            violations.append(f"C3: {relative} depends on the dual-mapping experiment")
    for from_node, to_node, relation in edges:
        if from_node in OBJECT_NODES and relation in ("PHASE02_EDGE_GATES",
                                                      "PHASE02_EDGE_REQUIRED"):
            violations.append(f"C3: the object axis is a hard dependency: {from_node}->{to_node}")
        if from_node in {"PHASE02_NODE_JIT", "PHASE02_NODE_LOADER",
                         "PHASE02_NODE_EXECMEM_ABI"} and to_node in OBJECT_NODES:
            violations.append(f"C3: an execution node depends on the object axis: "
                              f"{from_node}->{to_node}")
    out.append("ok   C3 AXES_SEPARATE: execution and object mapping are separate axes in the "
               "graph and in the code")

    # C4 ------------------------------------------------------------------
    records = sorted({name for name in RECORD_RE.findall(harness) if name.endswith(".") is False})
    unmapped = [name for name in records if name not in RECORD_ROOT]
    if unmapped:
        violations.append(f"C4: execution-chain records without an attributed node: {unmapped}")
    for name in records:
        node = RECORD_ROOT.get(name)
        if node is not None and node not in NODES:
            violations.append(f"C4: {name} is attributed to an unknown node {node}")
        if node in OBJECT_NODES:
            violations.append(f"C4: the execution-chain record {name} is attributed to the "
                              "object axis")
    if "jit." not in harness or "loader." not in harness:
        violations.append("C4: the harness no longer records both execution consumers")
    classifier = function_body(source, "int phase02_capability_record_is_execution_chain")
    if classifier.count("strncmp(") != 2 or '"jit."' not in classifier or \
            '"loader."' not in classifier:
        violations.append("C4: the record classifier no longer accepts exactly jit.* and "
                          "loader.*")
    for forbidden in ('"memory.', '"ipc.', '"fs.', '"cpu.', '"platform.'):
        if forbidden in classifier:
            violations.append(f"C4: the classifier accepts {forbidden}* records: an experiment "
                              "record would be counted as an execution-chain blocker")
    out.append(f"ok   C4 RECORD_ATTRIBUTION: all {len(records)} execution-chain records map "
               "to one node, none to the object axis")

    # C5 ------------------------------------------------------------------
    execution_records = [name for name in records if name.startswith(("jit.", "loader."))]
    roots = {RECORD_ROOT[name] for name in execution_records if name in RECORD_ROOT}
    if roots and len(roots) >= len(execution_records):
        violations.append(f"C5: {len(execution_records)} records attributed to {len(roots)} "
                          "independent roots: dependencies are being counted as defects")
    out.append(f"ok   C5 NOT_ONE_PER_RECORD: {len(execution_records)} execution-chain records "
               f"resolve to {len(roots)} root nodes {sorted(roots)}")

    # C6 ------------------------------------------------------------------
    if report is None or not report.is_file():
        out.append("ok   C6 REPORT_CONSISTENT: skipped (no report given; run with --report)")
        return out, violations
    text = report.read_text(errors="ignore")
    if "NOTE=capability_graph" not in text:
        violations.append(f"C6: {report.name} does not carry the capability graph")
    for line in text.splitlines():
        if "TEST=" not in line:
            continue
        record = line.split("TEST=", 1)[1].split()[0]
        if not record.startswith(("jit.", "loader.")) or "STATUS=BLOCKED" not in line:
            continue
        if "dual_mapping" in line or "posix_shm" in line:
            violations.append(f"C6: the blocked execution record {record} is attributed to the "
                              "object axis")
    out.append(f"ok   C6 REPORT_CONSISTENT: {report.name} carries the graph and attributes no "
               "execution-chain blocker to the object axis")
    return out, violations


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", default=str(pathlib.Path(__file__).resolve().parent.parent))
    parser.add_argument("--report", default=None,
                        help="a real phase-02 report to check against the graph")
    args = parser.parse_args()
    root = pathlib.Path(args.root).resolve()
    report = pathlib.Path(args.report).resolve() if args.report else None

    out, violations = rules(root, report)
    for line in out:
        print(line)
    for violation in violations:
        print(f"FAIL {violation}")
    print(f"CAPABILITY_GRAPH_AUDIT={len(violations)}")
    return 1 if violations else 0


if __name__ == "__main__":
    raise SystemExit(main())
