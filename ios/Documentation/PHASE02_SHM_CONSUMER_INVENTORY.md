# PHASE 02 — named POSIX shared memory: consumer inventory and classification

PHASE_02_RECONSTRUCTED_POC (pass 05). Machine-readable: `tools/audit_shm_requirements.py`
reads the table below and holds it against the tree.

## The two questions that keep being confused

1. **Is the named POSIX namespace (`shm_open`/`shm_unlink`) available on this target?**
   Measured, and on the physical iPhone the answer is no: `shm_open` is refused with
   `EPERM errno=1` before any object is created (physical runs 02 and 04).
2. **Does this runtime need it?** A different question with a different answer, and one that
   must be answered by inventory and measurement, never by extrapolation from (1).

The classification vocabulary is closed:

| status | meaning |
| --- | --- |
| `REQUIRED` | a shipped runtime path cannot work without the named namespace; its absence blocks that path |
| `REPLACEABLE` | the consumer exists in this tree and has a measured alternative on this target |
| `NOT_MEASURED` | the consumer is out of this tree (upstream/Winlator/Wine/Box64/X-server path), so this pass makes **no claim** about it |
| `SCAFFOLD` | uses the System V SHM API (a different namespace on Linux) and is not part of the iOS runtime |

Hypothesis is not one of the values: "Wine surely needs it" has no entry here.

## Inventory

| consumer | path | api | status | evidence / alternative |
| --- | --- | --- | --- | --- |
| `ipc.posix_shm` capability probe | `RuntimeCore/src/runtime_ipc.c` | `shm_open`, `shm_unlink` | `REPLACEABLE` | the probe *is* the question (1): it measures the namespace and classifies the refusal through `phase02_classify_shm_error`. It is not a dependency of anything: `--suite ipc` is an experiment, and no other suite calls it. |
| dual-mapping experiment, named backend | `RuntimeCore/src/runtime_dual_mapping.c` | `shm_open`, `shm_unlink` | `REPLACEABLE` | the same file provides `rt_dual_map_create_file_backed()` / `_readonly_view()` (app-container file, `MAP_SHARED`, unlinked immediately); measured as `memory.dual_mapping_views` — PASS on the host and on the device path that does not need the namespace. This is the recommended implementation for iOS backends of the same ABI. |
| System V SHM shims (Android side) | `glibc_patches/sysdeps/unix/sysv/linux/shmget.c` (`shmat.c`, `shmctl.c`) | `shmget`, `shmat`, `shmctl` | `SCAFFOLD` | a different API on a different platform (Linux System V IPC keys, not the POSIX name in `/dev/shm`). Not linked into the iOS runtime; listed so the inventory does not silently drop the SHM family. |
| Wine / Box64 / X-server paths | — (not vendored in this workspace) | unknown | `NOT_MEASURED` | the Android application tree in this workspace contains assets only (`app/app/src/main/assets/box64`), no sources. This pass therefore claims nothing about upstream consumers; the phase that vendors them must extend this table before relying on the namespace. |
| iOS runtime execution and memory paths | `RuntimeCore/src/runtime_exec_memory.c`, `runtime_jit.c`, `runtime_loader.c`, `ios/box64-registers/**` | none | `REPLACEABLE` | nothing to replace: enforced by R2 below — these paths contain no `shm_open`/`shm_unlink` and never fail because the namespace is absent. |

## Consequences recorded in every report

```
NOTE=posix_shm_requirement NAMED_POSIX_SHM=REPLACEABLE required_by_runtime=no \
     consumers_measured=none_on_execution_or_memory_path \
     alternatives=anonymous-MAP_SHARED|app-container-file-backed-MAP_SHARED|mach-VM \
     audit=tools/audit_shm_requirements.py
```

* `ipc.posix_shm` stays `BLOCKED` on the iPhone. That is the honest status of question (1)
  and it is **not** converted to `PASS`, and it does not block any other record.
* The alternatives in the note are measured, not advertised: `memory.dual_mapping_views`
  creates two live views of one app-owned object without the named namespace and verifies the
  alias by writing through one view and reading the other.
* Nothing in this file claims that a future Wine/Box64/X-server port is free of SHM needs —
  only that this runtime's execution and memory paths are, and that the refusal does not
  propagate.
