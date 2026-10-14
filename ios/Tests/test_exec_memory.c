/*
 * test_exec_memory.c — the executable-memory abstraction, the backend plan and the
 * observed-protection query.
 *
 * PHASE_02_RECONSTRUCTED_POC (pass 05).
 *
 * This is the unit level of the pass-05 work: one protocol, one plan, one capability model.
 * Everything here is behaviour, not constants:
 *
 *   - the plan names a backend per target class and the rank is consistent with it;
 *   - the measured backends say what this process really has (MAP_JIT is probed);
 *   - an arena written through the abstraction and made executable through it really runs the
 *     payload — 42 — and can be reopened for a rewrite (4242), which is the loader/JIT path;
 *   - the same calls refuse what they must refuse (out-of-range write, call before the
 *     executable state, zeroed arena) instead of pretending;
 *   - the observed-protection query distinguishes "the request was accepted" from "the
 *     mapping has execute permission", which is the question physical run 03 left open;
 *   - the read-only half of the dual-mapping experiment aliases two views without ever asking
 *     for an executable one.
 *
 * Nothing here claims an Apple result: on this host the execute permission is granted, and the
 * test says so.
 */
#include "runtime_exec_memory.h"
#include "phase02_capability_graph.h"
#include "runtime_jit.h"
#include "runtime_memory.h"
#include "runtime_platform.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

static void describe_backends(void)
{
    rt_exec_backend_status_t status[RT_EXEC_BACKEND_COUNT];
    int index;
    int supported = rt_exec_backends_measured(status);
    int count = 0;
    const rt_exec_backend_t *plan = rt_exec_backend_plan(&count);

    printf("# backends measured on %s: supported=%d\n", rt_platform_name(), supported);
    for (index = 0; index < RT_EXEC_BACKEND_COUNT; index++) {
        if (status[index].backend == RT_EXEC_BACKEND_NONE) {
            continue;
        }
        printf("#   %-8s supported=%d planned=%d errno=%d (%s)\n",
               rt_exec_backend_name(status[index].backend), status[index].supported,
               status[index].planned, status[index].errno_value,
               status[index].mechanism != NULL ? status[index].mechanism : "");
    }
    for (index = 0; index < count; index++) {
        printf("#   plan[%d]=%s\n", index, rt_exec_backend_name(plan[index]));
    }
}

static void test_plan_and_names(void)
{
    int count = 0;
    const rt_exec_backend_t *plan = rt_exec_backend_plan(&count);
    rt_exec_backend_status_t status[RT_EXEC_BACKEND_COUNT];
    int supported;

    CHECK(plan != NULL && count >= 1, "the plan is never empty");
    CHECK(plan[0] == RT_EXEC_BACKEND_MAP_JIT || plan[0] == RT_EXEC_BACKEND_ANON_WX,
          "the plan prefers one of the two real mechanisms");
    CHECK(strcmp(rt_exec_backend_name(plan[0]), "none") != 0,
          "the first choice has a real name");
    CHECK(rt_exec_backend_rank(plan[0]) == 0, "rank(plan[0]) == 0");
    if (count > 1) {
        CHECK(rt_exec_backend_rank(plan[1]) == 1, "rank(plan[1]) == 1");
    }
    CHECK(rt_exec_backend_rank(RT_EXEC_BACKEND_NONE) == -1,
          "the empty backend is not in the plan");
    CHECK(strcmp(rt_exec_backend_name(RT_EXEC_BACKEND_NONE), "none") == 0,
          "the empty backend is named 'none'");

    supported = rt_exec_backends_measured(status);
    CHECK(supported >= 1, "at least one backend exists on a host that can run these tests");
    CHECK(status[RT_EXEC_BACKEND_ANON_WX].supported == 1,
          "the anonymous W^X backend is supported where mmap/anonymous is");
    CHECK(status[RT_EXEC_BACKEND_NONE].supported == 0,
          "RT_EXEC_BACKEND_NONE is never reported supported");
    /* MAP_JIT is measured, not assumed: on this host it must be reported unsupported with a
     * real errno (ENOTSUP where the platform has no such mapping), and where it IS supported
     * the errno must be 0. Either way the field is consistent. */
    if (status[RT_EXEC_BACKEND_MAP_JIT].supported != 0) {
        CHECK(status[RT_EXEC_BACKEND_MAP_JIT].errno_value == 0,
              "a supported MAP_JIT reports no refusal errno");
    } else {
        CHECK(status[RT_EXEC_BACKEND_MAP_JIT].errno_value != 0,
              "an unsupported MAP_JIT carries the real refusal errno");
    }
    describe_backends();
}

