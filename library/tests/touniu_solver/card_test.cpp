#include <touniu_solver/card.hpp>

#include <gtest/gtest.h>

namespace touniu_solver {
namespace {

TEST(RankTest, StoresMillionthsWithoutFloatingPoint)
{
    const auto cherry_rank = Rank::from_millionths(14'000'001);
    const auto plus_two_rank = Rank::from_millionths(16'000'000);

    ASSERT_TRUE(cherry_rank.has_value());
    ASSERT_TRUE(plus_two_rank.has_value());
    EXPECT_EQ(cherry_rank->millionths(), 14'000'001);
    EXPECT_EQ(plus_two_rank->millionths(), 16'000'000);
    EXPECT_LT(*cherry_rank, *plus_two_rank);
}

TEST(RankTest, RejectsIntegerWhenCreatingFloatingRank)
{
    EXPECT_FALSE(Rank::floating(14'000'000).has_value());
    EXPECT_EQ(Rank::floating(14'999'999)->millionths(), 14'999'999);
}

TEST(CardTest, PreservesEntityIdentityAndIntrinsicSuit)
{
    const Card normal = Card::normal(CardId{41}, Suit::Spades, Rank::integer(14));
    const Card floating = Card::floating(CardId{42}, Suit::Hearts, *Rank::floating(11'250'001));

    EXPECT_EQ(normal.id(), CardId{41});
    EXPECT_EQ(normal.kind(), CardKind::Normal);
    EXPECT_EQ(normal.intrinsic_suits(), std::vector<Suit>{Suit::Spades});
    EXPECT_NE(normal.id(), floating.id());
    EXPECT_EQ(floating.kind(), CardKind::Floating);
}

TEST(CardTest, RepresentsMultiSuitAndSpecialCards)
{
    const Card four_suit = Card::four_suit(CardId{43}, Rank::integer(12));
    const Card cat = Card::three_suit_cat(CardId{44});
    const Card big_joker = Card::big_joker(CardId{45});
    const Card plus_two = Card::plus_two(CardId{46}, Suit::Diamonds);

    EXPECT_EQ(four_suit.kind(), CardKind::FourSuit);
    EXPECT_EQ(four_suit.intrinsic_suits().size(), 4U);
    EXPECT_EQ(cat.kind(), CardKind::ThreeSuitCat);
    EXPECT_EQ(big_joker.kind(), CardKind::BigJoker);
    EXPECT_EQ(big_joker.rank().millionths(), 16'000'000);
    EXPECT_EQ(plus_two.kind(), CardKind::PlusTwo);
    EXPECT_EQ(plus_two.rank().millionths(), 15'000'000);
}

TEST(CardTest, RepresentsNoTrumpAsTheFifthIntrinsicSuit)
{
    const Card no_trump = Card::no_trump(CardId{47}, Rank::integer(9));

    EXPECT_EQ(no_trump.kind(), CardKind::NoTrump);
    EXPECT_EQ(no_trump.intrinsic_suits(), std::vector<Suit>{Suit::NoTrump});
    EXPECT_EQ(no_trump.rank().millionths(), 9'000'000);
}

}  // namespace
}  // namespace touniu_solver
