/*
 * phase02_capability_graph.h — what depends on what, as data.
 *
 * PHASE_02_RECONSTRUCTED_POC (pass 05).
 *
 * Physical run 03/04 left nine BLOCKED records. Treating them as nine problems would be
 * wrong: they are four causes, and three of those causes are the SAME missing signing
 * capability seen from three angles. That distinction has to be checkable, not told: the
 * graph below is data, the harness publishes it in every report, and
 * tools/audit_capability_graph.py holds it against the code and against a real report.
 *
 * The one edge that matters most:
 *
 *     signing capability ──gates──▶ MAP_JIT ──satisfies──▶ executable arena
 *              │                                                    ▲
 *              └──gates──▶ effective execute permission ──required───┘
 *                                     │
 *                       ┌─────────────┴─────────────┐
 *                    jit chain                  loader
 *
 * and, on a separate axis (memory objects, never execution):
 *
 *     app sandbox ──refuses──▶ named POSIX shm ──supplies──▶ dual mapping (experiment)
 *          └──permits──▶ file-backed MAP_SHARED ──supplies──▶ dual mapping (iOS backend)
 *
 * "required" edges are architectural; "supplies" edges are alternatives: a refusal on a
 * supplies-edge is a removed option for an experiment, never a defect of the runtime.
 */
#ifndef PHASE02_CAPABILITY_GRAPH_H
#define PHASE02_CAPABILITY_GRAPH_H

#ifdef __cplusplus
extern "C" {
#endif

/* Nodes. The first five are the execution axis; the last three are the object-mapping
 * axis, deliberately kept out of the execution graph (the interface audit forbids the
 * execution paths from depending on the dual-mapping experiment). */
typedef enum {
    PHASE02_NODE_SIGNING = 0,
    PHASE02_NODE_MAP_JIT = 1,
    PHASE02_NODE_EXECMEM_ABI = 2,        /* runtime_exec_memory: allocate/write/transition/execute */
    PHASE02_NODE_EFFECTIVE_EXECUTE = 3,
    PHASE02_NODE_JIT = 4,
    PHASE02_NODE_LOADER = 5,
    PHASE02_NODE_NAMED_SHM = 6,
    PHASE02_NODE_DUAL_MAPPING = 7,
    PHASE02_NODE_COUNT = 8
} phase02_capability_node_t;

typedef enum {
    PHASE02_EDGE_GATES = 0,        /* the source must be granted for the target to work */
    PHASE02_EDGE_REQUIRED = 1,     /* architectural dependency (no alternative) */
    PHASE02_EDGE_SUPPLIES = 2,     /* the source provides an option for the target */
    PHASE02_EDGE_REFUSES = 3       /* the source is refused by the platform/sandbox */
} phase02_capability_relation_t;

typedef struct phase02_capability_edge {
    phase02_capability_node_t      from;
    phase02_capability_node_t      to;
    phase02_capability_relation_t  relation;
    const char                    *note;
} phase02_capability_edge_t;

const char *phase02_capability_node_name(phase02_capability_node_t node);
const char *phase02_capability_relation_name(phase02_capability_relation_t relation);

/* The graph. Returns the number of edges; `out` receives at most `capacity` of them. */
unsigned phase02_capability_edges(phase02_capability_edge_t *out, unsigned capacity);

/* True when `record` is an execution-chain record of this project (jit.*, loader.*) — the
 * records whose BLOCKED status must be attributable to a node in the graph. Provided as
 * data so the audit and the report agree on the list. */
int phase02_capability_record_is_execution_chain(const char *record);

/* One line summarising the graph for the report, e.g.
 * "nodes=8 edges=9 roots=4 dependents=2 execution_chain_records=13". */
int phase02_capability_graph_summary(char *out, unsigned long capacity);

#ifdef __cplusplus
}
#endif

#endif /* PHASE02_CAPABILITY_GRAPH_H */