static void test_capability_model(void)
{
    rt_exec_capabilities_t caps;
    int rc = rt_exec_capabilities_current(&caps);

    CHECK(rc == 0, "the capability model answers");
    CHECK(caps.backends_supported >= 1, "at least one executable-memory backend on this host");
    CHECK(caps.planned_backend == rt_exec_backend_plan(NULL)[0] ||
              rt_exec_backend_rank(caps.planned_backend) == 0,
          "the model reports the plan's first choice as the planned backend");
    CHECK(caps.can_allocate == 1, "this host can allocate an executable-capable arena");
    CHECK(caps.can_write == 1, "this host can write into it");
    CHECK(caps.can_transition == 1, "this host can request the executable state");
    CHECK(caps.can_rewrite == 1, "this host can reopen the arena for a rewrite");
    /* can_execute is the platform part of the answer only: -1 means "not determined here",
     * which is the honest value when MAP_JIT is unavailable and the execute permission is
     * obtained out of band on the real target. */
    CHECK(caps.can_execute == 1 || caps.can_execute == -1,
          "can_execute is either 'platform allows' or 'not determined', never a guess");
    CHECK(caps.execution_observed == 0,
          "the model does not claim an observation that has not happened");
    printf("# capabilities: planned=%s map_jit=%d can_execute=%d\n",
           rt_exec_backend_name(caps.planned_backend), caps.map_jit_supported,
           caps.can_execute);
}

