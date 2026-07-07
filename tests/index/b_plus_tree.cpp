#include "b_plus_tree_test_util.h"

// ============================================================================
// extractKey
// ============================================================================

TEST_F(BPlusTreeTest, ExtractKeyReadsColumnZeroAtOffsetZero) {
    BPlusTree tree = MakeTree(/*root_page_id=*/0, /*key_col_idx=*/0);
    auto rec = MakeRecord(42, 99);

    Key key = BPlusTreeTestAccess::ExtractKey(tree, rec.data(), static_cast<uint16_t>(rec.size()));

    EXPECT_EQ(key.GetSize(), 8);
    EXPECT_EQ(key.ToValue().val.integer, 42);
}

TEST_F(BPlusTreeTest, ExtractKeyReadsNonZeroColumnOffset) {
    BPlusTree tree = MakeTree(/*root_page_id=*/0, /*key_col_idx=*/1);  // "value" column, offset 8
    auto rec = MakeRecord(42, 99);

    Key key = BPlusTreeTestAccess::ExtractKey(tree, rec.data(), static_cast<uint16_t>(rec.size()));

    EXPECT_EQ(key.GetSize(), 8);
    EXPECT_EQ(key.ToValue().val.integer, 99);
}

TEST_F(BPlusTreeTest, ExtractKeyOnNullRecordReturnsEmptyKey) {
    BPlusTree tree = MakeTree(/*root_page_id=*/0);

    Key key = BPlusTreeTestAccess::ExtractKey(tree, nullptr, 0);

    // Don't call ToValue()/Compare() here: extractKey's nullptr/len==0 branch
    // builds this Key over a static "" literal with size 0, but Key::ToValue()
    // always deserializes key_width_ (8) bytes regardless of size_ — reading
    // past a 1-byte literal. GetSize() alone is safe to assert on.
    EXPECT_EQ(key.GetSize(), 0);
}

TEST_F(BPlusTreeTest, ExtractKeyOnZeroLengthRecordReturnsEmptyKey) {
    BPlusTree tree = MakeTree(/*root_page_id=*/0);
    auto rec = MakeRecord(1, 2);

    Key key = BPlusTreeTestAccess::ExtractKey(tree, rec.data(), 0);

    EXPECT_EQ(key.GetSize(), 0);
}

TEST_F(BPlusTreeTest, ExtractKeyClampsWhenFewerThanKeyWidthBytesRemain) {
    // key_col_idx_=1 -> offset 8; only supply a 10-byte record, so 2 bytes
    // remain after the offset instead of the full 8-byte key width.
    BPlusTree tree = MakeTree(/*root_page_id=*/0, /*key_col_idx=*/1);
    std::vector<uint8_t> rec(10, 0);

    Key key = BPlusTreeTestAccess::ExtractKey(tree, rec.data(), static_cast<uint16_t>(rec.size()));

    EXPECT_EQ(key.GetSize(), 2);
    EXPECT_EQ(key.GetData(), rec.data() + 8);
}

TEST_F(BPlusTreeTest, ExtractKeyClampsOffsetWhenRecordShorterThanOffset) {
    // key_col_idx_=1 -> offset 8, but the record is only 5 bytes long: the
    // offset itself gets clamped to len, leaving zero remaining bytes.
    BPlusTree tree = MakeTree(/*root_page_id=*/0, /*key_col_idx=*/1);
    std::vector<uint8_t> rec(5, 0);

    Key key = BPlusTreeTestAccess::ExtractKey(tree, rec.data(), static_cast<uint16_t>(rec.size()));

    EXPECT_EQ(key.GetSize(), 0);
}

// ============================================================================
// findRecord — single leaf (no internal levels)
// ============================================================================

