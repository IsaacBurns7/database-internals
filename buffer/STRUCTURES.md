# C++ STL Cheat Sheet

Quick reference for the containers/algorithms that come up constantly in matching-engine-style
problems (ordered price levels, O(1) cancel-by-id, FIFO time priority). Complexities noted
because interviewers will ask.

---

## `std::map<K, V>` — ordered, red-black tree

Keeps keys sorted. This is what gives you price-time priority for free.

```cpp
std::map<int, Level> bids;

bids[price];                          // O(log n) — inserts default V if absent, else returns ref
bids.insert({price, Level{}});        // O(log n) — no-op if key exists
bids.emplace(price, Level{});         // O(log n) — constructs in place

auto it = bids.find(price);           // O(log n) — end() if missing
bids.count(price);                    // O(log n) — 0 or 1
bids.at(price);                       // O(log n) — throws if missing

bids.erase(price);                    // O(log n) — by key, returns size_t count removed (0 or 1)
bids.erase(it);                       // O(1) amortized — by iterator, returns iterator to the NEXT element
                                       // (or end()) — only the erased iterator itself is invalidated

bids.begin();  bids.end();            // smallest key ... one-past-largest
bids.rbegin(); bids.rend();           // largest key ... one-before-smallest
bids.empty();  bids.size();

bids.lower_bound(price);              // iterator to first element with key >= price (end() if none)
bids.upper_bound(price);              // iterator to first element with key >  price (end() if none)
bids.equal_range(price);              // {lower_bound(price), upper_bound(price)} — a pair of iterators
```

**`lower_bound` / `upper_bound` — exactly what they return:** an *iterator*, never a value or
bool. `lower_bound(k)` lands on the first element **≥ k** — if `k` itself isn't a key in the
map, you still get an iterator, just pointing at the next key up. `upper_bound(k)` lands one
step further: the first element **> k** (skips past an element equal to `k`). Both return
`end()` if nothing qualifies.

Existence check:
```cpp
auto it = bids.lower_bound(k);
bool exists = (it != bids.end() && it->first == k);
```

The free-function versions (`std::lower_bound(first, last, val)` from `<algorithm>`) return
the same *kind* of thing — an iterator into `[first, last)` — but require the range already
sorted and walk it in O(log n) only if the iterators are random-access (vector/array); on a
`list` they silently degrade to O(n). Always prefer `map`'s own member functions over the free
ones when you have a map.

**Iterator invalidation:** erasing/inserting a node invalidates *only* iterators/references to
that node. Every other iterator (including `end()`) stays valid. This is why you can hold a
`map<int,Level>::iterator` inside a side-table for O(1) lookup-then-erase.

**Best price lookup:**
- Ascending map, want *lowest* key (best ask) → `m.begin()`
- Ascending map, want *highest* key (best bid) → `std::prev(m.end())` or `m.rbegin()`
- Reverse iterator `rbegin()` and `std::prev(end())` point at the same element, but
  `rbegin()` is a `reverse_iterator` — convert with `.base()` if you need a real iterator
  (its `.base()` points one *past* the element it dereferences to).

**Custom order:** `std::map<int, V, std::greater<int>>` flips ascending → descending, so
`begin()` is always the "best" side without branching — a common trick to avoid writing the
match loop twice for bids vs. asks.

---

## `std::unordered_map<K, V>` — hash table

O(1) average lookup/insert/erase, no ordering. Use for id → order-location indexes.

```cpp
std::unordered_map<int, Handle> live;

live[id] = Handle{...};               // O(1) avg — insert or overwrite
live.find(id);                        // O(1) avg — end() if missing
live.erase(id);                       // O(1) avg
live.count(id);                       // O(1) avg
```

**Invalidation:** erase invalidates only the erased element's iterators/references. Insert can
invalidate iterators (rehash) but never invalidates references/pointers to existing elements
(unless that element itself is erased).

---

## `std::list<T>` — doubly linked list

The only standard sequence container where insert/erase/splice don't invalidate other
iterators. This is what makes FIFO time-priority queues at a price level cheap.

```cpp
std::list<Order> orders;

orders.push_back(x);   orders.push_front(x);   // O(1)
orders.pop_back();     orders.pop_front();     // O(1)
orders.front();        orders.back();          // O(1)

auto it = orders.insert(pos, x);      // O(1) — insert before pos, returns iterator to new elem
orders.erase(it);                     // O(1) — returns iterator to next element; only `it` invalidated

orders.splice(pos, other_list, it);   // O(1) — move element between lists, no copy, no invalidation
```

**Why this matters here:** `rest()` stores `std::prev(lv.orders.end())` as a stable handle for
O(1) cancel — that iterator stays valid until that specific order is erased, even as other
orders are pushed/popped around it. A `std::vector` can't give you this guarantee.

---

## `std::vector<T>` — dynamic array

