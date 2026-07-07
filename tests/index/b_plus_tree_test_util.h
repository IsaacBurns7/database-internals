#pragma once

// Shared fixture + friend accessor for BPlusTree tests, split across:
//   - tests/index/b_plus_tree.cpp          (extractKey, findRecord)
//   - tests/index/b_plus_tree_splits.cpp   (splitChild, splitInternal,
//                                            splitRoot, insertIntoParent)
// One accessor struct instead of one per file: BPlusTree only declares
// `friend struct BPlusTreeTestAccess;` once, and both TUs including this
// header get an identical (ODR-safe) definition of it.

#include <gtest/gtest.h>

#include <cstdio>
#include <cstring>
#include <vector>

#include "index/b_plus_tree.h"
#include "storage/disk_manager.h"
#include "storage/slotted_page.h"
#include "type/column.h"
#include "type/schema.h"

namespace {

// Leaf record layout used throughout: two NUMERIC(width 8) columns, "id" at
// offset 0 and "value" at offset 8 — id doubles as the key column.
auto MakeRecord(int64_t id, int64_t value) -> std::vector<uint8_t> {
    std::vector<uint8_t> buf(16);
    std::memcpy(buf.data(), &id, sizeof(id));
    std::memcpy(buf.data() + 8, &value, sizeof(value));
    return buf;
}

}  // namespace

// Sole gateway into BPlusTree's private members — see the friend declaration
// on BPlusTree for why a single accessor struct is used instead of
// FRIEND_TEST per test case.
struct BPlusTreeTestAccess {
    static auto ExtractKey(const BPlusTree& tree, const uint8_t* record, uint16_t len) -> Key {
        return tree.extractKey(record, len);
    }
    static auto FindRecord(BPlusTree& tree, Key target) -> FindRecordMetadata {
        return tree.findRecord(target);
    }
    static auto SplitChild(BPlusTree& tree, BTStack bt_stack, Key child) -> page_id_t {
        return tree.splitChild(bt_stack, child);
    }
    static auto SplitInternal(BPlusTree& tree, BTStack bt_stack, Key internal) -> page_id_t {
        return tree.splitInternal(bt_stack, internal);
    }
    static auto SplitRoot(BPlusTree& tree, page_id_t left_child_id, Key separator_key,
                          page_id_t right_child_id) -> page_id_t {
        return tree.splitRoot(left_child_id, separator_key, right_child_id);
    }
    static auto InsertIntoParent(BPlusTree& tree, BTStack bt_stack, Key separator_key,
                                 page_id_t new_right_page_id) -> void {
        tree.insertIntoParent(bt_stack, separator_key, new_right_page_id);
    }
    static auto GetRootPageId(const BPlusTree& tree) -> page_id_t {
        return tree.root_page_id;
    }
};

class BPlusTreeTest : public ::testing::Test {
protected:
    const std::string db_name = "b_plus_tree_test.db";

    void SetUp() override {
        std::remove(db_name.c_str());
        dm_ = new DiskManager(db_name);
        schema_ = new Schema({
            Column("id", TypeId::NUMERIC, 8, 0),
            Column("value", TypeId::NUMERIC, 8, 8),
        });
    }

    void TearDown() override {
        delete dm_;
        delete schema_;
        std::remove(db_name.c_str());
    }

    // Builds a BPlusTree over root_page_id using this fixture's DiskManager/schema.
    auto MakeTree(page_id_t root_page_id, uint32_t key_col_idx = 0) -> BPlusTree {
        return BPlusTree(dm_, root_page_id, schema_, key_col_idx);
    }

    // Allocates a fresh leaf page and populates it with (id, value) records in
    // ascending id order (append == sorted position on an empty page).
    auto BuildLeafPage(const std::vector<std::pair<int64_t, int64_t>>& records) -> page_id_t {
        page_id_t page_id = dm_->allocatePage();
        std::vector<char> buf(PAGE_SIZE, 0);
        SlottedPage page(buf.data());
        page.init(page_id, SlottedPageType::LEAF_PAGE);
        for (const auto& [id, value] : records) {
            auto rec = MakeRecord(id, value);
            page.insertRecord(reinterpret_cast<const char*>(rec.data()), static_cast<uint16_t>(rec.size()));
        }
        dm_->writePage(page_id, buf.data());
        return page_id;
    }

    // Allocates a fresh internal page laid out as (key, child_page_id) pairs
    // at slots (2j, 2j+1) — same layout findRecord()'s internal search expects.
    auto BuildInternalPage(const std::vector<std::pair<int64_t, page_id_t>>& entries) -> page_id_t {
        page_id_t page_id = dm_->allocatePage();
        std::vector<char> buf(PAGE_SIZE, 0);
        SlottedPage page(buf.data());
        page.init(page_id, SlottedPageType::INTERNAL_PAGE);
        for (const auto& [key, child] : entries) {
            page.insertRecord(reinterpret_cast<const char*>(&key), sizeof(key));
            page.insertRecord(reinterpret_cast<const char*>(&child), sizeof(child));
        }
        dm_->writePage(page_id, buf.data());
        return page_id;
    }

    static auto MakeKey(const int64_t& value) -> Key {
        return Key::FromBytes(TypeId::NUMERIC, 8, reinterpret_cast<const uint8_t*>(&value), 8);
    }

    // Reads page_id's current on-disk contents into a fresh buffer, for
    // post-mutation inspection via SlottedPage.
    auto ReadPage(page_id_t page_id) -> std::vector<char> {
        std::vector<char> buf(PAGE_SIZE, 0);
        dm_->readPage(page_id, buf.data());
        return buf;
    }

    // Internal-node slots hold a raw key with no leaf/tuple framing —
    // decodes the NUMERIC(width 8) value directly instead of going through
    // extractKey (which assumes column offsets that only apply to leaf
    // records / key_col_idx_ == 0 — see extractKey()'s doc comment).
    static auto ReadInternalKey(const SlottedPage& page, slot_id_t slot) -> int64_t {
        auto [bytes, len] = page.getRecord(slot);
        int64_t value = 0;
        std::memcpy(&value, bytes, sizeof(value));
        return value;
    }

    static auto ReadChildPageId(const SlottedPage& page, slot_id_t slot) -> page_id_t {
        auto [bytes, len] = page.getRecord(slot);
        page_id_t value = 0;
        std::memcpy(&value, bytes, sizeof(value));
        return value;
    }

    DiskManager* dm_;
    Schema* schema_;
};