TEST_F(BPlusTreeTest, FindRecordExactMatchInSingleLeaf) {
    page_id_t leaf = BuildLeafPage({{10, 100}, {20, 200}, {30, 300}});
    BPlusTree tree = MakeTree(leaf);

    int64_t target = 20;
    FindRecordMetadata md = BPlusTreeTestAccess::FindRecord(tree, MakeKey(target));

    EXPECT_EQ(md.leaf_page, leaf);
    EXPECT_EQ(md.leaf_slot, 1);
    EXPECT_TRUE(md.bt_stack.empty());
}

TEST_F(BPlusTreeTest, FindRecordBetweenKeysReturnsLowerBoundInSingleLeaf) {
    page_id_t leaf = BuildLeafPage({{10, 100}, {20, 200}, {30, 300}});
    BPlusTree tree = MakeTree(leaf);

    int64_t target = 15;
    FindRecordMetadata md = BPlusTreeTestAccess::FindRecord(tree, MakeKey(target));

    EXPECT_EQ(md.leaf_page, leaf);
    EXPECT_EQ(md.leaf_slot, 1);  // first live slot with key >= 15 is 20 at slot 1
}

TEST_F(BPlusTreeTest, FindRecordLessThanAllReturnsSlotZero) {
    page_id_t leaf = BuildLeafPage({{10, 100}, {20, 200}, {30, 300}});
    BPlusTree tree = MakeTree(leaf);

    int64_t target = 5;
    FindRecordMetadata md = BPlusTreeTestAccess::FindRecord(tree, MakeKey(target));

    EXPECT_EQ(md.leaf_slot, 0);
}

TEST_F(BPlusTreeTest, FindRecordGreaterThanAllReturnsSlotCount) {
    page_id_t leaf = BuildLeafPage({{10, 100}, {20, 200}, {30, 300}});
    BPlusTree tree = MakeTree(leaf);

    int64_t target = 100;
    FindRecordMetadata md = BPlusTreeTestAccess::FindRecord(tree, MakeKey(target));

    EXPECT_EQ(md.leaf_slot, 3);  // no live slot >= target -> insertion point at end
}

TEST_F(BPlusTreeTest, FindRecordSkipsTombstoneDuringLeafBinarySearch) {
    page_id_t leaf = BuildLeafPage({{10, 100}, {20, 200}, {30, 300}});

    // Delete the middle record without compacting, leaving a tombstone at
    // slot 1 that the binary search must scan past. Live keys after this are
    // {10 at slot 0, 30 at slot 2} — slot 1 no longer has a key at all, so
    // every target's answer must be one of those two live slots, never the
    // tombstone itself.
    std::vector<char> buf(PAGE_SIZE, 0);
    dm_->readPage(leaf, buf.data());
    SlottedPage page(buf.data());
    ASSERT_TRUE(page.deleteRecord(1));
    dm_->writePage(leaf, buf.data());

    BPlusTree tree = MakeTree(leaf);

    int64_t exact_low = 10, between = 15, between_high = 25, exact_high = 30;
    EXPECT_EQ(BPlusTreeTestAccess::FindRecord(tree, MakeKey(exact_low)).leaf_slot, 0);
    // Both 15 and 25 fall strictly between the two surviving live keys (10
    // and 30), so lower_bound over the live keys alone lands on 30 at slot 2
    // for either target — the tombstone at slot 1 is never a valid answer.
    EXPECT_EQ(BPlusTreeTestAccess::FindRecord(tree, MakeKey(between)).leaf_slot, 2);
    EXPECT_EQ(BPlusTreeTestAccess::FindRecord(tree, MakeKey(between_high)).leaf_slot, 2);
    // Regression case for the bug this fixture caught: a live exact match
    // sitting immediately after a tombstone run, right at the binary
    // search's current window boundary, must still be found rather than
    // discarded in favor of the tombstone (see the `best` tracker in
    // findRecord()'s leaf loop).
    EXPECT_EQ(BPlusTreeTestAccess::FindRecord(tree, MakeKey(exact_high)).leaf_slot, 2);
}

