# ADR-001: Split-span SPSC frame ring

- Status: Accepted
- Date: 2026-08-15

## Context

V1 needs a bounded, non-blocking producer channel for long-lived, latency-
sensitive game-server threads. Each channel has exactly one producer and one
consumer. Records are variable length. The design must preserve all usable ring
capacity without requiring producers to reserve a shared ticket.

## Decision

### Storage and cursors

- The ring capacity is a power of two; the default is 64 KiB.
- Storage and every frame start are aligned to 8 bytes.
- Producer and consumer use 64-bit monotonically increasing logical byte
  cursors. A physical position is `cursor & (capacity - 1)`.
- The maintained invariant is:

  ```text
  0 <= published_write - published_read <= capacity
  ```

- Producer-local and consumer-local cursors are ordinary integers.
- The published write and read cursors are atomics placed on independent cache
  lines. Cold metadata and statistics must not share those cache lines.
- The normal SPSC path contains no CAS, `fetch_add`, per-slot state, or version
  number.

### Frame layout

The ring owns an 8-byte framing header:

```cpp
struct FrameHeader {
    std::uint32_t frame_bytes;   // header + payload + alignment padding
    std::uint32_t payload_bytes; // exact number of meaningful payload bytes
};
```

`frame_bytes` is `align_up(sizeof(FrameHeader) + payload_bytes, 8)`. Because the
capacity and all frame sizes are multiples of eight, the 8-byte header is always
physically contiguous. The payload may cross the ring end and is exposed as:

```cpp
struct MutableSegments {
    std::span<std::byte> first;
    std::span<std::byte> second;
};
```

The sum of the two span lengths equals `payload_bytes`; `second` is empty for
the common contiguous case.

### Reservation and publication

- `try_reserve(payload_bytes)` checks overflow and the 8 KiB payload limit.
- The producer calculates free space using its cached read cursor. Only when
  cached space is insufficient does it acquire-load the published read cursor.
- If refreshed space is still insufficient, the operation returns `full`
  immediately. No cursor changes, waiting, retry loop, allocation, or fallback
  I/O occurs.
- Only one outstanding producer reservation is allowed per channel.
- A reservation is move-only. Explicit `commit()` is required. Destruction
  without commit abandons the reservation and leaves the write cursor unchanged.
- `commit()` writes the frame header after the payload is complete, advances the
  producer-local cursor, and release-stores the published write cursor.
- The producer publishes every accepted record; implicit publication batching
  is not used because an isolated log must become visible without a later call.

### Consumption and reclamation

- The consumer uses its cached published-write snapshot until exhausted, then
  acquire-loads the current published write cursor.
- It validates both sizes before constructing a frame view. A malformed frame
  must never cause an out-of-bounds access or a zero-length progress loop.
- The consumer finishes decoding/formatting or copies the data into backend-
  owned memory before advancing the reusable boundary.
- It release-stores the published read cursor after 32 records, 4 KiB, a channel
  switch, or observing the channel empty, whichever happens first.
- No view into ring storage may survive publication of the corresponding read
  cursor.

### Memory ordering

```text
producer payload/header writes
    -> published_write.store(release)
    -> published_write.load(acquire)
    -> consumer payload/header reads

consumer finishes all ring reads
    -> published_read.store(release)
    -> published_read.load(acquire)
    -> producer may overwrite released bytes
```

Cached peer cursors may be stale. Staleness only underestimates available work
or space; it must never permit reading unpublished bytes or overwriting
unconsumed bytes.

### Crossing the physical tail

No padding frame is inserted. A wrapped payload is written directly through its
two spans. The consumer handles fmt's contiguous-string requirement as follows:

- contiguous frame: decode directly from ring storage;
- wrapped frame: copy the complete payload into an 8 KiB preallocated scratch
  buffer, then decode and format from that contiguous buffer.

The fast encoder must branch once per record between the contiguous and split
paths. It must not impose a split check on every scalar field of every
contiguous record.

## Consequences

### Benefits

- Every byte of ring capacity remains usable; there is no tail padding.
- Full/empty state is unambiguous because logical cursors are not reduced to
  physical offsets.
- The producer has no shared RMW contention and never waits when full.
- Variable-sized records near the tail do not cause conservative admission
  failures solely because contiguous physical space is unavailable.

### Costs

- A wrapped producer encode may perform two copies instead of one.
- A wrapped consumer record is copied once into scratch before formatting.
- The implementation and tests are more involved than a contiguous-only frame
  queue.
- This design is not assumed to be universally faster than tail padding. Its
  expected advantage is capacity utilization and overload behavior, not the CPU
  cost of the individual wrapped record.

## Required comparison benchmark

Before claiming a performance advantage, compare this split-span design with a
tail-padding/contiguous-payload variant using the same frame protocol.

Test matrix:

- ring capacities: 4 KiB, 64 KiB, 256 KiB;
- fixed 64-byte records;
- variable 32-512-byte records;
- mixed game-style records with occasional 1-8 KiB payloads;
- low occupancy, burst load, and near-full sustained load.

Report:

- producer P50/P99/P99.9;
- consumer throughput;
- accepted and dropped counts;
- tail bytes wasted per accepted record;
- wrapped-frame rate and scratch-copy bytes;
- CPU cycles and cache misses where available.

Expected hypothesis, not a promised result:

- at low occupancy with small records, the contiguous tail-padding variant may
  be slightly faster or indistinguishable;
- under variable-size bursts and high occupancy, split spans may accept more
  records and reduce drops because no tail capacity is discarded;
- with large records in a small ring, the capacity benefit and scratch-copy cost
  both become more visible.

## Verification requirements

- Exhaustively test every start offset and valid length on tiny rings.
- Test exact-tail, split-by-one-byte, exact-full, full, empty, abort, and reuse.
- Compare random operations against a deque-based reference model.
- Run long producer/consumer sequence and checksum tests under TSan.
- Inject cursors near `UINT64_MAX` and verify unsigned wrap behavior.
- Verify `attempted == accepted + dropped` in all overload tests.

