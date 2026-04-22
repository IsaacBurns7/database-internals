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
