# ha_core task / thread ownership contract

Status: **frozen Agent A runtime contract**.

`ha_core` intentionally has no internal concurrency machinery. The fixed Device / Entity / State pools and their borrowed pointers are designed for one serial owner task.

## Contract

1. Exactly one application-selected task owns `ha_core` runtime access.
2. All Device/Entity/State mutations run on that owner task.
3. All Agent E Device/Entity/State enumeration and getter calls run on that same owner task.
4. `ha_core_reset()`, State-listener registration, Entity service invocation, and `ha_core_revision()` sampling also run on the owner task.
5. B/C/D or other worker tasks never call `ha_core` directly.
6. Cross-task semantic results are copied through an existing application/FreeRTOS queue to the owner task.
7. `ha_core` itself does not own that queue and does not add a queue manager, dispatcher, lock, mutex, semaphore, critical section, atomic wrapper, or task framework.

The owner can be the LVGL/UI task or another application task, but Agent E enumeration must execute in the same task that owns mutations. The project should choose one owner and keep that ownership stable.

## Required topology

```text
Agent B/C worker task(s)
  discover / parse / match elsewhere
             |
             | copied semantic value only
             v
     existing application queue
             |
             v
       ha_core owner task
        |            |
        | apply      | Agent E enumerate/read
        v            v
   Device / Entity / State
```

A worker-side result is already semantic: Device identity/display fields, Entity identity/domain/display fields, and State/attributes. Raw packets, scanner buffers, recognition DB rows, or pointers whose lifetime belongs to the worker must not be placed into `ha_core`.

The queue message format is application glue outside Agent A. Do not create a new `ha_core` manager/provider/adapter type for it.

## `ha_core_revision()` semantics

`ha_core_revision()` is an invalidation counter only.

It is useful on the owner task when code has resolved borrowed pointers or completed a read pass and wants to know whether owner-task code performed a successful mutation since an earlier sample. On change, discard/re-resolve borrowed views by durable copied keys.

It is explicitly **not**:

- an atomic variable;
- a lock or mutex;
- a memory barrier;
- a seqlock;
- a cross-task publication mechanism;
- a way to make concurrent UI reads safe.

Do not implement this pattern:

```text
worker task mutates ha_core
UI task enumerates concurrently
UI checks revision and retries
```

That has a data race before the revision check and is outside the contract.

The supported pattern is:

```text
worker task copies semantic result to queue
owner task dequeues and mutates ha_core
owner task enumerates/reads for UI
```

Within a read-only owner-task enumeration pass, revision remains stable. After a successful mutation, previously borrowed pointers are treated as invalidated and callers re-resolve them if needed.

## Callback rules

### State listener

`ha_state_set_listener()` registers one lightweight callback. `ha_state_set()` invokes it synchronously on the owner task after publishing the State.

The callback may use the supplied State during that call as a borrowed read-only view. It must not hand that pointer to another task. Typical Agent E behavior is to mark a view/card dirty and render from normal getters/enumerators on the owner task.

### Entity service handler

`ha_entity_call_service()` invokes the Entity's handler synchronously on the owner task.

If control of the physical device belongs to another task, the handler queues a command outward and returns. The protocol task performs the operation and later sends an already-parsed semantic result back through the normal semantic-result queue. The owner task then calls `ha_state_set()`.

This preserves HA service names without turning `ha_core` into a cross-task action framework.

## Minimal owner loop

Illustrative control flow only; the queue implementation remains outside Agent A:

```text
owner task loop:
  dequeue zero or more semantic results
    -> ha_device_upsert
    -> ha_entity_upsert
    -> ha_state_set

  process UI on the same task
    -> ha_device_count / ha_device_at
    -> ha_entity_count_for_device / ha_entity_at_for_device
    -> ha_state_get

  process UI interaction
    -> ha_entity_call_service
```

No lock is required because the pools never have two task owners.

## Regression coverage

`tests/ha_core_owner_task.c` is intentionally single-threaded. It models a copied queued semantic value, applies it only through an owner function, enumerates UI data on that same serial flow, verifies read-only enumeration leaves revision unchanged, then verifies a later owner-task mutation changes revision and the caller re-resolves by a copied `entity_id`.

`tests/run_ha_core_tests.sh` also rejects production `ha_core.c/.h` if common C/FreeRTOS lock or atomic primitives are introduced. This is a policy guard, not a claim that grep can prove thread safety: the actual safety property is single-task ownership.

## Freeze decision

This contract closes the last Agent A freeze item. Do not add internal synchronization, a manager/dispatcher framework, or cross-task access to make a future integration easier. B/C/D integration must adapt at the queue boundary while `ha_core` remains a small serial RAM-only semantic runtime.
