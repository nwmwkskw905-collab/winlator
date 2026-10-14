/*
 * runtime_memory.c — page API, fault probe and the platform selector.
 *
 * PHASE_02_RECONSTRUCTED_POC (see runtime_platform.h).
 */
#include "runtime_memory.h"
#include "runtime_signals.h"

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#if defined(__linux__)
#include <inttypes.h>          /* parsing /proc/self/maps */
#endif
#if defined(__APPLE__)
#include <mach/mach.h>         /* mach_vm_region: the SDK's public way to ask about a range */
#include <mach/mach_vm.h>
#endif

/* ------------------------------------------------------------------ names */

const char *rt_status_name(rt_status_t status)
{
    switch (status) {
    case RT_PASS:           return "PASS";
    case RT_FAIL:           return "FAIL";
    case RT_BLOCKED:        return "BLOCKED";
    case RT_UNSUPPORTED:    return "UNSUPPORTED";
    case RT_UNTESTED:       return "UNTESTED";
    case RT_NOT_APPLICABLE: return "NOT_APPLICABLE";
    }
    return "UNKNOWN";
}

const char *rt_prot_name(rt_prot_t prot)
{
    if (prot == RT_PROT_NONE)  return "NONE";
    if (prot == RT_PROT_READ)  return "R";
    if (prot == RT_PROT_WRITE) return "W";
    if (prot == RT_PROT_EXEC)  return "X";
    if (prot == (RT_PROT_READ | RT_PROT_WRITE)) return "RW";
    if (prot == (RT_PROT_READ | RT_PROT_EXEC))  return "R-X";
    if (prot == (RT_PROT_WRITE | RT_PROT_EXEC)) return "W-X";
    if (prot == (RT_PROT_READ | RT_PROT_WRITE | RT_PROT_EXEC)) return "RWX";
    return "?";
}

/* ---------------------------------------------------------------- platform */

int rt_platform_apple_target(void)
{
#if defined(__APPLE__)
    return RT_APPLE_TARGET;
#else
    return RT_APPLE_TARGET_NONE;
#endif
}

const char *rt_platform_apple_target_name(void)
{
    switch (rt_platform_apple_target()) {
    case RT_APPLE_TARGET_MACOS:
        return "macos";
    case RT_APPLE_TARGET_IPHONE_DEVICE:
        return "ios-device";
    case RT_APPLE_TARGET_IPHONE_SIMULATOR:
        return "ios-simulator";
    case RT_APPLE_TARGET_UNKNOWN_APPLE:
        return "apple-unknown";
    case RT_APPLE_TARGET_NONE:
    default:
        return "none";
    }
}

int rt_platform_random_bytes(void *buffer, size_t length, int *err_out)
{
    const rt_platform_t *platform = rt_platform_current();
    if (platform == NULL || platform->random_bytes == NULL) {
        if (err_out != NULL) {
            *err_out = ENOTSUP;
        }
        return -1;
    }
    return platform->random_bytes(buffer, length, err_out);
}

int rt_platform_unique_shm_name(char *out, size_t capacity, const char *prefix, int *err_out)
{
    static const unsigned char zero[8] = { 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u };
    unsigned char random_bytes[8];
    int written;

    if (out == NULL || prefix == NULL || capacity == 0u) {
        if (err_out != NULL) {
            *err_out = EINVAL;
        }
        return -1;
    }
    if (rt_platform_random_bytes(random_bytes, sizeof(random_bytes), err_out) != 0) {
        out[0] = '\0';
        return -1;   /* no unpredictable name -> the caller must fail, never guess */
    }
    if (memcmp(random_bytes, zero, sizeof(zero)) == 0) {
        /* A source that returns nothing but zeros is not a source. One in 2^64, but
         * silently accepting it would be exactly the kind of "looks fine" fallback
         * this project refuses to ship. */
        if (err_out != NULL) {
            *err_out = EIO;
        }
        out[0] = '\0';
        return -1;
    }
    written = snprintf(out, capacity, "%s_%ld_%02x%02x%02x%02x%02x%02x%02x%02x",
                       prefix, (long)getpid(),
                       random_bytes[0], random_bytes[1], random_bytes[2], random_bytes[3],
                       random_bytes[4], random_bytes[5], random_bytes[6], random_bytes[7]);
    if (written < 0 || (size_t)written >= capacity) {
        out[0] = '\0';
        if (err_out != NULL) {
            *err_out = ENAMETOOLONG;
        }
        return -1;
    }
    if (err_out != NULL) {
        *err_out = 0;
    }
    return 0;
}

const rt_platform_t *rt_platform_current(void)
{
#if defined(__APPLE__)
    return &rt_platform_darwin;
#elif defined(__linux__)
    return &rt_platform_linux;
#else
    return &rt_platform_linux;
#endif
}

