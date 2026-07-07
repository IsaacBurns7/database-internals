#include "b_plus_tree_test_util.h"

// Tests for BPlusTree's split machinery: splitChild (leaf split), splitInternal
// (internal-node split), splitRoot (root replacement), and insertIntoParent
// (the shared "wire the new sibling into its parent, splitting that parent
// first if needed" tail both split paths call into). These are all private —
// reached only through BPlusTreeTestAccess (see b_plus_tree_test_util.h).

class BPlusTreeSplitTest : public BPlusTreeTest {
protected:
    // Fills a fresh internal page with ascending (key=i*10, child=1000+i)
    // pairs until one more pair would no longer fit — i.e. until
    // getFreeSpace() drops below a single pair's cost (key + child + 2
    // slots). Returns the page id and the pairs that made it in, so callers
    // can derive split math (pivot index, etc.) from entries.size() instead
    // of hardcoding a page-capacity-dependent count.
    auto BuildFullInternalPage() -> std::pair<page_id_t, std::vector<std::pair<int64_t, page_id_t>>> {
        page_id_t page_id = dm_->allocatePage();
        std::vector<char> buf(PAGE_SIZE, 0);
        SlottedPage page(buf.data());
        page.init(page_id, SlottedPageType::INTERNAL_PAGE);

        std::vector<std::pair<int64_t, page_id_t>> entries;
        int64_t key = 0;
        page_id_t child = 1000;
        const uint16_t pair_cost = sizeof(int64_t) + sizeof(page_id_t) + 2 * sizeof(Slot);
        while (page.getFreeSpace() >= pair_cost) {
            page.insertRecord(reinterpret_cast<const char*>(&key), sizeof(key));
            page.insertRecord(reinterpret_cast<const char*>(&child), sizeof(child));
            entries.push_back({key, child});
            key += 10;
            child += 1;
        }
        dm_->writePage(page_id, buf.data());
        return {page_id, entries};
    }
};

// ============================================================================
// splitRoot
// ============================================================================

TEST_F(BPlusTreeSplitTest, SplitRootBuildsTwoChildInternalRootOverLeafLeftChild) {
    page_id_t left_leaf = BuildLeafPage({{10, 100}, {20, 200}, {30, 300}});
    page_id_t right_leaf = BuildLeafPage({{100, 1000}});
    BPlusTree tree = MakeTree(left_leaf);

    page_id_t new_root_id = BPlusTreeTestAccess::SplitRoot(tree, left_leaf, MakeKey(100), right_leaf);

    EXPECT_EQ(BPlusTreeTestAccess::GetRootPageId(tree), new_root_id);

    auto buf = ReadPage(new_root_id);
    SlottedPage root(buf.data());
    EXPECT_EQ(root.getPageType(), SlottedPageType::INTERNAL_PAGE);
    ASSERT_EQ(root.getSlotCount(), 4);
    EXPECT_EQ(ReadInternalKey(root, 0), 10);  // left_leaf's own min key, not the separator
    EXPECT_EQ(ReadChildPageId(root, 1), left_leaf);
    EXPECT_EQ(ReadInternalKey(root, 2), 100);
    EXPECT_EQ(ReadChildPageId(root, 3), right_leaf);
}

TEST_F(BPlusTreeSplitTest, SplitRootDerivesLeftMinKeyFromInternalLeftChildsOwnSlotZero) {
    page_id_t left_internal = BuildInternalPage({{5, 900}, {50, 901}});
    page_id_t right_internal = BuildInternalPage({{200, 902}});
    BPlusTree tree = MakeTree(left_internal);

    page_id_t new_root_id = BPlusTreeTestAccess::SplitRoot(tree, left_internal, MakeKey(100), right_internal);

    auto buf = ReadPage(new_root_id);
    SlottedPage root(buf.data());
    ASSERT_EQ(root.getSlotCount(), 4);
    // No walk down to a leaf needed: left_internal's own slot 0 key (5) IS
    // the subtree's min under strict min-key, by induction.
    EXPECT_EQ(ReadInternalKey(root, 0), 5);
    EXPECT_EQ(ReadChildPageId(root, 1), left_internal);
    EXPECT_EQ(ReadInternalKey(root, 2), 100);
    EXPECT_EQ(ReadChildPageId(root, 3), right_internal);
}

