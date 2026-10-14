/*
 * runtime_exec_memory.c — the single executable-memory abstraction (see the header).
 *
 * PHASE_02_RECONSTRUCTED_POC (pass 05).
 *
 * The protocol is stated once, here, and every execution path in the project goes through
 * it. Two mechanisms exist and they are NOT interchangeable:
 *
 *   ANON_WX  — an anonymous mapping. The payload is written while the mapping is writable,
 *              the instruction cache is synchronised, the mapping is flipped to R-X with
 *              rt_mem_protect(); a rewrite flips it back to RW first. There is never a
 *              writable-and-executable page.
 *   MAP_JIT  — the platform's own JIT region. The protections are fixed when the region is
 *              mapped; writing happens INSIDE the write window (pthread_jit_write_protect_np
 *              on macOS) and closing the window is the transition to executable. Calling
 *              mprotect() on such a region is the wrong tool and is exactly the defect
 *              physical run #2 exposed (S-019/S-020).
 *
 * Which one is used is decided by the plan (rt_exec_backend_plan): MAP_JIT first where the
 * process can really obtain it — the platform's supported route — and the single-view W^X
 * anonymous backend everywhere else, which is also the only mechanism available to a
 * third-party iOS app (the entitlement that gates MAP_JIT is not obtainable; physical run #2
 * measured the refusal with errno=1).
 */
#include "runtime_exec_memory.h"

#include <errno.h>
#include <string.h>

#include "runtime_memory.h"
#include "runtime_signals.h"

/* ------------------------------------------------------------------ plan and names */

const char *rt_exec_backend_name(rt_exec_backend_t backend)
{
    switch (backend) {
    case RT_EXEC_BACKEND_ANON_WX:  return "anon-wx";
    case RT_EXEC_BACKEND_MAP_JIT:  return "map-jit";
    case RT_EXEC_BACKEND_NONE:     return "none";
    case RT_EXEC_BACKEND_COUNT:    break;
    }
    return "unknown";
}

const rt_exec_backend_t *rt_exec_backend_plan(int *count_out)
{
    /* The plan is the ORDER OF PREFERENCE, not the order of attempts: an allocator that
     * cannot obtain the first choice still has a defined, documented second choice, and the
     * fallback never hides the first refusal (rt_jit_alloc_ex reports it). */
    static const rt_exec_backend_t native_plan[2] = { RT_EXEC_BACKEND_MAP_JIT,
                                                      RT_EXEC_BACKEND_ANON_WX };
    static const rt_exec_backend_t portable_plan[2] = { RT_EXEC_BACKEND_ANON_WX,
                                                        RT_EXEC_BACKEND_MAP_JIT };
    (void)native_plan;
    (void)portable_plan;

    if (count_out != NULL) {
        *count_out = 2;
    }
#if defined(__APPLE__) && defined(__aarch64__)
    /* Apple ARM64: MAP_JIT is the platform's supported JIT mechanism where it is granted;
     * everywhere else the single-view W^X mapping is the mechanism (this is the iOS case). */
    return native_plan;
#else
    return portable_plan;
#endif
}

int rt_exec_backend_rank(rt_exec_backend_t backend)
{
    int count = 0;
    const rt_exec_backend_t *plan = rt_exec_backend_plan(&count);
    int index;

    for (index = 0; index < count; index++) {
        if (plan[index] == backend) {
            return index;
        }
    }
    return -1;
}

int rt_exec_backends_measured(rt_exec_backend_status_t out_status[RT_EXEC_BACKEND_COUNT])
{
    int count = 0;
    const rt_exec_backend_t *plan;
    int index;
    int supported = 0;
    void *probe_addr = NULL;
    int map_jit_errno = ENOTSUP;
    int map_jit_ok;

    if (out_status == NULL) {
        return -1;
    }
    plan = rt_exec_backend_plan(&count);

    /* MAP_JIT is measured, never assumed: the probe really maps (and unmaps) a page. */
    map_jit_ok = (rt_jit_probe_map_jit(&probe_addr, &map_jit_errno) == 0);

    for (index = 0; index < RT_EXEC_BACKEND_COUNT; index++) {
        out_status[index].backend = (rt_exec_backend_t)index;
        out_status[index].supported = 0;
        out_status[index].planned = 0;
        out_status[index].errno_value = 0;
        out_status[index].mechanism = NULL;
    }

    for (index = 0; index < count; index++) {
        rt_exec_backend_status_t *entry = &out_status[(int)plan[index]];
        entry->planned = (index == 0) ? 1 : 0;
        entry->mechanism = (plan[index] == RT_EXEC_BACKEND_MAP_JIT)
                               ? "MAP_JIT region with the platform write-protect window"
                               : "anonymous mapping flipped RW <-> R-X (strict W^X)";
    }

    /* The anonymous backend exists wherever a private anonymous mapping does. It is measured
     * with the same API the allocator uses, so a refusal here is the real one. */
    {
        void *page = rt_mem_reserve((size_t)rt_platform_page_size(), NULL);
        if (page != NULL) {
            out_status[RT_EXEC_BACKEND_ANON_WX].supported = 1;
            (void)rt_mem_release(page, (size_t)rt_platform_page_size(), NULL);
        } else {
            out_status[RT_EXEC_BACKEND_ANON_WX].supported = 0;
            out_status[RT_EXEC_BACKEND_ANON_WX].errno_value = errno;
        }
    }
    if (map_jit_ok) {
        out_status[RT_EXEC_BACKEND_MAP_JIT].supported = 1;
        out_status[RT_EXEC_BACKEND_MAP_JIT].errno_value = 0;
    } else {
        out_status[RT_EXEC_BACKEND_MAP_JIT].supported = 0;
        out_status[RT_EXEC_BACKEND_MAP_JIT].errno_value = map_jit_errno;
    }
    out_status[RT_EXEC_BACKEND_NONE].supported = 0;
    out_status[RT_EXEC_BACKEND_NONE].mechanism = "no executable-memory mechanism available";

    supported = out_status[RT_EXEC_BACKEND_ANON_WX].supported +
                out_status[RT_EXEC_BACKEND_MAP_JIT].supported;
    return supported;
}

