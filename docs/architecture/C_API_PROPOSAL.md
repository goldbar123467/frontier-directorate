# Initial C API proposal

Status: Gate Zero proposal, frozen for U01 implementation after approval.

## ABI and ownership rules

- Public header compiles as C17 and C++20 and wraps declarations in
  `extern "C"` for C++.
- Exported names use `fd_`. Wire/value structs use fixed-width integer fields
  and bounded byte arrays and start with `uint32_t struct_size` and
  `uint32_t abi_version` when extensible. Process-local callback/buffer structs
  may contain pointers but are never serialized or passed across processes.
- `fd_context` and `fd_world` are incomplete types. A context owns configuration,
  allocator callbacks, and immutable tables; a world owns all authoritative
  mutable state. Destroying a context with live worlds returns `FD_ERR_BUSY`.
- Caller owns input memory and output buffers. The library owns opaque handles.
  Query views are copied to caller structs; no pointer into authoritative state
  survives a call.
- Create/load functions publish an output handle only on success. Destroy accepts
  a pointer-to-handle, clears it, and treats an already-null handle as success.
- No exception, `errno`, logging callback, or global last-error crosses the ABI.
  Every call returns `fd_result`; optional caller diagnostics receive stable
  code, field, index, and bounded UTF-8 explanatory text.
- The same world cannot be called concurrently. Separate worlds under the same
  immutable context can be called concurrently. Rendering/querying is not safe
  concurrently with stepping that same world unless using a snapshot.

## Core types

```c
typedef struct fd_context fd_context;
typedef struct fd_world fd_world;

typedef uint64_t fd_entity_id;

typedef uint32_t fd_result;

#define FD_OK                         UINT32_C(0)
#define FD_ERR_INVALID_ARGUMENT       UINT32_C(1)
#define FD_ERR_INVALID_SIZE           UINT32_C(2)
#define FD_ERR_OUT_OF_RANGE           UINT32_C(3)
#define FD_ERR_CAPACITY               UINT32_C(4)
#define FD_ERR_OUT_OF_MEMORY          UINT32_C(5)
#define FD_ERR_GENERATION_EXHAUSTED   UINT32_C(6)
#define FD_ERR_INVALID_ACTION         UINT32_C(7)
#define FD_ERR_BUFFER_TOO_SMALL       UINT32_C(8)
#define FD_ERR_FORMAT                 UINT32_C(9)
#define FD_ERR_VERSION                UINT32_C(10)
#define FD_ERR_CHECKSUM               UINT32_C(11)
#define FD_ERR_STATE                  UINT32_C(12)
#define FD_ERR_BUSY                   UINT32_C(13)
#define FD_ERR_INTERNAL               UINT32_C(14)

typedef struct fd_diagnostic {
    uint32_t struct_size;
    uint32_t abi_version;
    fd_result code;
    uint32_t field_id;
    uint32_t item_index;
    char message[160];
} fd_diagnostic;

typedef struct fd_version {
    uint32_t struct_size;
    uint16_t api_major, api_minor, api_patch;
    uint16_t generator_major, generator_minor;
    uint16_t save_major, save_minor;
    uint16_t replay_major, replay_minor;
} fd_version;
```

ID 0 is invalid. Entity IDs encode a 32-bit bounded slot and 32-bit generation;
callers treat them as opaque integers. All enum wire values are explicit and
append-only within a compatible major version.

## Lifecycle and world generation

```c
fd_result fd_get_version(fd_version *out);
fd_result fd_context_create(const fd_context_config *config,
                            fd_context **out, fd_diagnostic *diag);
fd_result fd_context_destroy(fd_context **context, fd_diagnostic *diag);

fd_result fd_world_generate(fd_context *context,
                            const fd_world_config *config,
                            uint64_t seed,
                            const fd_content_manifest *content,
                            fd_world **out, fd_generation_report *report,
                            fd_diagnostic *diag);
fd_result fd_world_destroy(fd_world **world, fd_diagnostic *diag);
fd_result fd_world_validate(const fd_world *world,
                            fd_validation_report *report,
                            fd_diagnostic *diag);
```

Configs carry dimensions, bounded capacities, generator version, attempt budget,
and determinism mode. Unknown nonzero reserved fields are rejected. All lengths
are validated before allocation or multiplication.

## Queries and snapshots

