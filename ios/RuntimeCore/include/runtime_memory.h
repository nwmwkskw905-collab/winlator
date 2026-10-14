/*
 * runtime_memory.h — page-level memory API and the dual-mapping experiment.
 *
 * PHASE_02_RECONSTRUCTED_POC (see runtime_platform.h).
 *
 * Architecture rule: strict W^X is preferred. No API here maps a page writable and
 * executable at the same time through the generic path; the JIT arena (runtime_jit.h)
 * is the only place that deals with MAP_JIT, and it does so explicitly.
 */
#ifndef RUNTIME_MEMORY_H
#define RUNTIME_MEMORY_H

#include <stddef.h>
#include <stdint.h>

#include "runtime_platform.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Reserve `len` bytes of anonymous memory, readable and writable. Rounds up to a
 * whole number of pages. Returns NULL and sets *err_out to the real errno on failure. */
void *rt_mem_reserve(size_t len, int *err_out);

/* Change the protection of a region previously returned by rt_mem_reserve(). */
int rt_mem_protect(void *addr, size_t len, rt_prot_t prot, int *err_out);

/* Release a region. */
int rt_mem_release(void *addr, size_t len, int *err_out);

/* Read `len` bytes from `addr` (one volatile read per page). Returns 0 on success. */
int rt_mem_read_probe(const void *addr, size_t len);

/*
 * What the kernel reports about a mapping's protection, in one call.
 *
 * Why this exists (pass 05): a successful mprotect(PROT_READ|PROT_EXEC) is a REQUEST that was
 * accepted, not a statement that the page is executable. On platforms that enforce execute
 * permission out of band (iOS 26 on TXM/SPTM hardware) the two can differ, and physical run
 * #3 died exactly there. A caller that wants to distinguish "accepted" from "honoured" asks
 * this function and records both.
 *
 * Bits are rt_mem_observed_t. Returns 0 with *bits_out set; -1 with errno=ENOTSUP when this
 * platform cannot answer (then the caller must record "not observable", never a guess) or
 * another real errno from the query. The probe never touches the pages: it queries metadata
 * only, so it cannot fault and cannot change any protection.
 */
int rt_mem_protection_observed(const void *addr, size_t len, unsigned *bits_out, int *err_out);

/* Bit values returned above (they are a query result, not an rt_prot_t value). */
typedef enum {
    RT_MEM_OBSERVED_NONE  = 0u,
    RT_MEM_OBSERVED_READ  = 1u,
    RT_MEM_OBSERVED_WRITE = 2u,
    RT_MEM_OBSERVED_EXEC  = 4u
} rt_mem_observed_t;

/* Renders observed bits as "R", "RW", "R-X", "RWX", "-" (never NULL). */
const char *rt_mem_observed_name(unsigned bits);

/*
 * Attempt an access at `addr` and report whether the CPU refused it.
 * write != 0 writes a byte, otherwise reads one.
 * Returns:
 *    0  access completed without a fault;
 *   -1  a fault was observed (SIGSEGV/SIGBUS caught by a temporary handler),
 *       *fault_addr_out receives si_addr;
 *   -2  this process cannot install the temporary handler (errno in *err_out).
 * The mapping is never modified by this function.
 */
int rt_mem_fault_probe(void *addr, int write, void **fault_addr_out, int *err_out);

/*
 * Dual mapping experiment: two views of the same physical memory, one writable and
 * one executable/readable. Implemented with a POSIX shared-memory object mapped twice.
 * On platforms that refuse PROT_EXEC on such a mapping (expected on iOS without the
 * right signing context) creation fails with the real errno and `supported` stays 0.
 * Success here is a *host* observation only; iOS behaviour is UNTESTED until a device
 * runs the same code.
 */