static void test_arena_roundtrip(void)
{
    rt_exec_arena_t arena;
    rt_exec_backend_t used = RT_EXEC_BACKEND_NONE;
    uint8_t payload[16];
    size_t payload_len = 0u;
    unsigned observed = 0u;
    void *fault = NULL;
    uint32_t value = 0u;
    int err = 0;

    CHECK(rt_jit_emit_return_imm(payload, sizeof(payload), 42u, &payload_len) == 0 &&
              payload_len > 0u,
          "the emitter produced a payload");
    CHECK(rt_exec_arena_allocate(&arena, payload_len, RT_EXEC_BACKEND_NONE, &used, &err) == 0,
          "the abstraction allocated an arena");
    CHECK(arena.base != NULL && arena.len == payload_len, "the arena is usable");
    CHECK(used == arena.backend, "the reported backend is the arena's backend");
    printf("# arena backend=%s kind=%d map_jit_attempted=%d\n",
           rt_exec_backend_name(arena.backend), (int)arena.arena_kind,
           arena.map_jit_attempted);

    /* Before anything is written the mapping is data, not code. */
    CHECK(rt_exec_arena_observe(&arena, &observed, &err) == 0 || err == ENOTSUP,
          "the protection of a fresh arena is either observed or reported unobservable");
    if (err == 0) {
        CHECK((observed & RT_MEM_OBSERVED_WRITE) != 0u,
              "a fresh arena is writable");
        CHECK((observed & RT_MEM_OBSERVED_EXEC) == 0u,
              "a fresh arena is not executable (no W+X mapping is ever created)");
    }

    /* Calling before the executable state is a programming error, refused, not attempted. */
    err = 0;
    CHECK(rt_exec_arena_call(&arena, 0u, &value, &fault, &err) == -1 && err == EINVAL,
          "calling an arena that is not executable is refused with EINVAL");

    CHECK(rt_exec_arena_write(&arena, 0u, payload, payload_len, &err) == 0,
          "the payload was written through the backend's own protocol");
    CHECK(rt_exec_arena_sync_icache(&arena, payload_len, &err) == 0,
          "the instruction cache was synchronised");
    CHECK(rt_exec_arena_make_executable(&arena, &err) == 0,
          "the executable state was requested");

    err = 0;
    if (rt_exec_arena_observe(&arena, &observed, &err) == 0) {
        CHECK((observed & RT_MEM_OBSERVED_EXEC) != 0u,
              "after the request the mapping really has execute permission on this host");
        CHECK((observed & RT_MEM_OBSERVED_WRITE) == 0u,
              "and it is no longer writable: strict W^X held across the transition");
        printf("# observed protection after R-X request: %s\n", rt_mem_observed_name(observed));
    } else {
        printf("# observed protection not available on this platform (errno=%d)\n", err);
    }

    /* REAL execution: the value has to come back from the CPU. */
    CHECK(rt_exec_arena_call(&arena, 0u, &value, &fault, &err) == 0,
          "the generated code was called and returned");
    CHECK(value == 42u, "the call returned 42 from the emitted payload");
    printf("# executed payload returned %u\n", value);

    /* Rewrite path: writable again, new payload, executable again, real 4242. */
    CHECK(rt_exec_arena_make_writable(&arena, &err) == 0, "the arena was reopened for writing");
    CHECK(rt_jit_emit_return_imm(payload, sizeof(payload), 4242u, &payload_len) == 0,
          "the second payload was emitted");
    CHECK(rt_exec_arena_write(&arena, 0u, payload, payload_len, &err) == 0,
          "the rewrite was written through the same protocol");
    CHECK(rt_exec_arena_sync_icache(&arena, payload_len, &err) == 0,
          "the cache was synchronised after the rewrite");
    CHECK(rt_exec_arena_make_executable(&arena, &err) == 0,
          "the executable state was requested again");
    value = 0u;
    CHECK(rt_exec_arena_call(&arena, 0u, &value, &fault, &err) == 0 && value == 4242u,
          "the rewritten payload returned 4242 from real execution");

    CHECK(rt_exec_arena_destroy(&arena) == 0, "the arena was released");
    CHECK(arena.base == NULL && arena.len == 0u,
          "the arena struct is cleared, so a second destroy is a no-op");
    CHECK(rt_exec_arena_destroy(&arena) == 0, "destroying a cleared arena is harmless");
}

static void test_arena_negatives(void)
{
    rt_exec_arena_t arena;
    rt_exec_backend_t used = RT_EXEC_BACKEND_NONE;
    uint8_t byte = 0u;
    int err = 0;

    memset(&arena, 0, sizeof(arena));
    err = 0;
    CHECK(rt_exec_arena_write(&arena, 0u, &byte, 1u, &err) == -1 && err == EINVAL,
          "writing into a zeroed arena is refused");
    err = 0;
    CHECK(rt_exec_arena_make_executable(&arena, &err) == -1 && err == EINVAL,
          "making a zeroed arena executable is refused");
    err = 0;
    CHECK(rt_exec_arena_allocate(NULL, 8u, RT_EXEC_BACKEND_NONE, NULL, &err) == -1 &&
              err == EINVAL,
          "allocating into a NULL out parameter is refused");
    err = 0;
    CHECK(rt_exec_arena_allocate(&arena, 0u, RT_EXEC_BACKEND_NONE, &used, &err) == -1 &&
              err == EINVAL,
          "allocating zero bytes is refused");

    CHECK(rt_exec_arena_allocate(&arena, 8u, RT_EXEC_BACKEND_NONE, &used, &err) == 0,
          "a small arena for the bounds checks");
    err = 0;
    CHECK(rt_exec_arena_write(&arena, 4u, &byte, 8u, &err) == -1 && err == EINVAL,
          "a write that would cross the end of the arena is refused (no overflow)");
    err = 0;
    CHECK(rt_exec_arena_sync_icache(&arena, arena.len + 1u, &err) == -1 && err == EINVAL,
          "an icache request beyond the arena is refused");
    CHECK(rt_exec_arena_destroy(&arena) == 0, "cleanup of the bounds-check arena");

    /* A preferred backend this process cannot get must be reported, not silently ignored. */
    {
        rt_exec_arena_t asked;
        rt_exec_backend_t got = RT_EXEC_BACKEND_NONE;
        err = 0;
        if (rt_exec_arena_allocate(&asked, 8u, RT_EXEC_BACKEND_MAP_JIT, &got, &err) == 0) {
            CHECK(got == RT_EXEC_BACKEND_MAP_JIT || err == EPROTONOSUPPORT,
                  "a MAP_JIT preference is either honoured or reported as not honoured");
            if (got != RT_EXEC_BACKEND_MAP_JIT) {
                CHECK(err == EPROTONOSUPPORT,
                      "the mismatch between asked and used backend is reported");
            }
            CHECK(rt_exec_arena_destroy(&asked) == 0, "cleanup of the preferred-backend arena");
        } else {
            CHECK(err != 0, "a failed allocation always carries a real errno");
        }
    }
}