// ============================================================================
// findRecord — one internal level over two leaves
// ============================================================================
//
// Layout (strict min-key, per the KEY PROMOTION comment in b_plus_tree.cpp):
//   root: [key=10 -> leaf_left] [key=40 -> leaf_right]
//   leaf_left:  10, 20, 30
//   leaf_right: 40, 50, 60

class BPlusTreeMultiLevelTest : public BPlusTreeTest {
protected:
    void SetUp() override {
        BPlusTreeTest::SetUp();
        leaf_left_ = BuildLeafPage({{10, 100}, {20, 200}, {30, 300}});
        leaf_right_ = BuildLeafPage({{40, 400}, {50, 500}, {60, 600}});
        root_ = BuildInternalPage({{10, leaf_left_}, {40, leaf_right_}});
    }

    page_id_t leaf_left_;
    page_id_t leaf_right_;
    page_id_t root_;
};

TEST_F(BPlusTreeMultiLevelTest, RoutesToLeftLeafOnExactBoundaryMatch) {
    BPlusTree tree = MakeTree(root_);

    int64_t target = 10;
    FindRecordMetadata md = BPlusTreeTestAccess::FindRecord(tree, MakeKey(target));

    EXPECT_EQ(md.leaf_page, leaf_left_);
    EXPECT_EQ(md.leaf_slot, 0);
    ASSERT_EQ(md.bt_stack.size(), 1u);
    EXPECT_EQ(md.bt_stack[0].page_id, root_);
    EXPECT_EQ(md.bt_stack[0].child_slot, 1);  // slot 1 holds leaf_left_'s child pointer
}

TEST_F(BPlusTreeMultiLevelTest, RoutesToRightLeafOnExactBoundaryMatch) {
    BPlusTree tree = MakeTree(root_);

    int64_t target = 40;
    FindRecordMetadata md = BPlusTreeTestAccess::FindRecord(tree, MakeKey(target));

    EXPECT_EQ(md.leaf_page, leaf_right_);
    EXPECT_EQ(md.leaf_slot, 0);
    ASSERT_EQ(md.bt_stack.size(), 1u);
    EXPECT_EQ(md.bt_stack[0].child_slot, 3);  // slot 3 holds leaf_right_'s child pointer
}

TEST_F(BPlusTreeMultiLevelTest, RoutesToLeftLeafWhenBelowFirstKey) {
    BPlusTree tree = MakeTree(root_);

    int64_t target = 5;
    FindRecordMetadata md = BPlusTreeTestAccess::FindRecord(tree, MakeKey(target));

    EXPECT_EQ(md.leaf_page, leaf_left_);
    EXPECT_EQ(md.leaf_slot, 0);
}

TEST_F(BPlusTreeMultiLevelTest, RoutesToRightLeafWhenAboveAllKeys) {
    BPlusTree tree = MakeTree(root_);

    int64_t target = 100;
    FindRecordMetadata md = BPlusTreeTestAccess::FindRecord(tree, MakeKey(target));

    EXPECT_EQ(md.leaf_page, leaf_right_);  // falls back to rightmost child
    EXPECT_EQ(md.leaf_slot, 3);            // greater than every key in leaf_right_ too
}

// Regression test for a fixed bug: for a target strictly between two
// separator keys with no exact match (10 < 25 < 40), strict min-key says the
// correct child is the one whose key is <= target, i.e. leaf_left_ (key 10).
// findRecord()'s internal-node search used to binary-search a plain
// lower_bound (first key >= target) and descend through THAT key's child
// even on a non-exact match, landing on leaf_right_ (key 40) instead — see
// BPlusTree::findRecord()'s internal-node binary search, now an upper_bound
// with a floor (lo - 1) descent instead.
TEST_F(BPlusTreeMultiLevelTest, RoutesToLeftLeafWhenStrictlyBetweenKeys) {
    BPlusTree tree = MakeTree(root_);

    int64_t target = 25;
    FindRecordMetadata md = BPlusTreeTestAccess::FindRecord(tree, MakeKey(target));

    EXPECT_EQ(md.leaf_page, leaf_left_);
}