/* Which step of the dual-mapping experiment failed. Physical run #1 (iPhone 13,
 * 2026-10-02) recorded `memory.dual_mapping_rw_rx = UNSUPPORTED` with
 * "second (executable) view refused errno=1" — but the old code could not know that: it
 * returned -1 from four different places (the unique name, shm_open, ftruncate, the first
 * mmap, the second mmap) and the report *assumed* it was the executable view. On iOS
 * EPERM at shm_open (a sandbox refusal of the object itself) and EPERM at the second mmap
 * (a refusal of the executable view) are different findings, and the record has to say
 * which one happened. */
typedef enum {
    RT_DUAL_STAGE_NONE = 0,   /* success */
    RT_DUAL_STAGE_NAME,       /* composing the unique object name failed */
    RT_DUAL_STAGE_SHM_OPEN,   /* shm_open(3) */
    RT_DUAL_STAGE_FILE_OPEN,  /* open(2) of the file-backed object (iOS backend) */
    RT_DUAL_STAGE_FTRUNCATE,  /* ftruncate(2) */
    RT_DUAL_STAGE_MAP_RW,     /* first mmap(2), the writable view */
    RT_DUAL_STAGE_MAP_RX,     /* second mmap(2), the executable view */
    RT_DUAL_STAGE_ALIAS_CHECK /* both views exist and disagree about their contents */
} rt_dual_stage_t;

const char *rt_dual_stage_name(rt_dual_stage_t stage);

typedef struct rt_dual_map {
    void  *rw;        /* writable view */
    void  *rx;        /* executable/readable view */
    size_t len;       /* page-rounded length */
    int    err;       /* errno of the failing step, when supported == 0 */
    rt_dual_stage_t stage; /* which step failed, when supported == 0 */
    int    supported; /* 1 only when both views exist */
} rt_dual_map_t;

int rt_dual_map_create(size_t len, rt_dual_map_t *out);

/*
 * Same experiment with the backing object an iOS app *can* create: a regular file inside a
 * directory this process may write (the app container / workdir), mapped MAP_SHARED twice.
 *
 * Why this exists: physical run #2 recorded `shm_open refused errno=1` (EPERM) on the
 * iPhone. That is a statement about the *named POSIX* namespace — the iOS sandbox refuses
 * system-wide names to third-party apps — and it must not be read as "this device cannot
 * share memory" or "dual mapping is impossible". This backend keeps exactly the semantics
 * the experiment needs (one physical object, two live views, one writable and one
 * executable, aliasing verified, object unlinked immediately so nothing is left behind)
 * while using only mechanisms the app sandbox does grant.
 *
 * Selection is explicit and per platform: rt_dual_map_create() stays the first and default
 * mechanism on every platform (Linux and macOS are untouched); callers select this one only
 * for Apple targets after the named path was refused, and record both stage results.
 * Being a capability experiment, a refusal here is reported with its real stage and errno —
 * never converted into success, and never treated as a requirement of the port.
 */
int rt_dual_map_create_file_backed(const char *dir, size_t len, rt_dual_map_t *out);

/*
 * The SAFE half of the experiment (pass 05): the same app-owned, file-backed MAP_SHARED
 * object mapped TWICE, one view read-write and the second read-only. No page is ever asked
 * to be executable, so this call carries none of the risk of entering written memory — and
 * it answers the first of the two questions physical run 03 left merged:
 *
 *     can this device hold two live views of one object, aliasing the same memory?
 *     (separate from: can one of those views be executable?)
 *
 * `out->rx` holds the read-only view on success; `out->supported` is 1 only when both views
 * exist and alias. Refusals carry their real stage and errno, as everywhere in this module.
 */
int rt_dual_map_create_file_backed_readonly_view(const char *dir, size_t len,
                                                 rt_dual_map_t *out);
int rt_dual_map_destroy(rt_dual_map_t *map);

/* Writes a known pattern through `rw` and reads it back through `rx`.
 * Returns 1 when the two views alias the same memory, 0 when they do not,
 * -1 when the map is not usable. */
int rt_dual_map_views_aliased(rt_dual_map_t *map);

#ifdef __cplusplus
}
#endif

#endif /* RUNTIME_MEMORY_H */
