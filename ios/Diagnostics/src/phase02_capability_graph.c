/*
 * phase02_capability_graph.c — the graph, as data (see the header for the picture).
 *
 * PHASE_02_RECONSTRUCTED_POC (pass 05).
 *
 * Every edge here is a statement the code already makes; the point of writing them down in
 * one place is that a report can be checked against them, and that a new dependency cannot
 * sneak into the code without appearing in the graph (tools/audit_capability_graph.py).
 */
#include "phase02_capability_graph.h"

#include <stdio.h>
#include <string.h>

static const phase02_capability_edge_t g_edges[] = {
    /* --- execution axis: signing -> MAP_JIT -> executable arena -> execution ---------- */
    { PHASE02_NODE_SIGNING, PHASE02_NODE_MAP_JIT, PHASE02_EDGE_GATES,
      "MAP_JIT requires the dynamic-codesigning entitlement, which on iOS-based platforms "
      "must be allowlisted by the provisioning profile; physical run 02 measured the "
      "refusal (errno=1) for this signing context" },
    { PHASE02_NODE_MAP_JIT, PHASE02_NODE_EXECMEM_ABI, PHASE02_EDGE_SUPPLIES,
      "a MAP_JIT region is the platform's own executable-memory backend for the "
      "abstraction; without it the single-view W^X anonymous backend is used instead" },
    { PHASE02_NODE_SIGNING, PHASE02_NODE_EFFECTIVE_EXECUTE, PHASE02_EDGE_GATES,
      "on iOS 26 / TXM-SPTM hardware the execute permission of a page whose protection the "
      "process changed is removed out of band; the request may still be accepted" },
    { PHASE02_NODE_EXECMEM_ABI, PHASE02_NODE_EFFECTIVE_EXECUTE, PHASE02_EDGE_REQUIRED,
      "the abstraction requests the executable state and reports what the kernel says "
      "afterwards (rt_mem_protection_observed); acceptance alone is never treated as "
      "executability" },
    { PHASE02_NODE_EFFECTIVE_EXECUTE, PHASE02_NODE_JIT, PHASE02_EDGE_REQUIRED,
      "the JIT chain enters code it wrote: without effective execute permission there is "
      "nothing to enter, and the attempt is what terminated physical run 03" },
    { PHASE02_NODE_EFFECTIVE_EXECUTE, PHASE02_NODE_LOADER, PHASE02_EDGE_REQUIRED,
      "the loader enters the mapped entry point through the same abstraction, so it "
      "inherits exactly this dependency - one mechanism, two consumers (pass 05 merged the "
      "protocol; before that the loader had its own copy)" },
    /* --- object-mapping axis: sandbox -> named POSIX shm / file-backed object ---------- */
    { PHASE02_NODE_NAMED_SHM, PHASE02_NODE_DUAL_MAPPING, PHASE02_EDGE_SUPPLIES,
      "the named POSIX namespace is the original backend of the experiment; the sandbox "
      "refuses it (physical run 02: shm_open, errno=1)" },
    { PHASE02_NODE_SIGNING, PHASE02_NODE_NAMED_SHM, PHASE02_EDGE_REFUSES,
      "the refusal is a property of the sandbox/namespace, not of shared memory: anonymous "
      "MAP_SHARED, container file-backed objects and mach VM are unaffected" },
    { PHASE02_NODE_DUAL_MAPPING, PHASE02_NODE_EFFECTIVE_EXECUTE, PHASE02_EDGE_SUPPLIES,
      "an executable second view would be another way to obtain executable memory; the "
      "experiment measures whether it exists, and the runtime never depends on it (the "
      "interface audit rule keeps the execution paths out of the dual-mapping module)" }
};