static void test_observed_protection_query(void)
{
    size_t page = (size_t)rt_platform_page_size();
    void *region = rt_mem_reserve(page, NULL);
    unsigned bits = 0u;
    int err = 0;

    CHECK(region != NULL, "a page for the protection query");
    if (region == NULL) {
        return;
    }
    if (rt_mem_protection_observed(region, page, &bits, &err) == 0) {
        CHECK((bits & RT_MEM_OBSERVED_READ) != 0u && (bits & RT_MEM_OBSERVED_WRITE) != 0u &&
                  (bits & RT_MEM_OBSERVED_EXEC) == 0u,
              "a fresh RW mapping is observed as RW (no execute)");
        CHECK(rt_mem_protect(region, page, RT_PROT_READ | RT_PROT_EXEC, &err) == 0,
              "the R-X request was accepted");
        bits = 0u;
        CHECK(rt_mem_protection_observed(region, page, &bits, &err) == 0 &&
                  (bits & RT_MEM_OBSERVED_EXEC) != 0u,
              "the observation follows the request on a host that grants execute");
        CHECK(strcmp(rt_mem_observed_name(bits), "R-X") == 0 ||
                  strcmp(rt_mem_observed_name(bits), "RWX") == 0,
              "the observed bits render as a protection string");
    } else {
        CHECK(err == ENOTSUP,
              "an unobservable platform reports ENOTSUP rather than inventing a value");
        printf("# observed protection: not available here (errno=%d)\n", err);
    }
    /* negatives: the query refuses nonsense instead of answering */
    err = 0;
    CHECK(rt_mem_protection_observed(NULL, page, &bits, &err) == -1 && err == EINVAL,
          "a NULL range is refused");
    err = 0;
    CHECK(rt_mem_protection_observed(region, 0u, &bits, &err) == -1 && err == EINVAL,
          "a zero length is refused");
    err = 0;
    CHECK(rt_mem_protection_observed(region, page, NULL, &err) == -1 && err == EINVAL,
          "a NULL out parameter is refused");
    CHECK(strcmp(rt_mem_observed_name(0u), "-") == 0, "no bits render as '-'");
    (void)rt_mem_release(region, page, NULL);
}

/* The read-only half of the dual-mapping experiment: two live views of one app-owned object,
 * neither of them executable. This is the measurement that answers "can this device hold two
 * views of one object?" without touching the question that carries the entry risk. */