// ============================================================================
// splitChild — leaf split
// ============================================================================

TEST_F(BPlusTreeSplitTest, SplitChildAtRootHalvesRecordsAndBuildsNewRoot) {
    page_id_t leaf = BuildLeafPage({{10, 100}, {20, 200}, {30, 300}, {40, 400}, {50, 500}, {60, 600}});
    BPlusTree tree = MakeTree(leaf);  // leaf IS the root -> bt_stack is empty

    page_id_t fresh = BPlusTreeTestAccess::SplitChild(tree, /*bt_stack=*/{}, MakeKey(10));

    auto left_buf = ReadPage(leaf);
    SlottedPage left(left_buf.data());
    ASSERT_EQ(left.getSlotCount(), 3);
    EXPECT_EQ(ReadInternalKey(left, 0), 10);
    EXPECT_EQ(ReadInternalKey(left, 1), 20);
    EXPECT_EQ(ReadInternalKey(left, 2), 30);
    EXPECT_EQ(left.getRightSibling(), fresh);

    auto right_buf = ReadPage(fresh);
    SlottedPage right(right_buf.data());
    ASSERT_EQ(right.getSlotCount(), 3);
    EXPECT_EQ(ReadInternalKey(right, 0), 40);
    EXPECT_EQ(ReadInternalKey(right, 1), 50);
    EXPECT_EQ(ReadInternalKey(right, 2), 60);
    EXPECT_EQ(right.getLeftSibling(), leaf);
    EXPECT_EQ(right.getRightSibling(), static_cast<page_id_t>(INVALID_PAGE_ID));

    page_id_t new_root = BPlusTreeTestAccess::GetRootPageId(tree);
    EXPECT_NE(new_root, leaf);
    auto root_buf = ReadPage(new_root);
    SlottedPage root(root_buf.data());
    ASSERT_EQ(root.getSlotCount(), 4);
    EXPECT_EQ(ReadInternalKey(root, 0), 10);
    EXPECT_EQ(ReadChildPageId(root, 1), leaf);
    EXPECT_EQ(ReadInternalKey(root, 2), 40);  // pivot key, copied up (leaf split)
    EXPECT_EQ(ReadChildPageId(root, 3), fresh);
}

// slot_count/2 (the initial pivot guess) lands on a slot that was tombstoned
// before the split — splitChild must walk forward to the next live slot
// rather than moving a dead record.
TEST_F(BPlusTreeSplitTest, SplitChildWalksPivotForwardPastTombstoneAtMidpoint) {
    page_id_t leaf = BuildLeafPage({{10, 100}, {20, 200}, {30, 300}, {40, 400}, {50, 500}, {60, 600}});
    {
        auto buf = ReadPage(leaf);
        SlottedPage page(buf.data());
        ASSERT_TRUE(page.deleteRecord(3));  // tombstone key 40, exactly at slot_count/2
        dm_->writePage(leaf, buf.data());
    }
    BPlusTree tree = MakeTree(leaf);

    page_id_t fresh = BPlusTreeTestAccess::SplitChild(tree, {}, MakeKey(10));

    auto left_buf = ReadPage(leaf);
    SlottedPage left(left_buf.data());
    // Tombstone at slot 3 falls before the walked-forward pivot (4), so it's
    // dropped by child_page.compactify() along with the live records before it.
    ASSERT_EQ(left.getSlotCount(), 3);
    EXPECT_EQ(ReadInternalKey(left, 0), 10);
    EXPECT_EQ(ReadInternalKey(left, 1), 20);
    EXPECT_EQ(ReadInternalKey(left, 2), 30);

    auto right_buf = ReadPage(fresh);
    SlottedPage right(right_buf.data());
    ASSERT_EQ(right.getSlotCount(), 2);
    EXPECT_EQ(ReadInternalKey(right, 0), 50);
    EXPECT_EQ(ReadInternalKey(right, 1), 60);
}