```cpp
v.push_back(x);            // O(1) amortized
v.reserve(n);               // O(n) — pre-allocate, avoids realloc churn
v[i];  v.at(i);              // O(1) — at() bounds-checked
v.size();  v.empty();
v.erase(v.begin() + i);     // O(n) — shifts everything after i
```

**Invalidation:** any reallocation (from `push_back` exceeding capacity) invalidates *all*
iterators/pointers/references. Even without reallocation, `insert`/`erase` invalidate
everything from that point onward. Bad choice for anything you hold long-lived handles into.

---

## Iterator helpers (`<iterator>`)

```cpp
std::next(it);       std::next(it, n);      // O(1) for random-access, O(n) for list/map otherwise... 
                                             // actually O(n) steps for bidirectional (list, map)
std::prev(it);        std::prev(it, n);
std::advance(it, n);  // in-place version of next/prev
std::distance(a, b);  // O(1) random-access, O(n) otherwise
```

Note: for `std::map`/`std::list` (bidirectional, not random-access), `next`/`prev`/`advance`
walk one node at a time — O(n) for n > 1, but O(1) for the common `std::prev(end())` /
`std::next(begin())` single-step case.

---

## Erasing while iterating

`erase(it)` invalidates `it` — you can't touch it again once the call returns. Two ways to
keep going:

```cpp
// Idiomatic (C++11+): erase() itself returns the next-element iterator.
for (auto it = m.begin(); it != m.end(); ) {
    if (should_remove(it)) it = m.erase(it);   // it now points at the next element (or end())
    else ++it;
}

// Equivalent, older style — save next() before erasing, since erasing `it`
// doesn't touch any other iterator, so a next computed beforehand is still valid.
for (auto it = m.begin(); it != m.end(); ) {
    auto tmp = std::next(it);
    if (should_remove(it)) m.erase(it);
    it = tmp;
}
```

Both are correct and both rely on the same fact: erasing one element doesn't invalidate
iterators to *other* elements (true for `map`, `set`, `unordered_map`, `list`; **false** for
`vector`/`deque`, where erasing shifts everything after it and invalidates the whole tail).
Prefer the first form — one fewer variable, and it's what `erase(it)`'s return value is *for*.

This project's `unlink()` doesn't need either pattern — it erases a single order by id via the
`live` handle, not while mid-iteration over the book.

---

## `std::priority_queue<T>` — binary heap

Top of the alternatives list PROBLEM.md asks you to defend against `std::map`. Good for "give
me the max" but **no way to erase or decrease-key an arbitrary element** without lazy deletion
(push a tombstone, check-and-skip on pop) — which is why it loses to `std::map` when you need
O(log n) cancel-by-id.

```cpp
std::priority_queue<int> pq;                       // max-heap by default
std::priority_queue<int, std::vector<int>, std::greater<int>> minpq;

pq.push(x);   // O(log n)
pq.pop();     // O(log n) — removes top, no return value
pq.top();     // O(1)
```

---

## `std::pair` / `std::tuple`

```cpp
std::pair<int, int> p = {a, b};
p.first; p.second;

auto [a, b] = p;                       // structured binding (C++17)

std::tuple<int, char, int> t{1, 'x', 2};
auto [id, side, qty] = t;
std::get<0>(t);
```

---

## `std::string`

```cpp
s += "text";  s += c;  s += std::to_string(42);   // amortized O(1) append
s.reserve(n);                                       // pre-size to avoid realloc, like vector
std::to_string(42);                                 // int/long/etc -> string
```

**I/O perf tip:** build one big `std::string` buffer and `fwrite` it once at the end (as this
project's `Engine::out` does) instead of `std::cout << ... << std::endl` per line — `endl`
flushes, and per-line stream ops are the classic reason a correct solution times out.

---

## Algorithms (`<algorithm>`)

```cpp
std::min(a, b);  std::max(a, b);       // O(1)
std::sort(v.begin(), v.end());         // O(n log n) — random-access iterators only (vector, not list/map)
std::lower_bound(first, last, val);    // O(log n) on sorted random-access range; O(n) on list
std::binary_search(first, last, val);  // O(log n) on sorted random-access range
```

`std::map` has its own `lower_bound`/`upper_bound` members (O(log n), tree-based) — prefer
those over the free-function versions, which assume random-access iterators.

---

## Picking a container: the questions PROBLEM.md actually asks

| Need | Structure | Why |
|---|---|---|
| Price levels, sorted, insert/erase/best-price all O(log n) | `std::map<int, Level>` | Balanced tree, ordered iteration, stable iterators on erase-elsewhere |
| id → order location, O(1) cancel | `std::unordered_map<int, Handle>` | Hash lookup, `Handle` holds a `list::iterator` so erase is O(1) once found |
| FIFO within a price level, O(1) push/pop/erase-by-handle | `std::list<Order>` | Only sequence container with erase-stability for untouched elements |
| "Just give me the max" with no need to cancel | `std::priority_queue` | Simpler, but no arbitrary erase — wrong tool once cancel/amend exist |