static void test_dual_mapping_readonly_views(void)
{
    char dir[] = "/tmp/phase02-dual-XXXXXX";
    rt_dual_map_t map;
    int rc;

    if (mkdtemp(dir) == NULL) {
        CHECK(0, "a work directory for the dual-mapping test");
        return;
    }
    rc = rt_dual_map_create_file_backed_readonly_view(dir, (size_t)rt_platform_page_size(), &map);
    CHECK(rc == 0, "the read-only two-view mapping was created");
    if (rc == 0) {
        CHECK(rt_dual_map_views_aliased(&map) == 1,
              "the read-write and read-only views alias the same memory");
        /* Write through the RW view, read through the R view: the proof is the value, not the
         * return code. */
        memset(map.rw, 0x5a, 64u);
        CHECK(((const unsigned char *)map.rx)[0] == 0x5au,
              "a byte written through the RW view is visible through the read-only view");
        CHECK(rt_dual_map_destroy(&map) == 0, "the read-only mapping was destroyed");
    }
    /* negatives */
    rc = rt_dual_map_create_file_backed_readonly_view(NULL, (size_t)rt_platform_page_size(), &map);
    CHECK(rc == -1 && map.err == EINVAL,
          "no directory is refused with EINVAL instead of guessing one");
    (void)rmdir(dir);
}

/* ---------------------------------------------------------------- the capability graph
 *
 * Nine BLOCKED records are not nine problems. The graph is the data that says so, and these
 * checks are what makes the data load-bearing: they recompute every count from the edge list
 * (never from the summary's own words) and reject the two shapes that would inflate the
 * blocker count - a duplicated dependency, and an execution path hanging off the object
 * (dual-mapping / named-SHM) experiment.
 */