/* ------------------------------------------------------------------------- arenas */

int rt_exec_arena_allocate(rt_exec_arena_t *arena_out, size_t len,
                           rt_exec_backend_t preferred, rt_exec_backend_t *used_out,
                           int *err_out)
{
    rt_jit_arena_kind_t kind = RT_JIT_ARENA_ANON;
    int map_jit_attempted = 0;
    int map_jit_refused = 0;
    rt_exec_backend_t used;
    void *base;

    if (arena_out == NULL || len == 0u) {
        if (err_out != NULL) {
            *err_out = EINVAL;
        }
        return -1;
    }
    memset(arena_out, 0, sizeof(*arena_out));

    /* One allocation path, whichever mechanism is chosen; the allocator reports which of the
     * two it used, and a refused MAP_JIT is surfaced rather than hidden. */
    base = rt_jit_alloc_ex(len, err_out, &kind, &map_jit_attempted, &map_jit_refused);
    if (base == NULL) {
        return -1;
    }
    used = (kind == RT_JIT_ARENA_MAP_JIT) ? RT_EXEC_BACKEND_MAP_JIT : RT_EXEC_BACKEND_ANON_WX;

    if (preferred != RT_EXEC_BACKEND_NONE && preferred != used) {
        /* The caller asked for a mechanism this process did not get. That is reported, not
         * silently accepted: *used_out differs from `preferred`, and the caller decides
         * whether the difference is acceptable for its purpose. */
        if (err_out != NULL) {
            *err_out = EPROTONOSUPPORT;
        }
    } else if (err_out != NULL) {
        *err_out = 0;
    }

    arena_out->base = base;
    arena_out->len = len;
    arena_out->backend = used;
    arena_out->arena_kind = kind;
    arena_out->map_jit_attempted = map_jit_attempted;
    arena_out->map_jit_refused_errno = map_jit_refused;
    arena_out->executable = 0;
    if (used_out != NULL) {
        *used_out = used;
    }
    return 0;
}

int rt_exec_arena_write(rt_exec_arena_t *arena, size_t offset, const void *bytes, size_t len,
                        int *err_out)
{
    if (arena == NULL || arena->base == NULL || bytes == NULL || offset > arena->len ||
        len > arena->len - offset) {
        if (err_out != NULL) {
            *err_out = EINVAL;
        }
        return -1;
    }
    if (err_out != NULL) {
        *err_out = 0;
    }

    if (arena->backend == RT_EXEC_BACKEND_MAP_JIT) {
        /* The write window is the only supported way into a MAP_JIT region. */
        if (rt_jit_begin_write(arena->base, arena->len, err_out) != 0) {
            return -1;
        }
        memcpy((uint8_t *)arena->base + offset, bytes, len);
        if (rt_jit_end_write(arena->base, arena->len, err_out) != 0) {
            return -1;
        }
        arena->executable = 1;   /* closing the window is the transition to executable */
        return 0;
    }

    /* Anonymous backend: writing happens while writable. An arena that was already made
     * executable must be flipped back first — the caller does not have to remember that, and
     * a silent write into a read-only mapping would fault. */
    if (arena->executable != 0) {
        if (rt_mem_protect(arena->base, arena->len, RT_PROT_READ | RT_PROT_WRITE, err_out) != 0) {
            return -1;
        }
        arena->executable = 0;
    }
    memcpy((uint8_t *)arena->base + offset, bytes, len);
    return 0;
}

int rt_exec_arena_make_executable(rt_exec_arena_t *arena, int *err_out)
{
    if (arena == NULL || arena->base == NULL) {
        if (err_out != NULL) {
            *err_out = EINVAL;
        }
        return -1;
    }
    if (err_out != NULL) {
        *err_out = 0;
    }

    if (arena->backend == RT_EXEC_BACKEND_MAP_JIT) {
        /* Protections were fixed at creation; closing the window IS the transition. It was
         * already closed by rt_exec_arena_write(); calling it again is harmless and keeps
         * this state machine total. */
        if (rt_jit_end_write(arena->base, arena->len, err_out) != 0) {
            return -1;
        }
        arena->executable = 1;
        return 0;
    }

    if (rt_mem_protect(arena->base, arena->len, RT_PROT_READ | RT_PROT_EXEC, err_out) != 0) {
        return -1;
    }
    arena->executable = 1;
    return 0;
}

