# V1 Decision Log

> This directory records the design of the new logging project. It is not part
> of the BQLog implementation; BQLog is only a reference implementation.

## Project scope

- Status: Accepted
- Date: 2026-08-15
- Target: Linux C++20 real-time game servers and real-time simulation services.
- V1 topology: one SPSC channel per producer thread and one background consumer.
- Producer contract: no formatting, no steady-state heap allocation, no locks,
  no blocking, and no shared atomic read-modify-write operation in the normal
  logging path.
- Backpressure: fixed memory and `drop_new` when a channel is full.
- Ordering: per-thread FIFO only; no strict cross-thread total order.
- Text formatting: deferred to the backend and implemented with fmt in V1.

## Accepted decisions

1. Records may cross the physical end of an SPSC ring and are represented by
   at most two spans. No tail padding or `invalid` frame is inserted.
2. The fast record format uses a static argument schema and a schema-specific
   formatting function. Per-argument TLV tags are not stored in each record.
3. Each callsite is represented in records by a runtime-assigned `uint32_t`
   `CallsiteId`, not by a raw pointer.
4. Callsite registration is a cold-path operation. The ID is cached at the
   callsite; the producer does not query a map or allocate an ID per event.
5. A compile-time `ClockPolicy` boundary is used. Correctness tests can inject a
   simple/fake clock; the optimized Linux x86-64 implementation will use TSC
   with backend calibration and a safe fallback.
6. Backend wake-up is hybrid: active polling while work is flowing, followed
   by waiting when idle. Wake coordination must not add a shared RMW operation
   to every hot-path log call.
7. Default SPSC capacity is 64 KiB. Maximum V1 payload is 8 KiB. Oversized
   records are dropped as a whole and counted; they are never truncated.
8. Wrapped payloads are linearized by the consumer into an 8 KiB preallocated
   scratch buffer before decoding and fmt formatting. Non-wrapped payloads are
   decoded directly from the ring.
9. Consumer space reclamation is published after 32 records, 4 KiB, a channel
   switch, or an empty observation, whichever occurs first.

## Important limitations

- A `CallsiteId` alone does not make plugin hot-unload safe. The registry still
  stores a formatting function that may reside in a dynamic module. V1 requires
  a module to drain its accepted records before unloading.
- Runtime-assigned IDs are not stable across processes or builds. A future
  binary file format must write an `ID -> metadata` table into each log stream.
- V1 does not promise audit-grade durability or survival of machine power loss.

## ADR index

- [ADR-001: Split-span SPSC frame ring](./ADR-001-spsc-frame-ring.md)

