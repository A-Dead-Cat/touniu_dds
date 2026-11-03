#include <touniu_solver/transposition_table.hpp>

#include <gtest/gtest.h>

namespace touniu_solver {
namespace {

auto lead_action(const std::uint64_t id) -> Action
{
    return Action::play(CardId{id}, PlayMode::Lead, Suit::Spades);
}

TEST(TranspositionTableTest, PrimaryCollisionsAreRejectedByVerifier)
{
    TranspositionTable table;
    const PositionIdentity first{42, 1};
    const PositionIdentity second{42, 2};
    table.store(first, 3, 7, std::nullopt, std::nullopt, lead_action(1));
    table.store(second, 3, 9, std::nullopt, std::nullopt, lead_action(2));

    const auto first_probe = table.probe(first);
    ASSERT_NE(first_probe.entry, nullptr);
    EXPECT_FALSE(first_probe.collision);
    ASSERT_TRUE(first_probe.entry->exact.has_value());
    EXPECT_EQ(*first_probe.entry->exact, 7);

    const auto second_probe = table.probe(second);
    ASSERT_NE(second_probe.entry, nullptr);
    ASSERT_TRUE(second_probe.entry->exact.has_value());
    EXPECT_EQ(*second_probe.entry->exact, 9);

    const auto collision = table.probe(PositionIdentity{42, 3});
    EXPECT_EQ(collision.entry, nullptr);
    EXPECT_TRUE(collision.collision);
    EXPECT_EQ(table.size(), 2U);
}

TEST(TranspositionTableTest, BoundsMergeIntoExactWhenTheyMeet)
{
    TranspositionTable table;
    const PositionIdentity id{7, 7};
    table.store(id, 4, std::nullopt, 3, std::nullopt, std::nullopt);
    table.store(id, 4, std::nullopt, std::nullopt, 5, std::nullopt);

    auto entry = table.probe(id).entry;
    ASSERT_NE(entry, nullptr);
    ASSERT_TRUE(entry->lower.has_value());
    ASSERT_TRUE(entry->upper.has_value());
    EXPECT_EQ(*entry->lower, 3);
    EXPECT_EQ(*entry->upper, 5);
    EXPECT_FALSE(entry->exact.has_value());

    table.store(id, 4, std::nullopt, std::nullopt, 6, std::nullopt);
    table.store(id, 4, std::nullopt, 2, std::nullopt, std::nullopt);
    entry = table.probe(id).entry;
    ASSERT_TRUE(entry->lower.has_value());
    ASSERT_TRUE(entry->upper.has_value());
    EXPECT_EQ(*entry->lower, 3);
    EXPECT_EQ(*entry->upper, 5);

    table.store(id, 4, std::nullopt, 5, std::nullopt, std::nullopt);
    entry = table.probe(id).entry;
    ASSERT_TRUE(entry->exact.has_value());
    EXPECT_EQ(*entry->exact, 5);
    EXPECT_FALSE(entry->lower.has_value());
    EXPECT_FALSE(entry->upper.has_value());
}

TEST(TranspositionTableTest, ExactValuesAreNeverReplaced)
{
    TranspositionTable table;
    const PositionIdentity id{9, 9};
    table.store(id, 2, 8, std::nullopt, std::nullopt, std::nullopt);
    table.store(id, 2, 3, std::nullopt, std::nullopt, std::nullopt);
    table.store(id, 2, std::nullopt, 4, std::nullopt, std::nullopt);
    table.store(id, 2, std::nullopt, std::nullopt, 6, std::nullopt);

    const auto entry = table.probe(id).entry;
    ASSERT_NE(entry, nullptr);
    ASSERT_TRUE(entry->exact.has_value());
    EXPECT_EQ(*entry->exact, 8);
}

TEST(TranspositionTableTest, DeeperRecordSupersedesShallowerOne)
{
    TranspositionTable table;
    const PositionIdentity id{11, 11};
    table.store(id, 1, std::nullopt, 2, std::nullopt, std::nullopt);
    table.store(id, 3, std::nullopt, std::nullopt, 6, std::nullopt);

    auto entry = table.probe(id).entry;
    ASSERT_NE(entry, nullptr);
    EXPECT_EQ(entry->depth, 3U);
    ASSERT_TRUE(entry->upper.has_value());
    EXPECT_EQ(*entry->upper, 6);
    EXPECT_FALSE(entry->lower.has_value());

    table.store(id, 2, 4, std::nullopt, std::nullopt, std::nullopt);
    entry = table.probe(id).entry;
    EXPECT_EQ(entry->depth, 3U);
    EXPECT_FALSE(entry->exact.has_value());
}

TEST(TranspositionTableTest, EvictionIsReportedOnlyWhenAVictimIsReplaced)
{
    TranspositionTable table;
    // The default table has 256 buckets, so these primaries share bucket 0.
    for (std::uint64_t index = 0; index < 4U; ++index) {
        EXPECT_FALSE(table.store(
            PositionIdentity{index * 256U, index},
            3,
            static_cast<int>(index),
            std::nullopt,
            std::nullopt,
            std::nullopt));
    }

    // Merging into the same identity is not an eviction.
    EXPECT_FALSE(table.store(
        PositionIdentity{0, 0}, 3, std::nullopt, 2, std::nullopt, std::nullopt));

    // A fifth distinct identity at the same depth replaces the shallowest slot.
    EXPECT_TRUE(table.store(
        PositionIdentity{4 * 256U, 4}, 3, 7, std::nullopt, std::nullopt, std::nullopt));
    const auto probe = table.probe(PositionIdentity{4 * 256U, 4});
    ASSERT_NE(probe.entry, nullptr);
    ASSERT_TRUE(probe.entry->exact.has_value());
    EXPECT_EQ(*probe.entry->exact, 7);
}

}  // namespace
}  // namespace touniu_solver