static void test_capability_graph_structure(void)
{
    phase02_capability_edge_t edges[32];
    unsigned count = phase02_capability_edges(edges, 32u);
    unsigned index;
    unsigned other;
    unsigned roots = 0u;
    unsigned map_jit_in = 0u;
    unsigned execute_in = 0u;
    unsigned consumers = 0u;
    char summary[192];
    int node;

    CHECK(count >= 6u && count <= 32u, "the graph has a usable number of edges");
    CHECK(phase02_capability_edges(NULL, 0u) == count,
          "asking for the edge count alone reports the same number");

    for (index = 0u; index < count; index++) {
        const phase02_capability_edge_t *edge = &edges[index];

        CHECK(edge->from < PHASE02_NODE_COUNT && edge->to < PHASE02_NODE_COUNT,
              "every edge endpoint is a known node");
        CHECK(edge->from != edge->to, "no node depends on itself");
        CHECK(edge->relation <= PHASE02_EDGE_REFUSES, "every edge has a known relation");
        CHECK(edge->note != NULL && edge->note[0] != '\0', "every edge states its reason");

        /* A duplicated (from, to, relation) triple would make one dependency look like two. */
        for (other = 0u; other < index; other++) {
            CHECK(!(edges[other].from == edge->from && edges[other].to == edge->to &&
                        edges[other].relation == edge->relation),
                  "no dependency is listed twice");
        }

        /* The object-mapping axis is an experiment: it may OFFER a mechanism (supplies) but
         * nothing in the execution chain may depend on it, and no dependency may be sourced
         * from it. */
        if (edge->from == PHASE02_NODE_EXECMEM_ABI || edge->from == PHASE02_NODE_JIT ||
            edge->from == PHASE02_NODE_LOADER) {
            CHECK(edge->to != PHASE02_NODE_NAMED_SHM && edge->to != PHASE02_NODE_DUAL_MAPPING,
                  "no execution path depends on the object-mapping experiment");
        }
        if (edge->from == PHASE02_NODE_NAMED_SHM || edge->from == PHASE02_NODE_DUAL_MAPPING) {
            CHECK(edge->relation == PHASE02_EDGE_SUPPLIES || edge->relation == PHASE02_EDGE_REFUSES,
                  "the object axis only supplies options or is refused (never a hard "
                  "dependency of the runtime)");
        }

        if (edge->from == PHASE02_NODE_SIGNING) {
            roots++;
        }
        if (edge->to == PHASE02_NODE_MAP_JIT) {
            map_jit_in++;
        }
        if (edge->to == PHASE02_NODE_EFFECTIVE_EXECUTE) {
            execute_in++;
        }
        if (edge->from == PHASE02_NODE_EFFECTIVE_EXECUTE &&
            edge->relation == PHASE02_EDGE_REQUIRED) {
            unsigned earlier;
            for (earlier = 0u; earlier < index; earlier++) {
                if (edges[earlier].from == PHASE02_NODE_EFFECTIVE_EXECUTE &&
                    edges[earlier].relation == PHASE02_EDGE_REQUIRED &&
                    edges[earlier].to == edge->to) {
                    break;
                }
            }
            if (earlier == index) {
                consumers++;
            }
        }
    }

    for (node = 0; node < (int)PHASE02_NODE_COUNT; node++) {
        const char *name = phase02_capability_node_name((phase02_capability_node_t)node);
        CHECK(name != NULL && strcmp(name, "unknown") != 0 && name[0] != '\0',
              "every node has a name (the report is read by people)");
    }
    CHECK(strcmp(phase02_capability_relation_name(PHASE02_EDGE_GATES), "gates") == 0 &&
              strcmp(phase02_capability_relation_name(PHASE02_EDGE_REQUIRED), "required") == 0 &&
              strcmp(phase02_capability_relation_name(PHASE02_EDGE_SUPPLIES), "supplies") == 0 &&
              strcmp(phase02_capability_relation_name(PHASE02_EDGE_REFUSES), "refuses") == 0,
          "the four relations are named");

    CHECK(phase02_capability_graph_summary(summary, sizeof(summary)) > 0,
          "the graph renders a summary");
    {
        unsigned summary_nodes = 0u;
        unsigned summary_edges = 0u;
        unsigned summary_roots = 0u;
        unsigned summary_map_jit = 0u;
        unsigned summary_execute = 0u;
        unsigned summary_consumers = 0u;

        CHECK(sscanf(summary, "nodes=%u edges=%u signing_edges=%u map_jit_in_edges=%u "
                              "effective_execute_in_edges=%u execution_consumers=%u",
                     &summary_nodes, &summary_edges, &summary_roots, &summary_map_jit,
                     &summary_execute, &summary_consumers) == 6,
              "the summary carries all six counts");
        CHECK(summary_nodes == (unsigned)PHASE02_NODE_COUNT,
              "the summary reports the real node count, not a written-down one");
        CHECK(summary_edges == count, "the summary reports the real edge count");
        CHECK(summary_roots == roots && summary_map_jit == map_jit_in &&
                  summary_execute == execute_in,
              "every reported count equals the count recomputed from the edge list");
        CHECK(summary_consumers == consumers && consumers >= 1u,
              "the dependent consumers are derived from the data (a duplicated edge cannot "
              "inflate them)");
        printf("# capability graph: %s (recomputed consumers=%u)\n", summary, consumers);
    }
    CHECK(phase02_capability_graph_summary(summary, 8u) == -1,
          "a summary that does not fit is refused instead of truncated");

    CHECK(phase02_capability_record_is_execution_chain("jit.execute_return_42") == 1 &&
              phase02_capability_record_is_execution_chain("loader.run_valid_module") == 1,
          "the execution-chain records are recognised");
    CHECK(phase02_capability_record_is_execution_chain("memory.dual_mapping_views") == 0 &&
              phase02_capability_record_is_execution_chain("ipc.posix_shm") == 0 &&
              phase02_capability_record_is_execution_chain(NULL) == 0,
          "the object-mapping records are not execution-chain records (a blocked experiment "
          "is not a blocked runtime)");
}

int main(void)
{
    printf("# executable-memory abstraction tests on %s\n", rt_platform_name());
    test_plan_and_names();
    test_capability_model();
    test_capability_graph_structure();
    test_arena_roundtrip();
    test_arena_negatives();
    test_observed_protection_query();
    test_dual_mapping_readonly_views();
    printf("== %u checks, %u failures ==\n", g_checks, g_failures);
    return (g_failures == 0u) ? 0 : 1;
}
