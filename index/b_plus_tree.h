class BPlusTree{
	//uses sibling pointers + strict min-key 
		//will implement latch crabbing for concurrency
	//database overall is IoT (Index Organized Tables) 
		//therefore caller must know Primary Index to Insert? 
		//... 
		//maybe we could have a RecordType parameter for the BPlusTree class, and then also define the primary index as part of the recordtype 
			//then they just insert via a RecordType (which is also the schema of the aforementioned table) 

	bool insert(RecordType record);
		//RecordType is a parameter of the class (DELETE COMMENT WHEN IMPLEMENTED) 
	bool remove(RecordType record); //could also just be keytype 
	std::optional<RecordType> get(RecordType record); //could also be keytype 
    // range scan — returns all values where key is in [start, end]
    std::vector<RecordType> scan(KeyType start, KeyType end); //could also be recordtype 
private:
	splitChild(page_id_t parent_node, KeyType child); //could also be index 
		//take child, split into two. remember to add/update key stuff to parent node (strict min-key) 
	merge(page_id_t parent_node, KeyType left_child); //could also be index, could also input right child
		//take nodes left_child and left_child+1=right_child, and put keys into left_child. destroy right_child
		//remember to delete right_child key, shouldn't affect left_child key(strict min-key) 
	redistribute(page_id_t parent_node, KeyType child); //could also be index 
		//take stuff in child, give to siblings (sibling pointers!)
		//remember to update parent keys (strict min-key)
	DiskManager* disk_manager_;
	//root page id is 0
};