const char *rt_platform_name(void)
{
    const rt_platform_t *platform = rt_platform_current();
    return (platform != NULL && platform->name != NULL) ? platform->name : "unknown";
}

int rt_platform_page_size(void)
{
    const rt_platform_t *platform = rt_platform_current();
    if (platform == NULL || platform->page_size == NULL) {
        return 4096;
    }
    return platform->page_size();
}

uint32_t rt_platform_capabilities(void)
{
    const rt_platform_t *platform = rt_platform_current();
    if (platform == NULL || platform->capabilities == NULL) {
        return 0u;
    }
    return platform->capabilities();
}

int rt_platform_is_darwin(void)
{
    return (rt_platform_current() == &rt_platform_darwin) ? 1 : 0;
}

int rt_platform_is_apple(void)
{
#if defined(__APPLE__)
    return 1;
#else
    return 0;
#endif
}

/* ------------------------------------------------------------------ memory */

void *rt_mem_reserve(size_t len, int *err_out)
{
    const rt_platform_t *platform = rt_platform_current();
    size_t page = (size_t)rt_platform_page_size();
    size_t rounded;

    if (len == 0) {
        if (err_out != NULL) {
            *err_out = EINVAL;
        }
        return NULL;
    }
    /* round up without overflowing */
    if (len > SIZE_MAX - page) {
        if (err_out != NULL) {
            *err_out = EOVERFLOW;
        }
        return NULL;
    }
    rounded = ((len + page - 1u) / page) * page;
    return platform->mem_map(rounded, RT_PROT_READ | RT_PROT_WRITE, err_out);
}

int rt_mem_protect(void *addr, size_t len, rt_prot_t prot, int *err_out)
{
    const rt_platform_t *platform = rt_platform_current();
    size_t page = (size_t)rt_platform_page_size();
    size_t rounded;

    if (addr == NULL || len == 0) {
        if (err_out != NULL) {
            *err_out = EINVAL;
        }
        return -1;
    }
    /* The same overflow guard rt_mem_reserve() already had: without it, len + page - 1
     * wraps and the rounded length becomes tiny, so the call would protect far fewer bytes
     * than it was asked to while reporting success — a silent protection shortfall on the
     * caller's assumption that the whole region changed. */
    if (len > SIZE_MAX - page) {
        if (err_out != NULL) {
            *err_out = EOVERFLOW;
        }
        return -1;
    }
    rounded = ((len + page - 1u) / page) * page;
    return platform->mem_protect(addr, rounded, prot, err_out);
}

int rt_mem_release(void *addr, size_t len, int *err_out)
{
    const rt_platform_t *platform = rt_platform_current();
    if (addr == NULL) {
        if (err_out != NULL) {
            *err_out = EINVAL;
        }
        return -1;
    }
    return platform->mem_unmap(addr, len, err_out);
}

int rt_mem_read_probe(const void *addr, size_t len)
{
    const unsigned char *bytes = (const unsigned char *)addr;
    size_t page = (size_t)rt_platform_page_size();
    size_t offset;
    volatile unsigned char sink = 0;

    if (addr == NULL || len == 0) {
        return -1;
    }
    for (offset = 0; offset < len; offset += page) {
        sink = (unsigned char)(sink ^ bytes[offset]);
    }
    /* touch the last byte as well: a region that is only partly mapped must fail */
    sink = (unsigned char)(sink ^ bytes[len - 1u]);
    (void)sink; /* the value is irrelevant: reaching this line is the result */
    return 0;
}

int rt_mem_fault_probe(void *addr, int write, void **fault_addr_out, int *err_out)
{
    rt_fault_guard_t guard;
    int rc;

    if (fault_addr_out != NULL) {
        *fault_addr_out = NULL;
    }
    rc = rt_fault_guard_begin(&guard, err_out);
    if (rc != 0) {
        return -2;
    }
    guard.armed = 1;
    if (RT_FAULT_GUARD_TRY(&guard)) {
        if (write != 0) {
            *(volatile unsigned char *)addr = 0x5Au;
        } else {
            volatile unsigned char sink = *(volatile unsigned char *)addr;
            (void)sink;
        }
        (void)rt_fault_guard_end(&guard);
        return 0;
    }
    if (fault_addr_out != NULL) {
        *fault_addr_out = rt_fault_guard_addr(&guard);
    }
    (void)rt_fault_guard_end(&guard);
    return -1;
}

/* --------------------------------------------------- observed protection (pass 05) */

const char *rt_mem_observed_name(unsigned bits)
{
    static const char *const combos[8] = {
        "-",        /* 0: none */
        "R",        /* 1 */
        "W",        /* 2 */
        "RW",       /* 3 */
        "X",        /* 4 */
        "R-X",      /* 5 */
        "W-X",      /* 6 */
        "RWX"       /* 7 */
    };
    return combos[bits & 7u];
}