TEST_F(BPlusTreeSplitTest, SplitChildRelinksExistingRightNeighborsLeftSibling) {
    page_id_t left_neighbor = BuildLeafPage({{1, 10}});
    page_id_t splitting = BuildLeafPage({{10, 100}, {20, 200}, {30, 300}, {40, 400}});
    page_id_t right_neighbor = BuildLeafPage({{100, 1000}});
    {
        auto buf = ReadPage(left_neighbor);
        SlottedPage p(buf.data());
        p.setRightSibling(splitting);
        dm_->writePage(left_neighbor, buf.data());
    }
    {
        auto buf = ReadPage(splitting);
        SlottedPage p(buf.data());
        p.setLeftSibling(left_neighbor);
        p.setRightSibling(right_neighbor);
        dm_->writePage(splitting, buf.data());
    }
    {
        auto buf = ReadPage(right_neighbor);
        SlottedPage p(buf.data());
        p.setLeftSibling(splitting);
        dm_->writePage(right_neighbor, buf.data());
    }
    BPlusTree tree = MakeTree(splitting);

    page_id_t fresh = BPlusTreeTestAccess::SplitChild(tree, {}, MakeKey(10));

    auto splitting_buf = ReadPage(splitting);
    SlottedPage splitting_page(splitting_buf.data());
    EXPECT_EQ(splitting_page.getLeftSibling(), left_neighbor);  // untouched
    EXPECT_EQ(splitting_page.getRightSibling(), fresh);

    auto fresh_buf = ReadPage(fresh);
    SlottedPage fresh_page(fresh_buf.data());
    EXPECT_EQ(fresh_page.getLeftSibling(), splitting);
    EXPECT_EQ(fresh_page.getRightSibling(), right_neighbor);

    auto right_buf = ReadPage(right_neighbor);
    SlottedPage right_page(right_buf.data());
    EXPECT_EQ(right_page.getLeftSibling(), fresh);  // re-pointed away from `splitting`
}

TEST_F(BPlusTreeSplitTest, SplitChildWithNonEmptyBtStackInsertsSeparatorIntoExistingParent) {
    page_id_t leaf_a = BuildLeafPage({{1, 10}, {2, 20}});
    page_id_t leaf_b = BuildLeafPage(
        {{100, 1000}, {110, 1100}, {120, 1200}, {130, 1300}, {140, 1400}, {150, 1500}});
    page_id_t root = BuildInternalPage({{1, leaf_a}, {100, leaf_b}});
    BPlusTree tree = MakeTree(root);

    FindRecordMetadata md = BPlusTreeTestAccess::FindRecord(tree, MakeKey(100));
    ASSERT_EQ(md.leaf_page, leaf_b);
    ASSERT_EQ(md.bt_stack.size(), 1u);

    page_id_t fresh = BPlusTreeTestAccess::SplitChild(tree, md.bt_stack, MakeKey(100));

    auto leaf_b_buf = ReadPage(leaf_b);
    SlottedPage leaf_b_page(leaf_b_buf.data());
    ASSERT_EQ(leaf_b_page.getSlotCount(), 3);
    EXPECT_EQ(ReadInternalKey(leaf_b_page, 0), 100);
    EXPECT_EQ(ReadInternalKey(leaf_b_page, 2), 120);
    EXPECT_EQ(leaf_b_page.getRightSibling(), fresh);

    auto fresh_buf = ReadPage(fresh);
    SlottedPage fresh_page(fresh_buf.data());
    ASSERT_EQ(fresh_page.getSlotCount(), 3);
    EXPECT_EQ(ReadInternalKey(fresh_page, 0), 130);
    EXPECT_EQ(fresh_page.getLeftSibling(), leaf_b);

    // root had room -> no new root; insertIntoParent wired the new pair
    // straight into it instead.
    EXPECT_EQ(BPlusTreeTestAccess::GetRootPageId(tree), root);
    auto root_buf = ReadPage(root);
    SlottedPage root_page(root_buf.data());
    ASSERT_EQ(root_page.getSlotCount(), 6);
    EXPECT_EQ(ReadInternalKey(root_page, 0), 1);
    EXPECT_EQ(ReadChildPageId(root_page, 1), leaf_a);
    EXPECT_EQ(ReadInternalKey(root_page, 2), 100);
    EXPECT_EQ(ReadChildPageId(root_page, 3), leaf_b);
    EXPECT_EQ(ReadInternalKey(root_page, 4), 130);
    EXPECT_EQ(ReadChildPageId(root_page, 5), fresh);
}