```c
fd_result fd_world_get_info(const fd_world *, fd_world_info *, fd_diagnostic *);
fd_result fd_world_get_tile(const fd_world *, uint32_t x, uint32_t y,
                            fd_tile_view *, fd_diagnostic *);
fd_result fd_world_get_entity(const fd_world *, fd_entity_id,
                              fd_entity_view *, fd_diagnostic *);
fd_result fd_world_get_region(const fd_world *, fd_entity_id,
                              fd_region_view *, fd_diagnostic *);
fd_result fd_world_state_hash(const fd_world *, uint8_t out_sha256[32],
                              fd_diagnostic *);

fd_result fd_snapshot_create(const fd_world *, fd_visibility_scope,
                             fd_entity_id viewer, fd_snapshot **out,
                             fd_diagnostic *);
fd_result fd_snapshot_destroy(fd_snapshot **, fd_diagnostic *);
```

U01 supports `FD_VISIBILITY_REFERENCE` only and rejects ordinary actor scopes;
this prevents an omniscient placeholder from becoming an actor observation.

## Hierarchical action and stepping

```c
typedef uint32_t fd_action_category;

#define FD_ACTION_PASS             UINT32_C(1)
#define FD_ACTION_EXPEDITION       UINT32_C(2)
#define FD_ACTION_CONSTRUCTION     UINT32_C(3)
#define FD_ACTION_TRANSPORT        UINT32_C(4)
#define FD_ACTION_PRODUCTION       UINT32_C(5)
#define FD_ACTION_TRADE            UINT32_C(6)
#define FD_ACTION_DIPLOMACY        UINT32_C(7)
#define FD_ACTION_ADMINISTRATION   UINT32_C(8)
#define FD_ACTION_INTELLIGENCE     UINT32_C(9)
#define FD_ACTION_MILITARY         UINT32_C(10)
#define FD_ACTION_RESEARCH         UINT32_C(11)
#define FD_ACTION_POLITICS         UINT32_C(12)

fd_result fd_action_mask_query(const fd_world *, fd_entity_id actor,
                               const fd_action_prefix *, fd_action_mask *,
                               fd_diagnostic *);
fd_result fd_action_validate(const fd_world *, fd_entity_id actor,
                             const fd_action *, fd_diagnostic *);
fd_result fd_world_step(fd_world *, const fd_joint_decision *,
                        fd_step_result *, fd_diagnostic *);
```

Masks use caller-provided bit buffers with exact bit counts and a schema ID.
`fd_world_step` first validates every seat and parameter, plans conflicts, then
commits. Any invalid action returns an error, appends a diagnostic replay record,
and leaves the authoritative state hash unchanged. Missing seat actions are not
silently supplied. In U01 every required seat must explicitly submit `PASS`.

## Serialization and replay

```c
fd_result fd_world_save_size(const fd_world *, uint64_t *out_size,
                             fd_diagnostic *);
fd_result fd_world_save(const fd_world *, void *buffer, uint64_t capacity,
                        uint64_t *written, fd_diagnostic *);
fd_result fd_world_load(fd_context *, const void *bytes, uint64_t size,
                        fd_world **out, fd_diagnostic *);
fd_result fd_replay_open(fd_context *, const void *bytes, uint64_t size,
                         fd_replay **out, fd_diagnostic *);
fd_result fd_replay_advance(fd_replay *, fd_world *, uint64_t decisions,
                            fd_replay_status *, fd_diagnostic *);
```

Size queries use 64-bit values, but context capacities cap accepted sizes.
Serializers never write partial logical files: insufficient buffers report the
required size and leave `written` zero.

## Rendering boundary

Rendering is a separate library consuming `fd_snapshot`; the core has no terminal
or locale calls. The renderer follows the same size-query/caller-buffer pattern.

```c
fd_result fd_ascii_measure(const fd_snapshot *, const fd_ascii_options *,
                           uint64_t *out_size, fd_diagnostic *);
fd_result fd_ascii_render(const fd_snapshot *, const fd_ascii_options *,
                          char *buffer, uint64_t capacity,
                          uint64_t *written, fd_diagnostic *);
```

## C++ wrapper contract

The C++20 wrapper owns handles in move-only RAII types, represents failures as an
explicit result type (not exceptions crossing C), and never caches or calculates
rule outcomes. Batched workers own distinct worlds. The wrapper is tested against
the C ABI from both a C translation unit and a C++ translation unit.
