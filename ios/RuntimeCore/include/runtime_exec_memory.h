/*
 * runtime_exec_memory.h — the single executable-memory abstraction.
 *
 * PHASE_02_RECONSTRUCTED_POC (pass 05).
 *
 * Why this file exists. Before pass 05 the same protocol was written out three times:
 * in rt_jit_execution_allowed(), in the JIT microtest of the harness and in the loader
 * (runtime_loader.c). Three copies of "how do I write into memory that must become
 * executable, and how do I flip it" is three places for the rule to drift — and the
 * physical runs showed how expensive that is. The decision of *which* mechanism is used
 * is now made once, here, from one capability model:
 *
 *     can allocate  ->  can write  ->  can transition  ->  can execute  ->  can rewrite
 *
 * Consumers: the JIT chain, the loader and (through the same arena calls) any future
 * execution path. The dual-mapping experiment deliberately does NOT use this file: it
 * creates two views of one object and never executes either (see runtime_dual_mapping.c),
 * which is what keeps "two views work" and "dynamic execution works" measurable as two
 * different properties.
 *
 * What this file does NOT decide: whether the caller may attempt execution at all. That is
 * the execution policy (Diagnostics/phase02_execution_policy), a target/instrumentation
 * decision; here live the platform facts — which backends exist, which one is planned, and
 * what the requested transition actually produced.
 *
 * Layering: RuntimeCore never includes Diagnostics headers (interface audit rule); the
 * policy asks THIS module for facts, not the other way round.
 */
#ifndef RUNTIME_EXEC_MEMORY_H
#define RUNTIME_EXEC_MEMORY_H

#include <stddef.h>
#include <stdint.h>

#include "runtime_jit.h"
#include "runtime_platform.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The mechanisms that can produce memory this process may later execute. */
typedef enum {
    RT_EXEC_BACKEND_NONE = 0,        /* no mechanism on this platform/target */
    RT_EXEC_BACKEND_ANON_WX = 1,     /* anonymous mapping, single view flipped RW <-> R-X */
    RT_EXEC_BACKEND_MAP_JIT = 2,     /* MAP_JIT region; write window is the write protocol */
    RT_EXEC_BACKEND_COUNT = 3
} rt_exec_backend_t;

/* Why a backend is or is not in the plan. `supported` is a MEASURED platform capability,
 * never an assumption: MAP_JIT is only reported supported when this process can actually
 * obtain such a mapping. */
typedef struct rt_exec_backend_status {
    rt_exec_backend_t backend;
    int               supported;      /* 1 supported, 0 not */
    int               planned;        /* 1 when this is the backend the plan prefers */
    int               errno_value;    /* real errno of the probe/attempt, 0 when supported */
    const char       *mechanism;      /* what the mechanism is, in words */
} rt_exec_backend_status_t;

/* The plan for the current target, in the order the allocator will try. Length in
 * *count_out (never more than RT_EXEC_BACKEND_COUNT). */
const rt_exec_backend_t *rt_exec_backend_plan(int *count_out);

/* Name of a backend ("anon-wx", "map-jit", "none"). */
const char *rt_exec_backend_name(rt_exec_backend_t backend);

/* Index of a backend inside the plan order (0 = first choice), -1 when not in the plan. */
int rt_exec_backend_rank(rt_exec_backend_t backend);

/* Measures the backends available to this process (MAP_JIT is probed, not assumed) and
 * fills `out_status` with RT_EXEC_BACKEND_COUNT entries. Returns the number of supported
 * backends. */
int rt_exec_backends_measured(rt_exec_backend_status_t out_status[RT_EXEC_BACKEND_COUNT]);

/* An allocated executable-capable arena. The layout is intentionally inert data: no
 * global mutable state, no hidden handle, so an arena can live on a stack. */
typedef struct rt_exec_arena {
    void               *base;
    size_t              len;
    rt_exec_backend_t   backend;
    rt_jit_arena_kind_t arena_kind;          /* mechanism reported by the allocator */
    int                 map_jit_attempted;
    int                 map_jit_refused_errno;
    int                 executable;          /* 1 while the arena is in the executable state */
} rt_exec_arena_t;

/* Allocates through the plan (or a preferred backend when preferred != RT_EXEC_BACKEND_NONE;
 * a preference that is not supported falls back to the plan and says so through
 * *used_out != preferred). *used_out receives the backend that produced the arena. */
int rt_exec_arena_allocate(rt_exec_arena_t *arena_out, size_t len,
                           rt_exec_backend_t preferred, rt_exec_backend_t *used_out,
                           int *err_out);

/* Writes `len` bytes through the backend's own protocol (write window for MAP_JIT, an
 * explicit writable state for the anonymous backend). `len` must fit the arena. */
int rt_exec_arena_write(rt_exec_arena_t *arena, size_t offset, const void *bytes, size_t len,
                        int *err_out);

/* Requests the executable state. On success the transition was ACCEPTED — which is not yet
 * proof that this process may execute the memory (see rt_exec_arena_observed). */
int rt_exec_arena_make_executable(rt_exec_arena_t *arena, int *err_out);

/* Requests the writable state (needed before a rewrite of an executable arena). */
int rt_exec_arena_make_writable(rt_exec_arena_t *arena, int *err_out);

/* Instruction-cache synchronisation for the arena (or part of it). */
int rt_exec_arena_sync_icache(rt_exec_arena_t *arena, size_t len, int *err_out);

/* Reads back what the kernel reports about the arena's protection (bit mask of
 * rt_mem_observed_t). Returns 0 with *bits_out set, or -1 with errno (ENOTSUP when this
 * platform cannot answer) — in which case the caller must record "not observable" rather
 * than a guess. */
int rt_exec_arena_observe(const rt_exec_arena_t *arena, unsigned *bits_out, int *err_out);

/* Calls the arena's entry point through the fault guard (a fault is caught and reported, it
 * is not resolved by pretending the call returned).
 * Returns 0 when the call returned (*value_out set, *fault_addr_out NULL),
 *        -1 when a fault was caught (*fault_addr_out = si_addr),
 *        -2 when the guard could not be installed (errno in *err_out). */
int rt_exec_arena_call(rt_exec_arena_t *arena, size_t entry_offset, uint32_t *value_out,
                       void **fault_addr_out, int *err_out);

/* Frees an arena. Safe on a zeroed arena; clears the struct so a double destroy is a no-op. */
int rt_exec_arena_destroy(rt_exec_arena_t *arena);

/* The capability model: everything the callers may rely on, in one place. `can_execute` and
 * `can_rewrite` are the platform's part of the answer only — whether the caller is allowed
 * to attempt anything is the execution policy's decision, deliberately kept elsewhere. */
typedef struct rt_exec_capabilities {
    rt_exec_backend_t planned_backend;
    int               backends_supported;
    int               map_jit_supported;
    int               can_allocate;
    int               can_write;
    int               can_transition;
    int               can_execute;      /* platform part only (see the note above) */
    int               can_rewrite;
    int               execution_observed;  /* 1 only after a call really returned a value */
} rt_exec_capabilities_t;

int rt_exec_capabilities_current(rt_exec_capabilities_t *out);

#ifdef __cplusplus
}
#endif

#endif /* RUNTIME_EXEC_MEMORY_H */