const char *phase02_capability_node_name(phase02_capability_node_t node)
{
    switch (node) {
    case PHASE02_NODE_SIGNING:           return "signing-capability";
    case PHASE02_NODE_MAP_JIT:           return "map-jit";
    case PHASE02_NODE_EXECMEM_ABI:       return "execmem-abstraction";
    case PHASE02_NODE_EFFECTIVE_EXECUTE: return "effective-execute";
    case PHASE02_NODE_JIT:               return "jit-chain";
    case PHASE02_NODE_LOADER:            return "loader";
    case PHASE02_NODE_NAMED_SHM:         return "named-posix-shm";
    case PHASE02_NODE_DUAL_MAPPING:      return "dual-mapping-experiment";
    case PHASE02_NODE_COUNT:             break;
    }
    return "unknown";
}

const char *phase02_capability_relation_name(phase02_capability_relation_t relation)
{
    switch (relation) {
    case PHASE02_EDGE_GATES:    return "gates";
    case PHASE02_EDGE_REQUIRED: return "required";
    case PHASE02_EDGE_SUPPLIES: return "supplies";
    case PHASE02_EDGE_REFUSES:  return "refuses";
    }
    return "unknown";
}

unsigned phase02_capability_edges(phase02_capability_edge_t *out, unsigned capacity)
{
    unsigned count = (unsigned)(sizeof(g_edges) / sizeof(g_edges[0]));
    unsigned index;

    if (out == NULL) {
        return count;
    }
    for (index = 0u; index < count && index < capacity; index++) {
        out[index] = g_edges[index];
    }
    return count;
}

int phase02_capability_record_is_execution_chain(const char *record)
{
    if (record == NULL) {
        return 0;
    }
    return (strncmp(record, "jit.", 4u) == 0 || strncmp(record, "loader.", 7u) == 0) ? 1 : 0;
}

int phase02_capability_graph_summary(char *out, unsigned long capacity)
{
    phase02_capability_edge_t edges[16];
    unsigned count;
    unsigned index;
    unsigned other;
    unsigned roots = 0u;
    unsigned dependents = 0u;
    unsigned map_jit_in_edges = 0u;
    unsigned execute_in_edges = 0u;
    int written;

    if (out == NULL || capacity == 0u) {
        return -1;
    }
    count = phase02_capability_edges(edges, 16u);
    for (index = 0u; index < count && index < 16u; index++) {
        if (edges[index].from == PHASE02_NODE_SIGNING) {
            roots++;
        }
        if (edges[index].to == PHASE02_NODE_MAP_JIT) {
            map_jit_in_edges++;
        }
        if (edges[index].to == PHASE02_NODE_EFFECTIVE_EXECUTE) {
            execute_in_edges++;
        }
    }
    /* Dependent nodes are the execution consumers that hang off ONE permission: they are
     * counted HERE, from the data, and never written down. Two properties follow, and both
     * matter for how a blocked record is read: a new consumer shows up the moment it appears
     * in the graph, and a duplicated edge cannot inflate the count - a repeated dependency is
     * still one dependency, which is exactly the mistake (nine records read as nine causes)
     * this graph exists to prevent. */
    for (index = 0u; index < count && index < 16u; index++) {
        if (edges[index].from != PHASE02_NODE_EFFECTIVE_EXECUTE ||
            edges[index].relation != PHASE02_EDGE_REQUIRED) {
            continue;
        }
        for (other = 0u; other < index; other++) {
            if (edges[other].from == PHASE02_NODE_EFFECTIVE_EXECUTE &&
                edges[other].relation == PHASE02_EDGE_REQUIRED &&
                edges[other].to == edges[index].to) {
                break;
            }
        }
        if (other == index) {
            dependents++;
        }
    }

    written = snprintf(out, (size_t)capacity,
                       "nodes=%u edges=%u signing_edges=%u map_jit_in_edges=%u "
                       "effective_execute_in_edges=%u execution_consumers=%u",
                       (unsigned)PHASE02_NODE_COUNT, count, roots, map_jit_in_edges,
                       execute_in_edges, dependents);
    if (written < 0 || (unsigned long)written >= capacity) {
        out[0] = '\0';
        return -1;
    }
    return written;
}