// ============================================================================
// splitInternal
// ============================================================================

TEST_F(BPlusTreeSplitTest, SplitInternalAtRootBuildsNewRootOverTwoInternalNodes) {
    page_id_t mid = BuildInternalPage({{0, 900}, {10, 901}, {20, 902}, {30, 903}, {40, 904}, {50, 905}});
    BPlusTree tree = MakeTree(mid);
    BTStack bt_stack{{mid, /*child_slot unused by splitInternal*/ 0}};

    page_id_t fresh = BPlusTreeTestAccess::SplitInternal(tree, bt_stack, MakeKey(999));

    auto mid_buf = ReadPage(mid);
    SlottedPage mid_page(mid_buf.data());
    ASSERT_EQ(mid_page.getSlotCount(), 6);  // 3 pairs retained
    EXPECT_EQ(ReadInternalKey(mid_page, 0), 0);
    EXPECT_EQ(ReadChildPageId(mid_page, 1), 900);
    EXPECT_EQ(ReadInternalKey(mid_page, 4), 20);
    EXPECT_EQ(ReadChildPageId(mid_page, 5), 902);

    auto fresh_buf = ReadPage(fresh);
    SlottedPage fresh_page(fresh_buf.data());
    ASSERT_EQ(fresh_page.getSlotCount(), 6);  // 3 pairs moved
    EXPECT_EQ(ReadInternalKey(fresh_page, 0), 30);
    EXPECT_EQ(ReadChildPageId(fresh_page, 1), 903);
    EXPECT_EQ(ReadInternalKey(fresh_page, 4), 50);
    EXPECT_EQ(ReadChildPageId(fresh_page, 5), 905);

    page_id_t new_root = BPlusTreeTestAccess::GetRootPageId(tree);
    EXPECT_NE(new_root, mid);
    auto root_buf = ReadPage(new_root);
    SlottedPage root_page(root_buf.data());
    ASSERT_EQ(root_page.getSlotCount(), 4);
    EXPECT_EQ(ReadInternalKey(root_page, 0), 0);  // mid's own min key
    EXPECT_EQ(ReadChildPageId(root_page, 1), mid);
    EXPECT_EQ(ReadInternalKey(root_page, 2), 30);  // separator, copied up AND kept in fresh
    EXPECT_EQ(ReadChildPageId(root_page, 3), fresh);
}

TEST_F(BPlusTreeSplitTest, SplitInternalWithGrandparentInsertsSeparatorThereInstead) {
    page_id_t mid = BuildInternalPage({{0, 900}, {10, 901}, {20, 902}, {30, 903}, {40, 904}, {50, 905}});
    page_id_t grandparent = BuildInternalPage({{5, mid}});
    BPlusTree tree = MakeTree(grandparent);
    BTStack bt_stack{{grandparent, 1}, {mid, 0}};

    page_id_t fresh = BPlusTreeTestAccess::SplitInternal(tree, bt_stack, MakeKey(999));

    // grandparent had room -> no new root; splitInternal's own recursive call
    // wired (separator=30, fresh) directly into it via insertIntoParent.
    EXPECT_EQ(BPlusTreeTestAccess::GetRootPageId(tree), grandparent);
    auto gp_buf = ReadPage(grandparent);
    SlottedPage gp_page(gp_buf.data());
    ASSERT_EQ(gp_page.getSlotCount(), 4);
    EXPECT_EQ(ReadInternalKey(gp_page, 0), 5);
    EXPECT_EQ(ReadChildPageId(gp_page, 1), mid);
    EXPECT_EQ(ReadInternalKey(gp_page, 2), 30);
    EXPECT_EQ(ReadChildPageId(gp_page, 3), fresh);

    auto mid_buf = ReadPage(mid);
    SlottedPage mid_page(mid_buf.data());
    EXPECT_EQ(mid_page.getSlotCount(), 6);

    auto fresh_buf = ReadPage(fresh);
    SlottedPage fresh_page(fresh_buf.data());
    ASSERT_EQ(fresh_page.getSlotCount(), 6);
    EXPECT_EQ(ReadInternalKey(fresh_page, 0), 30);
}