#if defined(__linux__)
/* Linux: /proc/self/maps is the documented interface for a process to read its own
 * mappings ("r-xp 00000000 ... path"). The parser is deliberately strict: a malformed line
 * is skipped, and if no line covers the range the answer is "not observable" rather than a
 * guess. */
static int linux_observed_bits(const void *addr, size_t len, unsigned *bits_out, int *err_out)
{
    FILE *maps;
    char line[512];
    uintptr_t want_start;
    uintptr_t want_end;
    int found = 0;
    unsigned bits = 0u;

    if (len == 0u) {
        if (err_out != NULL) {
            *err_out = EINVAL;
        }
        return -1;
    }
    want_start = (uintptr_t)addr;
    want_end = want_start + len;   /* overflow checked by the caller's own sizing rules */

    maps = fopen("/proc/self/maps", "r");
    if (maps == NULL) {
        if (err_out != NULL) {
            *err_out = errno;
        }
        return -1;
    }
    while (fgets(line, (int)sizeof(line), maps) != NULL) {
        unsigned long start = 0ul;
        unsigned long end = 0ul;
        char perms[5] = { 0, 0, 0, 0, 0 };
        if (sscanf(line, "%lx-%lx %4s", &start, &end, perms) != 3) {
            continue;                     /* malformed line: skipped, never guessed */
        }
        if ((uintptr_t)start > want_start) {
            break;                        /* maps are ordered: past the range already */
        }
        if (want_start >= (uintptr_t)start && want_end <= (uintptr_t)end) {
            bits = 0u;
            if (perms[0] == 'r') {
                bits |= RT_MEM_OBSERVED_READ;
            }
            if (perms[1] == 'w') {
                bits |= RT_MEM_OBSERVED_WRITE;
            }
            if (perms[2] == 'x') {
                bits |= RT_MEM_OBSERVED_EXEC;
            }
            found = 1;
            break;
        }
    }
    (void)fclose(maps);
    if (found == 0) {
        if (err_out != NULL) {
            *err_out = ENOTSUP;
        }
        return -1;
    }
    if (bits_out != NULL) {
        *bits_out = bits;
    }
    if (err_out != NULL) {
        *err_out = 0;
    }
    return 0;
}
#endif /* __linux__ */

#if defined(__APPLE__)
/* Apple: mach_vm_region is the public, documented way for a process to ask the kernel about
 * a range of its own address space (no entitlement, no private API). If the call fails for
 * any reason the answer is ENOTSUP: "not observable" is a legitimate result and must never
 * be replaced by an assumption. */
static int apple_observed_bits(const void *addr, size_t len, unsigned *bits_out, int *err_out)
{
    mach_vm_address_t address = (mach_vm_address_t)(uintptr_t)addr;
    mach_vm_size_t size = 0u;
    vm_region_basic_info_data_64_t info;
    mach_port_t object_name = MACH_PORT_NULL;
    mach_msg_type_number_t count = VM_REGION_BASIC_INFO_COUNT_64;
    kern_return_t kr;

    (void)len; /* the query answers for the containing region, which is what a caller needs */
    memset(&info, 0, sizeof(info));
    kr = mach_vm_region(mach_task_self(), &address, &size, VM_REGION_BASIC_INFO_64,
                        (vm_region_info_t)&info, &count, &object_name);
    if (object_name != MACH_PORT_NULL) {
        (void)mach_port_deallocate(mach_task_self(), object_name);
    }
    if (kr != KERN_SUCCESS) {
        if (err_out != NULL) {
            *err_out = ENOTSUP;
        }
        return -1;
    }
    if (bits_out != NULL) {
        unsigned bits = 0u;
        if ((info.protection & VM_PROT_READ) != 0) {
            bits |= RT_MEM_OBSERVED_READ;
        }
        if ((info.protection & VM_PROT_WRITE) != 0) {
            bits |= RT_MEM_OBSERVED_WRITE;
        }
        if ((info.protection & VM_PROT_EXECUTE) != 0) {
            bits |= RT_MEM_OBSERVED_EXEC;
        }
        *bits_out = bits;
    }
    if (err_out != NULL) {
        *err_out = 0;
    }
    return 0;
}
#endif /* __APPLE__ */

int rt_mem_protection_observed(const void *addr, size_t len, unsigned *bits_out, int *err_out)
{
    if (addr == NULL || len == 0u || bits_out == NULL) {
        if (err_out != NULL) {
            *err_out = EINVAL;
        }
        return -1;
    }
    if (err_out != NULL) {
        *err_out = 0;
    }
#if defined(__linux__)
    return linux_observed_bits(addr, len, bits_out, err_out);
#elif defined(__APPLE__)
    return apple_observed_bits(addr, len, bits_out, err_out);
#else
    /* No public mechanism on this platform: UNTESTED, never a guess. */
    if (err_out != NULL) {
        *err_out = ENOTSUP;
    }
    (void)addr;
    (void)len;
    return -1;
#endif
}
