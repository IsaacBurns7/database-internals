has access to:
    Key — compare, copy, free, serialize, deserialize. That's its entire interface with the type system.
    - uses uint8_t* for reading and writing rows 
    DiskManager - read, write
    - uint8_t* buf = disk.read(node.page_id);  // borrow for this scope
      // ... do work ...
      disk.write(node.page_id, buf);           // or however your disk manager works
      // buf is dead after this point (because it will be evicted by the buffer pool manager)
class BPlusTree {
public:
    explicit BPlusTree(DiskManager* disk_manager);

    void insert(KeyType key, ValueType value);
    bool remove(KeyType key);
    std::optional<ValueType> get(KeyType key);

    // range scan — returns all values where key is in [start, end]
    std::vector<ValueType> scan(KeyType start, KeyType end);

private:
    page_id_t root_page_id_;
    DiskManager* disk_manager_;

    // internal helpers
    page_id_t findLeaf(KeyType key);
    void splitChild(page_id_t parent_id, int child_index);
    void mergeOrRedistribute(page_id_t node_id);  // called on underflow after delete
}
