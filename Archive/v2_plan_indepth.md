# Project Status & Time-to-Completion Analysis

Generated 2026-07-06. Based on a full read of `type/`, `common/`, `storage/`, `index/`, `tests/`, and the git history (43 commits, first commit 2026-03-22, most recent 2026-07-03 — roughly 3.5 calendar months of nights/weekends around an internship).

## 1. Current state, component by component

### Type system (`type/`, `common/`) — done
`Value`/`Type`/`Column`/`Schema`/`Tuple` are implemented and unit-tested (boolean, numeric, float, varchar, column, schema, tuple all have passing gtest files). `Type` supports `Compare`, and arithmetic per the commit log. This is a real, working polymorphic type system — not a shortcut. Nothing else in the plan blocks on this layer.

### Storage layer (`storage/`) — implemented, tested, but carrying one load-bearing bug
`Page`, `DiskManager`, `FreelistPage`, `SlottedPage` all exist with tests (`tests/page.cpp`, `tests/disk_manager.cpp`, `tests/slotted_page.cpp`). `DiskManager` does real `pread`/`pwrite`, has a free-list, and a page-0 `GlobalMetadata` header. This is solid, production-shaped code for a from-scratch project.

**The bug you flagged is real and is the single highest-leverage fix in the whole codebase.** `SlottedPage::insertRecord()` (`storage/slotted_page.cpp`) always inserts at `find_first_free_slot_id()` — i.e., insertion order — and never at a key-sorted position. Every downstream consumer that assumes slot position tracks key order is currently broken or downgraded to linear scan, and it's called out in your own comments in at least three places in `index/b_plus_tree.h`/`.cpp`.

**Diagnosis worth internalizing before you fix it:** stable slot IDs are a real, standard invariant — but only for *heap-organized* tables, where an external structure (a secondary index, or another page) stores `(page_id, slot_id)` as a durable row pointer that must survive future inserts/deletes on that page. Postgres calls this the "line pointer" / `ItemId` array, and it's exactly why heap tuples never renumber. **Your B+Tree is an index-organized table (IOT)** — the leaf *is* the row, nothing outside the page ever stores "slot 5 of page P" as a handle, and internal-node "slots" are pure routing (key, child-pointer) pairs that get rewritten on every split/merge anyway. So you don't actually need the indirection-array design you were about to reach for. You can just make `insertRecord` take an ordering key (or a target logical position) and `memmove` the `Slot[]` array itself into place — that's a shift of 4-byte structs, not the record heap, so it's cheap even at hundreds of slots per page. That single change (a) makes physical slot order == key order, (b) makes binary search in `findRecord` valid again, and (c) makes `BPlusTreeIterator::Next()`'s "slot_id_++ is ascending" assumption true. This removes the need for the separate "logical order array" you sketched in the `findRecord` comment — simpler than what you were planning, not more complex.

### B+Tree (`index/b_plus_tree.cpp`) — roughly 55% done, zero test coverage
- `extractKey` — done.
- `findRecord` — implemented, but currently a **linear scan** on every internal and leaf page (not binary search) as a direct consequence of the slot-ordering bug. Untested.
- `get` — implemented, untested.
- `insert` — implemented for the non-split path only. The split path calls `splitChild`, which is incomplete.
- `splitChild` — splits a **leaf's** records into a new right sibling, but the loop that's supposed to insert the new separator key + child pointer into the **parent** internal node is empty (comments only, no code). There is no `splitInternal` at all — if an internal node itself overflows during propagation, nothing handles it. Recursion up a multi-level tree is unimplemented.
- `remove` — deletes the leaf record but never checks for underflow.
- `merge`, `redistribute` — both are stub functions (params silenced with `(void)`, no bodies). Underflow after delete is entirely unhandled.
- `BPlusTreeIterator::Next()` — implemented, but its own doc comment states it isn't sound yet, for the same ordering reason as `findRecord`.
- **Tests: none.** There is no `tests/b_plus_tree.cpp`, no `index_logic` entry in the gtest targets in `CMakeLists.txt`. This is the biggest hidden cost — everything above needs a test harness built essentially from scratch before you can trust any of it.

### Buffer Pool Manager — does not exist
No file, no class. Today, every `BPlusTree` operation does `new char[PAGE_SIZE]` + `disk_manager_->readPage()` + `delete[]`, i.e., a real syscall on every single node touch, no caching, no pinning, no eviction policy. This is fine for correctness-first development but must be replaced before anything resembling "systems" work is demonstrable.