// ============================================================================
// insertIntoParent — parent has room (no recursive split)
// ============================================================================

TEST_F(BPlusTreeSplitTest, InsertIntoParentInsertsAtFrontWhenSeparatorIsSmallestSoFar) {
    page_id_t parent = BuildInternalPage({{10, 700}, {30, 701}});
    BPlusTree tree = MakeTree(parent);

    BPlusTreeTestAccess::InsertIntoParent(tree, {{parent, 0}}, MakeKey(5), 800);

    auto buf = ReadPage(parent);
    SlottedPage page(buf.data());
    ASSERT_EQ(page.getSlotCount(), 6);
    EXPECT_EQ(ReadInternalKey(page, 0), 5);
    EXPECT_EQ(ReadChildPageId(page, 1), 800);
    EXPECT_EQ(ReadInternalKey(page, 2), 10);
    EXPECT_EQ(ReadInternalKey(page, 4), 30);
}

TEST_F(BPlusTreeSplitTest, InsertIntoParentInsertsInMiddleWhenSeparatorFallsBetweenExistingKeys) {
    page_id_t parent = BuildInternalPage({{10, 700}, {30, 701}});
    BPlusTree tree = MakeTree(parent);

    BPlusTreeTestAccess::InsertIntoParent(tree, {{parent, 0}}, MakeKey(20), 800);

    auto buf = ReadPage(parent);
    SlottedPage page(buf.data());
    ASSERT_EQ(page.getSlotCount(), 6);
    EXPECT_EQ(ReadInternalKey(page, 0), 10);
    EXPECT_EQ(ReadInternalKey(page, 2), 20);
    EXPECT_EQ(ReadChildPageId(page, 3), 800);
    EXPECT_EQ(ReadInternalKey(page, 4), 30);
}

TEST_F(BPlusTreeSplitTest, InsertIntoParentInsertsAtEndWhenSeparatorIsLargestSoFar) {
    page_id_t parent = BuildInternalPage({{10, 700}, {30, 701}});
    BPlusTree tree = MakeTree(parent);

    BPlusTreeTestAccess::InsertIntoParent(tree, {{parent, 0}}, MakeKey(50), 800);

    auto buf = ReadPage(parent);
    SlottedPage page(buf.data());
    ASSERT_EQ(page.getSlotCount(), 6);
    EXPECT_EQ(ReadInternalKey(page, 4), 50);
    EXPECT_EQ(ReadChildPageId(page, 5), 800);
}

// ============================================================================
// insertIntoParent — parent is full, must split first (and the new pair may
// land in either the original node or the freshly split-off one)
// ============================================================================

