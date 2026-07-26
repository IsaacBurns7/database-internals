#include "buffer/arc_replacer.h"

#include <gtest/gtest.h>

// ============================================================================
// Size / SetEvictable
// ============================================================================

TEST(ArcReplacerTest, NewReplacerHasZeroSize) {
    ArcReplacer replacer(4);
    EXPECT_EQ(replacer.Size(), 0);
}

TEST(ArcReplacerTest, RecordAccessAloneDoesNotAffectSize) {
    ArcReplacer replacer(4);
    replacer.RecordAccess(1, 100);
    EXPECT_EQ(replacer.Size(), 0);
}

TEST(ArcReplacerTest, SetEvictableTrueIncrementsSize) {
    ArcReplacer replacer(4);
    replacer.RecordAccess(1, 100);
    replacer.SetEvictable(1, true);
    EXPECT_EQ(replacer.Size(), 1);
}

TEST(ArcReplacerTest, SetEvictableFalseDecrementsSize) {
    ArcReplacer replacer(4);
    replacer.RecordAccess(1, 100);
    replacer.SetEvictable(1, true);
    replacer.SetEvictable(1, false);
    EXPECT_EQ(replacer.Size(), 0);
}

TEST(ArcReplacerTest, SetEvictableIsIdempotent) {
    ArcReplacer replacer(4);
    replacer.RecordAccess(1, 100);
    replacer.SetEvictable(1, true);
    replacer.SetEvictable(1, true);
    EXPECT_EQ(replacer.Size(), 1);
}

TEST(ArcReplacerTest, SetEvictableOnUntrackedFrameIsNoop) {
    ArcReplacer replacer(4);
    replacer.SetEvictable(2, true);
    EXPECT_EQ(replacer.Size(), 0);
}

TEST(ArcReplacerTest, SetEvictableOnOutOfRangeFrameAborts) {
    ArcReplacer replacer(4);
    EXPECT_DEATH(replacer.SetEvictable(4, true), "");
}

// ============================================================================
// Evict
// ============================================================================

TEST(ArcReplacerTest, EvictOnEmptyReplacerReturnsNullopt) {
    ArcReplacer replacer(4);
    EXPECT_EQ(replacer.Evict(), std::nullopt);
}

TEST(ArcReplacerTest, EvictWithNoEvictableFramesReturnsNullopt) {
    ArcReplacer replacer(4);
    replacer.RecordAccess(1, 100);
    replacer.RecordAccess(2, 200);
    // neither frame was ever marked evictable
    EXPECT_EQ(replacer.Evict(), std::nullopt);
    EXPECT_EQ(replacer.Size(), 0);
}

TEST(ArcReplacerTest, EvictPicksLeastRecentlyUsedEvictableFrame) {
    ArcReplacer replacer(3);
    replacer.RecordAccess(0, 100);
    replacer.RecordAccess(1, 200);
    replacer.RecordAccess(2, 300);
    replacer.SetEvictable(0, true);
    replacer.SetEvictable(1, true);
    replacer.SetEvictable(2, true);

    // all three are fresh (mru_) entries; frame 0 was touched first, so it's
    // the least recently used and should be victimized first.
    auto victim = replacer.Evict();
    ASSERT_TRUE(victim.has_value());
    EXPECT_EQ(victim.value(), 0);
    EXPECT_EQ(replacer.Size(), 2);
}

TEST(ArcReplacerTest, EvictSkipsPinnedFramesForOlderEvictableOne) {
    ArcReplacer replacer(3);
    replacer.RecordAccess(0, 100);
    replacer.RecordAccess(1, 200);
    replacer.RecordAccess(2, 300);
    // frame 0 is the least recently used, but it's pinned (not evictable).
    replacer.SetEvictable(1, true);
    replacer.SetEvictable(2, true);

    auto victim = replacer.Evict();
    ASSERT_TRUE(victim.has_value());
    EXPECT_EQ(victim.value(), 1);
    EXPECT_EQ(replacer.Size(), 1);
}

TEST(ArcReplacerTest, EvictedFrameBecomesGhostAndCanBeRevived) {
    ArcReplacer replacer(3);
    replacer.RecordAccess(1, 100);
    replacer.SetEvictable(1, true);

    auto victim = replacer.Evict();
    ASSERT_TRUE(victim.has_value());
    EXPECT_EQ(victim.value(), 1);
    EXPECT_EQ(replacer.Size(), 0);

    // page 100 should now be a ghost entry; accessing it again under a new
    // frame id (as a buffer pool would after re-fetching it from disk)
    // should hit the ghost path rather than crash or be treated as brand new.
    replacer.RecordAccess(2, 100);
    EXPECT_EQ(replacer.Size(), 0);  // revived frames start non-evictable
    replacer.SetEvictable(2, true);
    EXPECT_EQ(replacer.Size(), 1);
}

// ============================================================================
// Remove
// ============================================================================

TEST(ArcReplacerTest, RemoveOnUntrackedFrameIsNoop) {
    ArcReplacer replacer(4);
    replacer.Remove(1);
    EXPECT_EQ(replacer.Size(), 0);
}

TEST(ArcReplacerTest, RemoveEvictableFrameDecrementsSize) {
    ArcReplacer replacer(4);
    replacer.RecordAccess(1, 100);
    replacer.SetEvictable(1, true);
    replacer.Remove(1);
    EXPECT_EQ(replacer.Size(), 0);
}

TEST(ArcReplacerTest, RemoveOnPinnedFrameAborts) {
    ArcReplacer replacer(4);
    replacer.RecordAccess(1, 100);
    // never marked evictable -> still pinned
    EXPECT_DEATH(replacer.Remove(1), "");
}

TEST(ArcReplacerTest, RemovedFrameCanBeTreatedAsFreshOnNextAccess) {
    ArcReplacer replacer(4);
    replacer.RecordAccess(1, 100);
    replacer.SetEvictable(1, true);
    replacer.Remove(1);

    replacer.RecordAccess(1, 100);
    EXPECT_EQ(replacer.Size(), 0);  // fresh entries start non-evictable
    replacer.SetEvictable(1, true);
    EXPECT_EQ(replacer.Size(), 1);
}
