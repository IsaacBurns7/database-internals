struct BPlusNode {
    page_id_t page_id;      // permanent identity
    // NOT: uint8_t* buf   ← don't store this
};
uint8_t* buf = disk.read(node.page_id);  // borrow for this scope
// ... do work ...
disk.write(node.page_id, buf);           // or however your disk manager works
// buf is dead after this point (because it will be evicted by the buffer pool manager)
