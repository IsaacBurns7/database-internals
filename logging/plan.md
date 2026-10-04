# Classes to add
## First 

### LogRecord (and record-type definitions)
The unit of the log.
Behavior
Common header: LSN, prevLSN (per-transaction chain), transaction id, record type, length, and a checksum so a torn tail can be detected at restart.
Types: UPDATE (with redo and undo payload), CLR (redo payload plus undoNextLSN), COMMIT, ABORT, END, BEGIN_CHECKPOINT, END_CHECKPOINT (carrying transaction table and dirty page table snapshots).
A record-type tag that identifies which handler (page type or access method) interprets the payload. The header is understood by the log; the payload is not.
Serialized with the existing WriteBuffer/ReadBuffer little-endian infrastructure.
Needs from other classes
Serialization utilities.
Nothing from the buffer pool or access methods.

### LogManager
Storage-level infrastructure for the log file. Knows record headers, never record semantics.
Behavior
Appends records to an in-memory log buffer and assigns monotonically increasing LSNs (byte offsets work well).
Tracks flushedLSN, the highest LSN known durable.
Makes the log durable up to a requested LSN and signals completion only after the fsync returns. Batches concurrent requests (group commit).
Provides forward and backward iteration over durable records, used by recovery and by runtime abort.
At startup, finds the end of the valid log by scanning until a record fails its checksum, and truncates the torn tail.
Treats fsync failure as fatal: never retries, because a retry can falsely report success.
Needs from other classes
DiskScheduler (or a dedicated log I/O thread) for sequential writes and fsync.
LogRecord serialization.
Used by
BufferPoolManager (flushedLSN, flush-until), TransactionManager (commit force), MiniTxn (append), RecoveryManager and Checkpointer (iteration, append).

## AFTER TransactionManager, IN ORDER

### MasterRecord (This is very short...) 
A small fixed-location structure pointing to the last complete checkpoint.
Behavior
Updated atomically after a checkpoint is durable, for example by a write-then-rename or two alternating slots with checksums.
Needs from other classes
DiskScheduler or a direct file path.

### RecoveryManager
Drives restart recovery. Owns control flow; does not interpret record payloads.
Behavior
Reads the master record, loads the last complete checkpoint, and runs analysis to rebuild the transaction table and dirty page table and identify losers.
Redo: scans forward from the minimum recLSN; skips records whose page is not in the dirty page table or whose LSN is below the page’s recLSN; otherwise fetches the page and reapplies only if pageLSN is less than the record’s LSN. Does not log during redo. Dispatches to page-local redo handlers and must not traverse the tree, because the tree may be structurally inconsistent mid-redo.
Undo: processes all losers in a single backward pass ordered by LSN. Each undo emits a CLR; each CLR encountered redirects via undoNextLSN. Writes END when a loser is finished. Undo may traverse the tree, because redo has restored its structure.
Is restartable at any point: a crash during recovery is handled by the same procedure on the next start.
Needs from other classes
LogManager (iteration, append).
BufferPoolManager (fetch pages, seed dirty page table).

### Handler registry (redo and undo dispatch).
TransactionManager (shared undo routine and transaction table structure).
Handler registry
Maps each record type to the code that understands it, keeping recovery independent of page formats.
Behavior
Redo handlers are registered by page types (leaf, internal, metadata pages) and operate on a single page given the record.
Undo handlers are registered by access methods (the B+ tree) and operate logically, for example deleting a key wherever it now lives.
Needs from other classes
Registrations from page-type and access-method code at startup.

### Crash-injection harness
The resume milestone’s verification mechanism.
Behavior
Injects crashes at log append, log fsync completion, page write submission and completion, checkpoint steps, and during recovery itself.
Simulates loss of unflushed state by discarding the log buffer and all buffer pool frames.
After recovery, checks that the database equals the result of applying exactly the committed transactions, and that recovering twice gives the same result.
Needs from other classes
Hook points in DiskScheduler and LogManager; a reference model of committed state.

### Checkpointer
Takes fuzzy checkpoints.
Behavior
Appends BEGIN_CHECKPOINT, collects transaction table and dirty page table snapshots, appends END_CHECKPOINT with them, makes it durable, then updates the master record.
Does not force data pages or quiesce transactions.
Provides the point below which the log may be truncated, which is the minimum of the oldest recLSN and the oldest active transaction’s first LSN.
Needs from other classes
TransactionManager (transaction table snapshot).
BufferPoolManager (dirty page table snapshot).
LogManager (append, force).
Master record storage.