int rt_exec_arena_make_writable(rt_exec_arena_t *arena, int *err_out)
{
    if (arena == NULL || arena->base == NULL) {
        if (err_out != NULL) {
            *err_out = EINVAL;
        }
        return -1;
    }
    if (err_out != NULL) {
        *err_out = 0;
    }

    if (arena->backend == RT_EXEC_BACKEND_MAP_JIT) {
        /* Opening the window is the writable state; execution is unavailable until it closes,
         * so a MAP_JIT region is never readable-writable-executable at the same time. */
        if (rt_jit_begin_write(arena->base, arena->len, err_out) != 0) {
            return -1;
        }
        arena->executable = 0;
        return 0;
    }

    if (rt_mem_protect(arena->base, arena->len, RT_PROT_READ | RT_PROT_WRITE, err_out) != 0) {
        return -1;
    }
    arena->executable = 0;
    return 0;
}

int rt_exec_arena_sync_icache(rt_exec_arena_t *arena, size_t len, int *err_out)
{
    if (arena == NULL || arena->base == NULL || len > arena->len) {
        if (err_out != NULL) {
            *err_out = EINVAL;
        }
        return -1;
    }
    if (err_out != NULL) {
        *err_out = 0;
    }
    return rt_jit_invalidate(arena->base, len);
}

int rt_exec_arena_observe(const rt_exec_arena_t *arena, unsigned *bits_out, int *err_out)
{
    if (arena == NULL || arena->base == NULL || bits_out == NULL) {
        if (err_out != NULL) {
            *err_out = EINVAL;
        }
        return -1;
    }
    return rt_mem_protection_observed(arena->base, arena->len, bits_out, err_out);
}

int rt_exec_arena_call(rt_exec_arena_t *arena, size_t entry_offset, uint32_t *value_out,
                       void **fault_addr_out, int *err_out)
{
    void *target;
    rt_jit_fn_t fn;

    if (arena == NULL || arena->base == NULL || arena->executable == 0 ||
        entry_offset >= arena->len) {
        if (err_out != NULL) {
            *err_out = EINVAL;
        }
        return -1;
    }
    if (err_out != NULL) {
        *err_out = 0;
    }

    /* Object pointer -> function pointer through memcpy: no cast between incompatible
     * function types (same rule the JIT module follows). */
    target = (void *)((uint8_t *)arena->base + entry_offset);
    memcpy(&fn, &target, sizeof(fn));
    return rt_signal_call_guarded(fn, value_out, fault_addr_out, err_out);
}

int rt_exec_arena_destroy(rt_exec_arena_t *arena)
{
    int rc;

    if (arena == NULL || arena->base == NULL) {
        return 0;
    }
    rc = rt_mem_release(arena->base, arena->len, NULL);
    memset(arena, 0, sizeof(*arena));
    return rc;
}

/* ---------------------------------------------------------------- capability model */

int rt_exec_capabilities_current(rt_exec_capabilities_t *out)
{
    rt_exec_backend_status_t status[RT_EXEC_BACKEND_COUNT];
    const rt_exec_backend_t *plan;
    int plan_count = 0;
    int supported;

    if (out == NULL) {
        return -1;
    }
    memset(out, 0, sizeof(*out));
    plan = rt_exec_backend_plan(&plan_count);
    out->planned_backend = (plan_count > 0) ? plan[0] : RT_EXEC_BACKEND_NONE;

    supported = rt_exec_backends_measured(status);
    if (supported < 0) {
        supported = 0;
    }
    out->backends_supported = supported;
    out->map_jit_supported = status[RT_EXEC_BACKEND_MAP_JIT].supported;

    out->can_allocate = (status[RT_EXEC_BACKEND_ANON_WX].supported != 0) ? 1 : 0;
    if (out->map_jit_supported != 0) {
        out->can_allocate = 1;
    }
    /* Writing is the protocol of the planned backend; both protocols are implemented and
     * their preconditions are checked at every call, so this is 1 exactly when a backend
     * exists to write into. */
    out->can_write = out->can_allocate;
    /* The transition needs either the write window (MAP_JIT, reported by the platform hook)
     * or rt_mem_protect (measured: physical run #2 recorded memory.rw_to_rx_transition=PASS
     * on the device for the anonymous path). */
    out->can_transition = out->can_allocate;
    /* Platform part only. On a target where the execute permission is not obtainable the
     * policy is what stops the attempt; this field never claims more than the platform. */
    out->can_execute = (out->map_jit_supported != 0) ? 1 : -1;   /* -1 = not determined here */
    if (out->map_jit_supported == 0 && out->can_transition != 0) {
        out->can_execute = -1;   /* the transition may be accepted and not honoured */
    }
    out->can_rewrite = (out->can_transition != 0) ? 1 : 0;
    out->execution_observed = 0;
    return 0;
}