TEST_F(BPlusTreeSplitTest, InsertIntoParentSplitsFullParentAndKeepsSeparatorInOriginalNode) {
    auto [full_parent, entries] = BuildFullInternalPage();
    BPlusTree tree = MakeTree(full_parent);
    size_t num_keys = entries.size();
    size_t pivot_key_idx = num_keys / 2;

    // Pick an insertion point strictly inside the left half (the half that
    // stays behind at full_parent's own page id after the split), landing
    // strictly between two consecutive existing keys.
    size_t idx = pivot_key_idx / 2;
    int64_t separator = entries[idx].first + 5;

    BPlusTreeTestAccess::InsertIntoParent(tree, {{full_parent, 0}}, MakeKey(separator), 9999);

    auto left_buf = ReadPage(full_parent);
    SlottedPage left(left_buf.data());
    ASSERT_EQ(left.getSlotCount(), (pivot_key_idx + 1) * 2);
    EXPECT_EQ(ReadInternalKey(left, static_cast<slot_id_t>(idx * 2)), entries[idx].first);
    EXPECT_EQ(ReadInternalKey(left, static_cast<slot_id_t>((idx + 1) * 2)), separator);
    EXPECT_EQ(ReadChildPageId(left, static_cast<slot_id_t>((idx + 1) * 2 + 1)), 9999);
    EXPECT_EQ(ReadInternalKey(left, static_cast<slot_id_t>((idx + 2) * 2)), entries[idx + 1].first);

    page_id_t new_root = BPlusTreeTestAccess::GetRootPageId(tree);
    EXPECT_NE(new_root, full_parent);
    auto root_buf = ReadPage(new_root);
    SlottedPage root_page(root_buf.data());
    ASSERT_EQ(root_page.getSlotCount(), 4);
    EXPECT_EQ(ReadInternalKey(root_page, 0), entries[0].first);
    EXPECT_EQ(ReadChildPageId(root_page, 1), full_parent);
    EXPECT_EQ(ReadInternalKey(root_page, 2), entries[pivot_key_idx].first);
    page_id_t fresh = ReadChildPageId(root_page, 3);

    // The half that didn't receive the new pair is untouched by the insert.
    auto fresh_buf = ReadPage(fresh);
    SlottedPage fresh_page(fresh_buf.data());
    EXPECT_EQ(fresh_page.getSlotCount(), (num_keys - pivot_key_idx) * 2);
    EXPECT_EQ(ReadInternalKey(fresh_page, 0), entries[pivot_key_idx].first);
}

TEST_F(BPlusTreeSplitTest, InsertIntoParentSplitsFullParentAndRedirectsSeparatorToNewNode) {
    auto [full_parent, entries] = BuildFullInternalPage();
    BPlusTree tree = MakeTree(full_parent);
    size_t num_keys = entries.size();
    size_t pivot_key_idx = num_keys / 2;

    // Pick an insertion point strictly inside the right half (the half that
    // becomes the freshly allocated node after the split).
    size_t li = (num_keys - pivot_key_idx) / 2;
    size_t idx = pivot_key_idx + li;
    int64_t separator = entries[idx].first + 5;

    BPlusTreeTestAccess::InsertIntoParent(tree, {{full_parent, 0}}, MakeKey(separator), 9999);

    page_id_t new_root = BPlusTreeTestAccess::GetRootPageId(tree);
    EXPECT_NE(new_root, full_parent);
    auto root_buf = ReadPage(new_root);
    SlottedPage root_page(root_buf.data());
    ASSERT_EQ(root_page.getSlotCount(), 4);
    EXPECT_EQ(ReadChildPageId(root_page, 1), full_parent);
    page_id_t fresh = ReadChildPageId(root_page, 3);
    EXPECT_NE(fresh, full_parent);

    // full_parent (the half that did NOT get the new pair) is untouched.
    auto left_buf = ReadPage(full_parent);
    SlottedPage left(left_buf.data());
    EXPECT_EQ(left.getSlotCount(), pivot_key_idx * 2);

    auto fresh_buf = ReadPage(fresh);
    SlottedPage fresh_page(fresh_buf.data());
    ASSERT_EQ(fresh_page.getSlotCount(), (num_keys - pivot_key_idx + 1) * 2);
    EXPECT_EQ(ReadInternalKey(fresh_page, static_cast<slot_id_t>(li * 2)), entries[idx].first);
    EXPECT_EQ(ReadInternalKey(fresh_page, static_cast<slot_id_t>((li + 1) * 2)), separator);
    EXPECT_EQ(ReadChildPageId(fresh_page, static_cast<slot_id_t>((li + 1) * 2 + 1)), 9999);
    EXPECT_EQ(ReadInternalKey(fresh_page, static_cast<slot_id_t>((li + 2) * 2)), entries[idx + 1].first);
}