### WAL / durability / concurrency — does not exist
No log manager, no log record format, no mutex anywhere in the codebase (`grep -r mutex` turns up nothing). `SlottedPageHeader` already has an `lsn_t lsn` field reserved, so the intent was there, but nothing writes to it.

### Query Executor — does not exist
No `query/`, `executor/`, or `parser` directory. `main.cpp` is a 7-line stub that only includes `page.h`.

---

## 2. Hour estimates

These are **focused, heads-down coding hours** — not calendar time. Your own pace so far (type system + storage + partial B+Tree, ~43 commits over 3.5 months around a full-time internship) suggests something like 5–12 active hours/week is realistic for you right now; I've translated the totals into calendar time on that basis at the end.

| # | Component | Est. hours | Notes |
|---|---|---|---|
| 1 | Fix slot-ordering invariant in `SlottedPage` | **8–14** | Ordered `insertRecord` (shift `Slot[]`, not the heap), update/extend `tests/slotted_page.cpp` for ordering, confirm `compactify()` interaction is unaffected (it already sorts by offset, not slot index, so it should be fine) |
| 2 | Finish B+Tree | **22–32** | Binary search in `findRecord` (now valid once #1 lands), fill in `splitChild`'s parent-insertion loop, implement `splitInternal` + recursive split propagation up multiple levels, implement `merge`/`redistribute` for delete-underflow, fix/simplify `BPlusTreeIterator::Next()`, and — the big one — write `tests/b_plus_tree.cpp` from scratch (insert/get/remove/scan, single-page and multi-level split/merge cases, iterator range scans) |
| 3 | Buffer Pool Manager | **20–28** | Page table + frame array, a replacement policy (plain LRU or clock is enough — LRU-K is a stretch goal, not a requirement), pin/unpin + dirty-flag bookkeeping, `FetchPage`/`NewPage`/`UnpinPage`/`FlushPage`, then the less-obvious cost: **refactoring every `disk_manager_->readPage(...)` call site in `b_plus_tree.cpp` to pin/unpin through the buffer pool instead of raw `new char[PAGE_SIZE]`** |
| 4 | WAL + mutex | **20–30** | Scope this as *durable redo logging*, not full ARIES: log record format (insert/delete/update/commit), append-only in-memory buffer + flush, enforce the WAL invariant (flush log before the dirty page it describes hits disk), a single coarse-grained mutex around the buffer pool/log manager (per-page latches are a stretch goal, not required for the signal you want), and a redo pass on startup. Full ARIES-style undo+checkpointing recovery would add another **15–20 hrs** — explicitly call it out as a stretch goal, don't budget it into your critical path |
| 5 | Query Executor | **35–50** | See scoping discussion below — this is the one where "how much" matters more than "how long" |

**Critical-path total (components 1–4 + right-sized executor): ~105–155 hours.**
At 5–12 focused hrs/week around your internship, that's roughly **9–20 weeks** (~2–4.5 months) of calendar time — i.e., finishable this year, but not this summer, unless you can clear more hours/week.

---

## 3. Scoping the Query Executor for QD interview signal

You said this is aimed at solidifying **systems-level credibility for quant-dev interviews at tier-2 firms**. That goal should directly drive scope, because a query executor can absorb unbounded time if you chase completeness instead of signal.

**What actually gets asked about / matters in that interview context:** whether you understand the iterator/Volcano execution model, can reason about algorithmic complexity of joins and scans, and can talk concretely about how a query turns into a physical plan. **What does not move the needle much:** a hand-rolled SQL grammar/parser. Parsers are a large, well-trodden time sink that mostly tests string-wrangling, not systems understanding — and it's easy to burn 15+ hours on grammar edge cases that an interviewer will never ask about.

**Recommendation: skip the SQL text parser. Build a programmatic plan-tree API instead** — i.e., you construct a plan by composing executor nodes in C++ directly (a small builder/fluent API), not by parsing a query string. This still forces you to build and be able to explain every piece an interviewer actually cares about:

- A shared `Executor` base class (`Init()` / `Next() -> optional<Tuple>`) — the Volcano model itself.
- `SeqScanExecutor` / `IndexScanExecutor`, wrapping `BPlusTree::scan()`/`get()` — ties directly into the iterator you already half-built.
- `FilterExecutor` — predicate evaluation reusing `Type::Compare` on `Value`s, so no new expression engine needed.
- `ProjectionExecutor`.
- `NestedLoopJoinExecutor` (equality join) — enough to talk intelligently about join complexity; a hash join is a good, cheap stretch add once nested-loop works (a `std::unordered_map` keyed on the join column, maybe +6–8 hrs) and is worth it if you have slack, since "why hash join over nested loop" is a very on-brand systems question.
- `AggregationExecutor` — COUNT/SUM/MIN/MAX, optionally GROUP BY via a hash map.
- A tiny demo/driver in `main.cpp` that builds a plan by hand (e.g., scan → filter → project → aggregate over a synthetic table) and prints results — this is your "it actually runs" artifact for an interview conversation or a README GIF.

This scope (~35–50 hrs, in the table above) gives you a complete, demonstrable, defensible executor. A SQL-subset front end (`SELECT col FROM t WHERE col = x`) is a legitimate stretch goal on top if you have time left — budget **+10–15 hrs** for a small recursive-descent parser and binder — but treat it as optional polish, not part of "finished."

---

## 4. Suggested order

1. Slot ordering fix (unblocks everything downstream; also the most interesting bug to be able to narrate in an interview — "here's an invariant I had backwards and how I diagnosed it").
2. Finish B+Tree + its test suite (don't skip this — an untested B+Tree is not a credible artifact, and the merge/redistribute logic is exactly the kind of thing that gets probed in a systems interview).
3. Buffer Pool Manager (this is what turns "a B+Tree" into "a database" — it's also usually the single most recognizable component to anyone who's read a DB internals book or taken 15-445).
4. WAL + mutex, scoped to redo-only durability + a coarse lock, explicitly deferring full ARIES recovery.
5. Query Executor, scoped to the programmatic plan-tree API above, with the SQL subset as optional stretch.

**Total to a genuinely finished, demonstrable state: ~105–155 focused hours**, plus 10–35 more if you pursue the two explicitly-called-out stretch goals (full ARIES recovery, SQL parser).

---

## Appendix A — FIX_SLOT_ID: files, required changes, and the Postgres precedent

### The bug, precisely

`SlottedPage::insertRecord()` in `storage/slotted_page.cpp` always writes the new slot at `find_first_free_slot_id()` — the first tombstoned slot, or `max_slot_id` if none exists. That position is a function of **insertion/deletion history**, never of the record's key. Nothing in `SlottedPage` looks at record contents to decide where in the `Slot[]` array an entry goes — and by its own doc comment, `SlottedPage` is deliberately not supposed to (`"It does not interpret record contents — records are opaque byte spans"`). So the fix has to live at the boundary between `SlottedPage` (which owns the array) and `BPlusTree` (which is the only thing that knows what a key is).

### Files involved

**`storage/slotted_page.h` / `storage/slotted_page.cpp` — the array-shift itself**
- Add a position-taking entry point, e.g. `insertRecordAt(slot_id_t logical_pos, const char* record, uint16_t length)`, alongside (or replacing) `insertRecord`. `SlottedPage` still never inspects the record — the caller supplies *where*, not *why*.
- Internally this needs a `memmove` of the `Slot[]` array: shift every entry at index `>= logical_pos` up by one (`sizeof(Slot)` = 4 bytes each), then write the new `{offset, size}` at `logical_pos`, then `max_slot_id++`. This is cheap — it's moving 4-byte structs, not the record heap — but it's a new code path; nothing like it exists today.
- `find_first_free_slot_id()` stops being "where do I insert" and becomes irrelevant to insertion; it was only ever answering "insertion order," which is exactly the thing being removed. Tombstone reuse (not growing `max_slot_id` when a dead slot exists) is now in tension with shifting: a dead slot in the middle of the array can no longer be silently reused by an unrelated key without also shifting past it. Simplest correct option — and the one Postgres's own index pages take (see below) — is to drop slot-reuse-on-insert entirely and let dead slots get physically removed from the array during compaction/delete instead. This is a real behavior change worth deciding deliberately, not a side effect to discover later.
- `compactify()` itself is very likely **unaffected** — it already sorts live slots by `offset` (physical heap position) to repack the record heap, not by slot index, so it doesn't currently assume anything about key order. Worth a quick confirming read when you touch this file, not a rewrite.

**`index/b_plus_tree.h` / `index/b_plus_tree.cpp` — the caller-side search that decides "where"**
- `findRecord()`: once slot order == key order is guaranteed, the linear scans over internal-node slots (the `for (slot_id_t slot = 0; slot < n; slot += 2)` loop) and the leaf-node scan can both become real binary searches (`std::lower_bound`-style over slot index, comparing via `extractKey`). This is the payoff for doing the `SlottedPage` fix at all — right now these loops are linear specifically because the sort invariant doesn't hold.
- `insert()`: the call `leaf.insertRecord((const char*)record, len)` needs to become "run the same kind of search `findRecord` does to get a logical position, then call `leaf.insertRecordAt(pos, record, len)`." This is a second, smaller binary search (or the position can likely be derived from the same descent `findRecord` already did — worth checking before writing a redundant search).
- `splitChild()`: the (currently empty) loop meant to insert the new separator key + child pointer into the parent internal node has the exact same requirement — the new (key, child_ptr) pair must land at its sorted position among the parent's existing keys, not be appended.
- `BPlusTreeIterator::Next()`: its doc comment already says its "slot_id_++ visits keys in ascending order" assumption doesn't hold today. Once the `SlottedPage` fix lands, that assumption becomes actually true, and the current defensive logic can likely be simplified rather than just trusted.

**`tests/slotted_page.cpp`** — needs new cases: insert keys out of order and assert the resulting `Slot[]` array position matches sorted order; assert previously-returned `slot_id`s for *other* entries shift correctly when a new entry is inserted before them; assert interaction with tombstoned slots.

### A design point that falls out of this, worth deciding now rather than later
Because this project has no secondary indexes yet, nothing anywhere stores `(page_id, slot_id)` as a durable external pointer — so slot_ids are free to shift on insert **and** delete with zero correctness impact today. If you ever add a secondary index later (something pointing at a primary-tree leaf slot from outside), *that* structure would reintroduce the need for stable slot identity — but only for the pages it points into, and that's a bridge to cross later, not a reason to keep the indirection-array design now.

### How Postgres actually solves this

Postgres's on-disk page format is the same slotted-page shape this project uses — `PageHeaderData`, then an array of 4-byte `ItemIdData` "line pointers" (`lp_off`, `lp_len`, plus 2 flag bits) growing forward, tuple bytes growing backward from the end of the page (`src/include/storage/bufpage.h`, `src/include/storage/itemid.h`). The line-pointer array is structurally identical to this project's `Slot[]` array. But Postgres runs **two different policies over the same structure**, depending on what the page is for:

- **Heap pages** (`heap_insert` → `PageAddItemExtended` in `src/backend/storage/page/bufpage.c`): a new tuple is placed at the first `LP_UNUSED` line pointer, or appended — insertion order, no sort requirement, **exactly** what this project's current `insertRecord`/`find_first_free_slot_id` does. This is correct behavior *for a heap page*, because heap pages are never binary-searched by key — they're reached via sequential scan or via an index handing back an exact `(block, offset)` TID. Line-pointer stability matters here specifically because secondary indexes store that TID as a durable pointer into the heap page (`ItemPointerData`), and it must keep working after later inserts/deletes/vacuums on that page.
- **B-tree index pages** (`src/backend/access/nbtree/nbtinsert.c`, `_bt_insertonpg` → `PageAddItem` with an explicit `OffsetNumber` computed by `_bt_binsrch`): items are kept in sorted key order within the page precisely so `_bt_binsrch` can binary search it, and inserting into the middle of that order **does** shift every later line pointer up by one, via `memmove` over the `ItemId` array — the same operation recommended above for `SlottedPage::insertRecordAt`. Deletes (`PageIndexTupleDelete`) shift the array back down and remove the slot outright, rather than tombstoning it. None of this breaks anything, because nothing outside a B-tree index page ever refers to "item number K of this specific page" as a persistent handle — every access re-descends from the root by key comparison.

The mapping to this project: your B+Tree leaves hold full rows (index-organized table), which makes every one of your pages — leaf and internal alike — behave like Postgres's **nbtree** pages, never like its heap pages. `find_first_free_slot_id`-style insertion is Postgres's heap-page policy applied to a page that's actually playing the role of an index page — that's the precise shape of the bug, and `_bt_insertonpg`'s sorted-insert-with-shift is the fix, transplanted directly.

---

## Appendix 1: In-Place Sorted Shift — In Depth

This is a deep dive on refactoring direction (1) from the `slotted_page.h` design-directions comment: the minimal fix that makes `insertRecord` place records at their sorted position instead of appending.

### Blast radius — what actually breaks

The blast radius depends entirely on *how* the change is introduced.

**If `insertRecordAt` is added as a new method and `insertRecord` is left untouched, nothing outside `index/b_plus_tree.cpp` needs to change.** Every current caller of the affected `SlottedPage` methods, and whether it's affected:

| Caller | Method used | Breaks? |
|---|---|---|
| `tests/slotted_page.cpp` (5 tests) | `insertRecord` | No — untouched, still append-only |
| `tests/storage_viz.cpp` | `insertRecord`, `deleteRecord` | No — same reason |
| `index/b_plus_tree.cpp::insert()` (~line 201) | `leaf.insertRecord(...)` | **Yes — must switch to the ordered call** |
| `index/b_plus_tree.cpp::splitChild()` (~line 325) | `new_page.insertRecord(...)` | **No, and it shouldn't** |

`splitChild`'s call site is the interesting non-obvious case: `new_page` is freshly initialized (empty), and the loop feeding it walks `child_page`'s slots in ascending *physical* order. Once `insert()` is fixed to place records at their sorted position, `child_page`'s physical order already equals key order — so appending those records in that same order into an empty page *is* a sorted insert, with zero shifting needed. This is refactoring direction (5) — the bulk/append-sorted fast path — falling out for free, not something that needs separate implementation.

**What does NOT stay this clean:** the "shift on delete too" half of direction (1) as originally sketched in the `slotted_page.h` comment (`"Same idea applies to delete: shift the array down instead of tombstoning"`). Adopting that breaks `tests/slotted_page.cpp`'s `CompactionTest`:

- The test inserts `s1, s2, s3` at physical slots `0, 1, 2`, deletes `s2` (slot 1), then calls `page->getRecord(s3.value())` — using the *original* index, `2`.
- Under shift-on-delete, deleting slot 1 moves the record at slot 2 down to slot 1, and `max_slot_id` drops to 2. Querying slot 2 now hits `GetSlot`'s bounds check (`slot_id >= max_slot_id`) and returns empty — the test's `EXPECT_EQ(std::string(res3.first, res3.second), r3)` fails.
- The root cause: the test caches a slot handle (`s3.value()`) across a mutation (the delete) of the same page — precisely the pattern the new invariant ("slot rank is not stable across a mutation of the same page") forbids.

**Recommendation: split direction (1) into two independent sub-changes and adopt only the first for now:**
- **(1a) Ordered insert.** `insertRecordAt` shifts on insert; `deleteRecord` stays exactly as it is today (tombstone, no shift). Zero test breakage, and this alone is sufficient to fix `findRecord`'s binary search and `BPlusTreeIterator::Next()`'s ordering assumption.
- **(1b) Shift on delete.** Optional, deferred. Breaks `CompactionTest` as shown above (the test would need rewriting to re-fetch slot handles after each mutation instead of caching them). Only worth doing if something separately motivates removing tombstones from the array.

**A subtlety that (1a) alone does not resolve:** a *pure* binary search over the slot array requires no interior gaps. Under (1a), a tombstoned slot (`offset == 0`) still occupies its array index — its key can't be extracted from it (offset `0` doesn't point at a real record), so a naive `std::lower_bound`-style binary search would need to explicitly detect and step around dead slots rather than compare against them directly. This is still fine and still effectively O(log n) in the common case, just not a textbook-vanilla binary search. Worth knowing before implementing `findRecord`'s replacement, not a blocker to doing so.

### An independent bug found while tracing this: `FindRecordMetadata.leaf_slot` is never set

While confirming how `BPlusTree` would compute the sorted insertion position, tracing `findRecord()`'s leaf-level search in `index/b_plus_tree.cpp` turned up a pre-existing, unrelated bug at the point where the result is packaged for the caller:

```cpp
slot_id_t l = best_slot;
ret.leaf_page = current_page_id;
ret.leaf_page = l;              // bug: should be ret.leaf_slot = l;
ret.bt_stack = std::move(bt_stack);
```

`ret.leaf_page` is assigned twice; `FindRecordMetadata::leaf_slot` is never actually written, so it's stuck at its zero-initialized default from `FindRecordMetadata ret{};`. `get()` and `remove()` currently operate on `leaf_slot == 0` unconditionally, regardless of where the target key actually lives in the leaf — this is a real, currently-live correctness bug, independent of the ordering issue, and it needs fixing before `leaf_slot` can be reused as an insertion position (see below).

### How BPlusTree computes the sorted position

The key realization: no new search logic is needed. `findRecord()`'s existing leaf-level walk already computes "the first live slot whose key is `>= target`" — the textbook definition of `lower_bound` — which is simultaneously the *lookup* answer `get()`/`remove()` need and the correct *insertion index* `insert()` needs for a new key. It's only unusable today because (a) it's a linear scan, valid only once slot order tracks key order, and (b) the typo above discards it. Fix both, and the same value serves both purposes.

**Binary search, once slot order is trustworthy:**

```cpp
// binary search over a key-ordered page — the return value doubles as
// both "where is target" (search) and "where should target go" (insert)
slot_id_t BPlusTree::lowerBound(const SlottedPage& page, Key target) const {
    slot_id_t lo = 0, hi = page.getSlotCount();
    while (lo < hi) {
        slot_id_t mid = lo + (hi - lo) / 2;
        auto [bytes, len] = page.getRecord(mid);
        Key key = extractKey((const uint8_t*)bytes, len);
        if (key.Compare(target) < 0) lo = mid + 1;
        else hi = mid;
    }
    return lo;
}
```

**`SlottedPage` gains an additive method — `insertRecord` itself is untouched:**

```cpp
// storage/slotted_page.h — new declaration alongside the existing insertRecord()
std::optional<slot_id_t> insertRecordAt(slot_id_t logical_pos, const char* record, uint16_t length);
```

```cpp
// storage/slotted_page.cpp
std::optional<slot_id_t> SlottedPage::insertRecordAt(slot_id_t logical_pos, const char* record, uint16_t length) {
    if (length == 0) return std::nullopt;
    SlottedPageHeader* header = GetHeader();
    if (logical_pos > header->max_slot_id) return std::nullopt;

    uint16_t space_needed = length + sizeof(Slot);
    if (space_needed > getTotalFreeSpace()) return std::nullopt;
    if (!canInsertContigious(length, /*needs_new_slot=*/true)) compactify();

    header->free_space_ptr -= length;
    uint16_t new_offset = header->free_space_ptr;
    std::memcpy(data_ + new_offset, record, length);

    // open a gap at logical_pos by shifting everything after it up by one Slot
    uint16_t num_to_shift = header->max_slot_id - logical_pos;
    char* shift_start = data_ + sizeof(SlottedPageHeader) + sizeof(Slot) * logical_pos;
    std::memmove(shift_start + sizeof(Slot), shift_start, num_to_shift * sizeof(Slot));
    header->max_slot_id++;

    Slot* new_slot = GetSlot(logical_pos).value();
    new_slot->offset = new_offset;
    new_slot->size = length;
    return logical_pos;
}
```

**`BPlusTree::insert()` changes exactly one call site, reusing `findRecord`'s output instead of computing anything new:**

```cpp
FindRecordMetadata meta = findRecord(key);   // meta.leaf_slot is lower_bound(key), once the typo above is fixed
// ...
leaf.insertRecordAt(meta.leaf_slot, (const char*)record, len);   // was: leaf.insertRecord(record, len)
```

### Summary of required changes for (1a)

1. Fix the `ret.leaf_page = l;` → `ret.leaf_slot = l;` typo in `findRecord()` (`index/b_plus_tree.cpp`).
2. Replace `findRecord`'s linear scans (internal-node and leaf-node) with the `lowerBound`-style binary search above, now that it's valid.
3. Add `insertRecordAt` to `SlottedPage` as a new, additive method (`storage/slotted_page.h` / `.cpp`); leave `insertRecord` exactly as-is.
4. Change `BPlusTree::insert()`'s single call site from `leaf.insertRecord(...)` to `leaf.insertRecordAt(meta.leaf_slot, ...)`.
5. Leave `deleteRecord`, `splitChild`, and every existing test untouched — none of them require changes for (1a).



PLAN:
fix slot-id + finish B+Tree 
    by aug 1 
resume + linkedin work + application framework (which companies to target)
    aug 1st-3rd
start applying by aug3rd
    Roles: QD/SWE/other
    Terms: Spring co-op 2027 / Summer internship 2027