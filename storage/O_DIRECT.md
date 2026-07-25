# O_DIRECT vs pread/pwrite — design note

## The short answer

`pread`/`pwrite` and `O_DIRECT` are not alternatives — they're orthogonal.

- `pread`/`pwrite` are the *syscalls* used for positioned I/O (no shared file
  offset, safe for concurrent readers). DiskManager uses these regardless of
  caching strategy.
- `O_DIRECT` is a *flag on the open fd* that tells the kernel to bypass the
  page cache for that file. It's a decision made once, at `open()` time.

Since `DiskManager` owns `fd_`, it's the only layer that can decide this —
if O_DIRECT is ever added, it belongs in `DiskManager`'s constructor, not
`BufferPoolManager`. `BufferPoolManager` never touches file descriptors; it
only calls `diskManager->readPage()` / `writePage()`.

## Current decision: buffered I/O, no O_DIRECT

`DiskManager` currently opens the file with plain `O_RDWR | O_CREAT` and uses
buffered `pread`/`pwrite`. Reasons to keep it this way through Phase 2:

1. **Double-caching is the actual justification for O_DIRECT, and there's no
   second cache yet.** The classic argument for O_DIRECT is "don't cache the
   same page twice — once in the OS page cache, once in my own buffer pool."
   Until `BufferPoolManager` exists, there's only one cache (the OS's), so
   bypassing it buys nothing and only adds complexity.

2. **macOS has no O_DIRECT.** This project builds on Darwin. Linux's
   `O_DIRECT` doesn't exist there — the equivalent is
   `fcntl(fd, F_NOCACHE, 1)`, which has weaker guarantees (still does some
   caching, less strict alignment enforcement). Supporting both would mean
   `#ifdef __linux__` / `#ifdef __APPLE__` branches in `DiskManager`.

3. **Alignment requirements are invasive.** O_DIRECT (Linux) requires the
   buffer address, file offset, and transfer length to all be aligned to the
   device's logical block size (typically 512 or 4096 bytes). That means:
   - `Page::data_` (`char data_[PAGE_SIZE]`) can't just be a plain array
     member anymore — it would need `posix_memalign`/`aligned_alloc`.
   - Stack buffers used in `DiskManager::allocatePage()` /
     `deallocatePage()` for `Freelist_Page` and `GlobalMetadata` reads would
     also need to be aligned.
   - This ripples into code that currently works fine unaligned.

4. **No free readahead/writeback.** Buffered I/O gets sequential readahead
   and write coalescing from the kernel for free — useful during scans and
   freelist writes. O_DIRECT means you're responsible for that yourself (or
   you eat the latency on every page fetch).

## When to revisit

Once `BufferPoolManager` exists and is doing real caching + eviction, adding
O_DIRECT (or `F_NOCACHE` on macOS) to `DiskManager`'s `open()` call becomes a
meaningful optimization — at that point the OS page cache really is
redundant work. That's the point to pay the alignment-plumbing cost, not
before.
