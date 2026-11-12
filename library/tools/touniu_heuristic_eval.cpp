// Plays a deal with EW on a heuristic defence and NS double dummy, and reports
// how many tricks the heuristic loses against the double-dummy value.
//
// The heuristic only uses what a defender can see: its own hand, the trick in
// progress, the cards already played, the contract and the tricks won. It never
// looks at NS's hands, so the measurement is honest.
//
//   touniu_heuristic_eval --cards 8 --deals 100 [--specials 0] [--verbose]
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cctype>
#include <chrono>
#include <cstring>
#include <functional>
#include <optional>
#include <unordered_map>
#include <string>
#include <vector>

#include <touniu_solver/position.hpp>
#include <touniu_solver/search.hpp>

namespace {

using namespace touniu_solver;

auto suit_quick(const Position& position, int suit, Seat leader,
                Partnership side) -> int;

auto available_cash(const Position& position, Partnership side) -> int;
auto suit_quick(const Position& position, int suit, Seat leader,
                Partnership side) -> int;

auto suit_quick(const Position& position, int suit, Seat leader,
                Partnership side) -> int;

auto suit_dd(const Position& position, int suit_code, Seat leader,
             Seat entry_seat, int entries) -> int;

auto in_suit(const Card& card, const int code) -> bool;
auto effective_rank_micro(const Position& position, const Card* card) -> long long;
auto acts_as_trump(const Position& position, const Card& card) -> bool;

struct Args {
    int cards = 8;
    int deals = 100;
    int specials = 0;
    std::uint32_t seed = 20261104U;
    bool verbose = false;
    bool diagnose = false;
    int lead_rule = 0;  // 0 = highest card, 1 = lowest card, 2 = low card of longest suit
    bool dd_leads = false;   // use double dummy on EW leads, heuristic when following
    bool dd_follows = false; // use double dummy when following, heuristic on leads
    bool signals = false;   // pick the card inside its class by the convention
    bool void_aware = false; // use the voids inferred from the play
    bool full_info = false;  // EW sees all four hands, as double dummy does
    bool per_deal = false;   // one JSON line per deal, for corpus analysis
    bool per_card = false;   // dump every legal card and the tricks it costs
    std::string deal_spec;   // fixed deal: N:S8 H2J6T D3T CQ|E:...|S:...|W:...
    int leader = -1;         // opening leader when --deal is given
    std::string first_card;  // force this card as the very first play
    int show = -1;           // dump the hands and the play of this deal
    bool simulate = false;   // value a lead by playing its trick out, plus tempo
};

auto parse_args(const int argc, char** argv) -> Args
{
    Args args;
    for (int index = 1; index < argc; ++index) {
        const std::string flag = argv[index];
        if (flag == "--cards" && index + 1 < argc) {
            args.cards = std::atoi(argv[++index]);
        } else if (flag == "--deals" && index + 1 < argc) {
            args.deals = std::atoi(argv[++index]);
        } else if (flag == "--specials" && index + 1 < argc) {
            args.specials = std::atoi(argv[++index]);
        } else if (flag == "--seed" && index + 1 < argc) {
            args.seed = static_cast<std::uint32_t>(std::strtoul(argv[++index], nullptr, 10));
        } else if (flag == "--verbose") {
            args.verbose = true;
        } else if (flag == "--diagnose") {
            args.diagnose = true;
        } else if (flag == "--lead" && index + 1 < argc) {
            args.lead_rule = std::atoi(argv[++index]);
        } else if (flag == "--dd-leads") {
            args.dd_leads = true;
        } else if (flag == "--dd-follows") {
            args.dd_follows = true;
        } else if (flag == "--signals") {
            args.signals = true;
        } else if (flag == "--void-aware") {
            args.void_aware = true;
        } else if (flag == "--full-info") {
            args.full_info = true;
        } else if (flag == "--simulate") {
            args.simulate = true;
        } else if (flag == "--deal" && index + 1 < argc) {
            args.deal_spec = argv[++index];
        } else if (flag == "--first-card" && index + 1 < argc) {
            args.first_card = argv[++index];
        } else if (flag == "--leader" && index + 1 < argc) {
            const std::string seat = argv[++index];
            args.leader = seat == "N" ? 1 : seat == "E" ? 2 : seat == "S" ? 3 : 0;
        } else if (flag == "--per-card") {
            args.per_card = true;
        } else if (flag == "--per-deal") {
            args.per_deal = true;
        } else if (flag == "--show" && index + 1 < argc) {
            args.show = std::atoi(argv[++index]);
            args.diagnose = true;
            args.verbose = true;
        }
    }
    if (args.per_deal) {
        args.diagnose = true;
    }
    return args;
}

auto xorshift32(std::uint32_t& state) -> std::uint32_t
{
    std::uint32_t value = state;
    value ^= value << 13U;
    value ^= value >> 17U;
    value ^= value << 5U;
    state = value;
    return value;
}

// Mirrors the Playground's guide deck: specials are rolled per kind and
// *replace* ordinary cards, so the deck stays at fifty-two and every seat keeps
// a thirteen-card hand. `specials` acts as a toggle (0 = plain deck).
auto binomial_count(std::uint32_t& state, const int trials, const double mean) -> int
{
    const double probability = mean / static_cast<double>(trials);
    int count = 0;
    for (int trial = 0; trial < trials; ++trial) {
        const double roll = static_cast<double>(xorshift32(state)) /
            static_cast<double>(0xFFFFFFFFU);
        if (roll < probability) ++count;
    }
    return count;
}

auto make_deck(const int specials) -> std::vector<Card>
{
    const std::array<Suit, 4> suits{
        Suit::Spades, Suit::Hearts, Suit::Diamonds, Suit::Clubs};
    std::vector<Card> deck;
    std::uint64_t id = 1;
    for (const Suit suit : suits) {
        for (int rank = 2; rank <= 14; ++rank) {
            deck.push_back(Card::normal(CardId{id++}, suit, Rank::integer(rank)));
        }
    }
    if (specials <= 0) return deck;

    std::uint32_t state = 0x5EED1234U;
    auto shuffled_suits = [&]() {
        std::array<Suit, 4> pool = suits;
        for (int index = 3; index > 0; --index) {
            const int other = static_cast<int>(xorshift32(state) %
                static_cast<std::uint32_t>(index + 1));
            std::swap(pool[static_cast<std::size_t>(index)],
                      pool[static_cast<std::size_t>(other)]);
        }
        return pool;
    };
    int point_serial = 0;
    auto unique_point = [&]() -> Rank {
        ++point_serial;
        const std::int32_t millionths =
            1'000'000 + 100'000 + (point_serial * 7919) % 800'000;
        const std::optional<Rank> rank = Rank::floating(millionths);
        return rank.has_value() ? *rank : Rank::integer(2);
    };

    std::vector<Card> extras;
    auto suit_bound = [&](const int count, const int kind) {
        const std::array<Suit, 4> pool = shuffled_suits();
        const int take = count < 4 ? count : 4;
        for (int index = 0; index < take; ++index) {
            const Suit suit = pool[static_cast<std::size_t>(index)];
            switch (kind) {
                case 0: extras.push_back(Card::block(CardId{id++}, suit)); break;
                case 1: extras.push_back(Card::reverse(CardId{id++}, suit)); break;
                case 2: extras.push_back(Card::plus_two(CardId{id++}, suit)); break;
                default: extras.push_back(Card::cherry_bomb(CardId{id++}, suit)); break;
            }
        }
    };

    // Averages from GUIDE_SPECIAL_RULES in public/bridge-deal.js.
    const int floating = binomial_count(state, 10, 6.0);
    for (int index = 0; index < floating; ++index) {
        const std::array<Suit, 4> pool = shuffled_suits();
        const Rank rank = unique_point();
        extras.push_back(Card::floating(CardId{id++}, pool[0], rank));
    }
    suit_bound(binomial_count(state, 2, 1.0), 2);   // draw-two
    suit_bound(binomial_count(state, 2, 1.0), 0);   // block
    suit_bound(binomial_count(state, 2, 1.0), 1);   // reverse
    suit_bound(binomial_count(state, 2, 0.8), 3);   // cherry bomb
    const int cats = binomial_count(state, 2, 0.8);
    for (int index = 0; index < cats; ++index) {
        extras.push_back(Card::three_suit_cat(CardId{id++}));
    }
    const int four = binomial_count(state, 4, 2.0);
    for (int index = 0; index < four; ++index) {
        extras.push_back(Card::four_suit(CardId{id++}, unique_point()));
    }
    const int no_trump = binomial_count(state, 4, 2.0);
    for (int index = 0; index < no_trump; ++index) {
        extras.push_back(Card::no_trump(CardId{id++}, unique_point()));
    }
    const int jokers = binomial_count(state, 1, 0.6);
    for (int index = 0; index < jokers; ++index) {
        extras.push_back(index % 2 == 0
            ? Card::big_joker(CardId{id++})
            : Card::small_joker(CardId{id++}));
    }

    if (static_cast<std::size_t>(specials) < extras.size()) {
        extras.erase(
            extras.begin() + static_cast<std::ptrdiff_t>(specials), extras.end());
    }
    const std::size_t removable =
        extras.size() < deck.size() ? extras.size() : deck.size();
    // No resize: Card is not default-constructible.
    deck.erase(deck.end() - static_cast<std::ptrdiff_t>(removable), deck.end());
    for (std::size_t index = 0; index < extras.size(); ++index) {
        deck.push_back(std::move(extras[index]));
    }
    return deck;
}



// "N:S8 H2J6T D3T CQ|E:..." -> hands in deal order. Ranks use 2-9,T,J,Q,K,A.
auto parse_deal_spec(const std::string& spec, std::array<std::vector<Card>, 4>& hands)
    -> bool
{
    std::uint64_t id = 1;
    std::size_t start = 0;
    int seat = 0;
    while (start <= spec.size() && seat < 4) {
        std::size_t bar = spec.find('|', start);
        const std::string part = spec.substr(
            start, bar == std::string::npos ? std::string::npos : bar - start);
        start = bar == std::string::npos ? spec.size() + 1 : bar + 1;
        if (part.size() < 3) return false;
        if (part[1] != ':') return false;
        for (std::size_t index = 2; index < part.size();) {
            while (index < part.size() && part[index] == ' ') ++index;
            if (index >= part.size()) break;
            const char suit_letter = part[index++];
            const int group_suit = suit_letter == 'S' ? 0 : suit_letter == 'H' ? 1
                : suit_letter == 'D' ? 2 : suit_letter == 'C' ? 3 : -1;
            if (group_suit < 0 && suit_letter == 'X') {
                // "XCAT" is a three-flower cat: no suit, so it gets its own
                // group instead of riding in one.
                const std::size_t special_start = index;
                while (index < part.size() && part[index] != ' ') ++index;
                const std::string special = part.substr(special_start, index - special_start);
                if (special != "CAT") return false;
                hands[static_cast<std::size_t>(seat)].push_back(
                    Card::three_suit_cat(CardId{id++}));
                continue;
            }
            if (group_suit < 0) return false;
            // The ranks of one suit are written as one run: "K98", "AJT86".
            // A decimal run like "11.000001" is a floating card.
            const std::size_t group_start = index;
            while (index < part.size() && part[index] != ' ') ++index;
            const std::string group = part.substr(group_start, index - group_start);
            std::vector<std::string> tokens;
            for (std::size_t pos = 0; pos < group.size();) {
                const std::size_t dot = group.find('.', pos);
                if (dot != std::string::npos) {
                    std::size_t begin = dot;
                    while (begin > pos &&
                           std::isdigit(static_cast<unsigned char>(group[begin - 1]))) {
                        --begin;
                    }
                    std::size_t end = dot + 1;
                    while (end < group.size() &&
                           std::isdigit(static_cast<unsigned char>(group[end]))) {
                        ++end;
                    }
                    for (std::size_t k = pos; k < begin; ++k) {
                        tokens.push_back(std::string(1, group[k]));
                    }
                    tokens.push_back(group.substr(begin, end - begin));
                    pos = end;
                    continue;
                }
                if (group[pos] == '1' && pos + 1 < group.size() && group[pos + 1] == '0') {
                    tokens.push_back("10");
                    pos += 2;
                    continue;
                }
                tokens.push_back(std::string(1, group[pos]));
                ++pos;
            }
            for (const std::string& token : tokens) {
                if (token.find('.') != std::string::npos) {
                    const auto millionths = static_cast<std::int32_t>(
                        std::llround(std::atof(token.c_str()) * 1'000'000.0));
                    const std::optional<Rank> rank = Rank::floating(millionths);
                    if (!rank.has_value()) return false;
                    hands[static_cast<std::size_t>(seat)].push_back(
                        Card::floating(CardId{id++},
                                       static_cast<Suit>(group_suit), *rank));
                    continue;
                }
                const int rank = token == "10" || token == "T" ? 10
                    : token == "J" ? 11 : token == "Q" ? 12
                    : token == "K" ? 13 : token == "A" ? 14
                    : std::atoi(token.c_str());
                if (rank < 2) return false;
                hands[static_cast<std::size_t>(seat)].push_back(
                    Card::normal(CardId{id++},
                                 static_cast<Suit>(group_suit),
                                 Rank::integer(rank)));
            }
        }
        ++seat;
    }
    return seat == 4;
}


// "CT" / "SK" / "H10" -> suit letter plus rank.
auto card_matches_spec(const Card& card, const std::string& spec) -> bool
{
    if (spec.size() < 2) return false;
    const char suit_letter = spec[0];
    const int suit = suit_letter == 'S' ? 0 : suit_letter == 'H' ? 1
        : suit_letter == 'D' ? 2 : suit_letter == 'C' ? 3 : -1;
    if (suit < 0) return false;
    if (card.intrinsic_suits().size() != 1U) return false;
    if (static_cast<int>(card.intrinsic_suits().front()) != suit) return false;
    const std::string rest = spec.substr(1);
    const int rank = rest == "T" ? 10 : rest == "J" ? 11 : rest == "Q" ? 12
        : rest == "K" ? 13 : rest == "A" ? 14 : std::atoi(rest.c_str());
    return card.rank().millionths() == rank * 1'000'000;
}

auto build_scenario(const Args& args, const std::uint32_t seed) -> Scenario
{
    std::vector<Card> deck = make_deck(args.specials);
    std::uint32_t state = seed;
    for (int index = static_cast<int>(deck.size()) - 1; index >= 1; --index) {
        const std::uint32_t other = xorshift32(state) % static_cast<std::uint32_t>(index + 1);
        std::swap(deck[static_cast<std::size_t>(index)],
                  deck[static_cast<std::size_t>(other)]);
    }
    Scenario scenario;
    if (!args.deal_spec.empty()) {
        if (!parse_deal_spec(args.deal_spec, scenario.hands)) {
            std::fprintf(stderr, "bad --deal spec\n");
            std::exit(2);
        }
        scenario.next_player = static_cast<Seat>(args.leader < 0 ? 0 : args.leader);
        scenario.contract = Contract::NoTrump;
        scenario.tricks_remaining =
            static_cast<std::uint32_t>(scenario.hands[0].size());
        return scenario;
    }
    scenario.next_player = Seat::West;
    scenario.contract = Contract::NoTrump;
    scenario.tricks_remaining = static_cast<std::uint32_t>(args.cards);
    for (int index = 0; index < args.cards * 4; ++index) {
        scenario.hands[static_cast<std::size_t>(index % 4)].push_back(
            deck[static_cast<std::size_t>(index)]);
    }
    return scenario;
}

auto partnership_of(const Seat seat) -> Partnership
{
    return seat == Seat::North || seat == Seat::South
        ? Partnership::NorthSouth : Partnership::EastWest;
}

auto side_of(const Seat seat) -> int
{
    return partnership_of(seat) == Partnership::EastWest ? 0 : 1;
}

auto partner_of(const Seat seat) -> Seat
{
    return static_cast<Seat>((static_cast<int>(seat) + 2) % 4);
}

// Contract-derived trump suit; NoTrump means trump-like cards only.
auto trump_of(const Contract contract) -> std::optional<Suit>
{
    if (contract == Contract::NoTrump) {
        return std::nullopt;
    }
    return static_cast<Suit>(static_cast<int>(contract));
}

auto card_of(const Position& position, const CardId id) -> const Card*
{
    return position.card(id);
}

// True when the card behaves as trump: a joker, a NoTrump-kind card in a
// no-trump contract, or a card of the contract suit.
auto acts_as_trump(const Position& position, const Card& card) -> bool
{
    if (card.kind() == CardKind::BigJoker || card.kind() == CardKind::SmallJoker) {
        return true;
    }
    const std::optional<Suit> trump = trump_of(position.contract());
    if (!trump.has_value()) {
        return card.kind() == CardKind::NoTrump;
    }
    return std::find(card.intrinsic_suits().begin(), card.intrinsic_suits().end(), *trump)
        != card.intrinsic_suits().end();
}

auto beats(const Card& candidate, const Card& best) -> bool
{
    return candidate.rank() > best.rank();
}

// The heuristic defence for one EW decision. Returns the chosen action.
// Declared card-selection convention, so a player reading EW's spot cards can
// infer the suit length. It is a real policy, not a value-neutral relabelling:
// it can pick a card that is not interchangeable with the heuristic's choice.
//
//   leading a small card (9 or lower)
//     odd count      -> lowest
//     even count     -> third highest
//     two small only -> the higher of the two
//   following with a small card
//     even count     -> highest small card
//     two small only -> the first (lowest)
//     four cards     -> the highest small card that is not the lowest
//   from a touching sequence (T9, JT, ...)
//     leading  -> the top of the sequence
//     following-> the lowest of the sequence
auto small_rank(const Card& card) -> bool
{
    return card.rank().millionths() <= 9 * 1'000'000;
}

auto convention_pick(
    const Position& position,
    const Action& chosen,
    const ActionList& legal,
    const bool leading) -> Action
{
    if (chosen.is_pass() || !chosen.card_id().has_value()) {
        return chosen;
    }
    const Card* chosen_card = position.card(*chosen.card_id());
    if (chosen_card == nullptr || chosen_card->intrinsic_suits().size() != 1U) {
        return chosen;
    }
    const Suit suit = chosen_card->intrinsic_suits().front();
    if (suit == Suit::NoTrump) {
        return chosen;
    }

    // Every legal action of this suit, in descending rank.
    std::vector<const Action*> suited;
    std::vector<const Card*> suited_cards;
    for (const Action& action : legal) {
        if (action.is_pass() || !action.card_id().has_value()) continue;
        const Card* card = position.card(*action.card_id());
        if (card == nullptr || card->intrinsic_suits().size() != 1U) continue;
        if (card->intrinsic_suits().front() != suit) continue;
        suited.push_back(&action);
        suited_cards.push_back(card);
    }
    if (suited.size() <= 1U) {
        return chosen;
    }
    std::vector<std::size_t> order(suited.size());
    for (std::size_t index = 0; index < order.size(); ++index) order[index] = index;
    std::sort(order.begin(), order.end(), [&](const std::size_t left, const std::size_t right) {
        return suited_cards[left]->rank() > suited_cards[right]->rank();
    });
    const auto at = [&](const std::size_t index) -> const Action& {
        return *suited[order[index]];
    };
    const auto rank_at = [&](const std::size_t index) -> bool {
        return small_rank(*suited_cards[order[index]]);
    };

    // My full holding in the suit (the legal list equals it when leading).
    std::vector<Rank> holding;
    for (const CardId id : position.hand(position.next_player())) {
        const Card* card = position.card(id);
        if (card == nullptr || card->intrinsic_suits().size() != 1U) continue;
        if (card->intrinsic_suits().front() == suit) holding.push_back(card->rank());
    }
    const std::size_t count = holding.size();

    if (!leading) {
        // Touching sequence: follow with its lowest card.
        const std::int32_t chosen_rank = chosen_card->rank().millionths();
        for (std::size_t index = 1; index < order.size(); ++index) {
            const std::int32_t rank = suited_cards[order[index]]->rank().millionths();
            if (rank + 1'000'000 == chosen_rank) {
                // Walk down the touching run.
                std::size_t lowest = index;
                std::int32_t current = rank;
                for (std::size_t next = index + 1; next < order.size(); ++next) {
                    const std::int32_t below = suited_cards[order[next]]->rank().millionths();
                    if (below + 1'000'000 != current) break;
                    lowest = next;
                    current = below;
                }
                return at(lowest);
            }
        }
        if (!small_rank(*chosen_card)) {
            return chosen;
        }
        if (count == 2U) {
            return at(order.size() - 1U);        // doubleton of small cards: lowest
        }
        if (count == 4U) {
            // Highest small card that is not the lowest one.
            for (std::size_t index = 0; index + 1U < order.size(); ++index) {
                if (rank_at(index)) return at(index);
            }
            return chosen;
        }
        if (count % 2U == 0U) {
            for (std::size_t index = 0; index < order.size(); ++index) {
                if (rank_at(index)) return at(index);
            }
            return chosen;
        }
        return chosen;
    }

    if (!small_rank(*chosen_card)) {
        return chosen;   // the heuristic led a high card: keep the top of its run
    }
    if (count == 2U) {
        const bool both_small = std::all_of(holding.begin(), holding.end(), [](const Rank& rank) {
            return rank.millionths() <= 9 * 1'000'000;
        });
        return both_small ? at(0) : chosen;
    }
    if (count % 2U == 1U) {
        return at(order.size() - 1U);            // odd count: lowest
    }
    return order.size() >= 3U ? at(2) : chosen;  // even count: third highest
}

// Voids inferred from the cards already played: public information that a
// defender is entitled to use. void_seat[seat][suit] is true when that seat has
// failed to follow the suit at some point.
struct PlayTracker {
    std::array<std::array<bool, 5>, 4> void_seat{};
    std::array<std::array<bool, 5>, 4> seen_suit{};

    void observe(const Position& before, const Action& action)
    {
        const Seat mover = before.next_player();
        const Trick& trick = before.current_trick();
        const bool leading = trick.empty();
        std::size_t led = 4U;
        for (const TrickPlay& play : trick) {
            if (play.action.is_pass() || !play.action.effective_suit().has_value()) continue;
            led = static_cast<std::size_t>(*play.action.effective_suit());
            break;
        }
        if (action.is_pass()) return;
        const Card* card = before.card(*action.card_id());
        if (card == nullptr) return;
        const std::size_t suit = card->intrinsic_suits().empty()
            ? 4U : static_cast<std::size_t>(card->intrinsic_suits().front());
        if (!leading && led < 4U) {
            if (suit == led) {
                seen_suit[static_cast<std::size_t>(mover)][led] = true;
            } else if (!before.legal_actions().empty()) {
                // It played another suit (or a multi-suit card) while the lead
                // suit was on: a real void only when it had no card of that
                // suit and no multi-suit option, so record it conservatively.
                seen_suit[static_cast<std::size_t>(mover)][led] = true;
            }
        }
        for (const Suit intrinsic : card->intrinsic_suits()) {
            seen_suit[static_cast<std::size_t>(mover)]
                     [static_cast<std::size_t>(intrinsic)] = true;
        }
    }
};

// Full-information lead: the defender sees all four hands (the same assumption
// double dummy makes), so it does not need a search to know which suit cashes,
// which one an opponent can ruff and which one gives the partner a ruff. The
// cost is a scan of the hands, not a tree.

// Cards our side holds above every card the opponents hold in that suit: the
// tricks we could run without giving the lead away.
auto side_sure_winners(const Position& position, const Partnership side) -> int
{
    int total = 0;
    for (int suit = 0; suit < 4; ++suit) {
        long long their_best = -1;
        for (int seat = 0; seat < 4; ++seat) {
            const Seat who = static_cast<Seat>(seat);
            if (partnership_of(who) == side) continue;
            for (const CardId id : position.hand(who)) {
                const Card* card = position.card(id);
                if (card == nullptr) continue;
                if (!in_suit(*card, suit)) continue;
                their_best = std::max<long long>(
                    their_best, card->rank().millionths());
            }
        }
        for (int seat = 0; seat < 4; ++seat) {
            const Seat who = static_cast<Seat>(seat);
            if (partnership_of(who) != side) continue;
            for (const CardId id : position.hand(who)) {
                const Card* card = position.card(id);
                if (card == nullptr) continue;
                if (!in_suit(*card, suit)) continue;
                if (card->rank().millionths() > their_best) ++total;
            }
        }
    }
    return total;
}

// A suit the opponents can no longer stop: they hold at most three cards of it
// and every card we hold is above their best. Cashing it out drops their cards
// and every card of ours left in the suit then wins, because in no-trump a
// discard can never take a trick. The shorter holding has to cash first: if the
// long hand plays its honours the short hand's winners are left stranded.
struct CashRun {
    const Card* card = nullptr;
    int tricks = 0;
    // A suit that would cash but for a cat in an opponent's hand. One extra
    // hold-up round means the long hand must not open it: the short hand has to
    // lead it through the opponent instead.
    int blocked_suit = -1;
};

// A spare entry: a suit the short hand can lead and the long hand wins whatever
// the opponents hold. That is the road back after the short hand's last card of
// the run suit is gone.
auto has_spare_entry(
    const Position& position, const Seat long_hand, const Seat short_hand,
    const int skip_suit) -> bool
{
    for (int suit = 0; suit < 4; ++suit) {
        if (suit == skip_suit) continue;
        bool short_holds = false;
        for (const CardId id : position.hand(short_hand)) {
            const Card* card = position.card(id);
            if (card != nullptr && in_suit(*card, suit)) {
                short_holds = true;
                break;
            }
        }
        if (!short_holds) continue;
        long long long_best = -1;
        long long other_best = -1;
        for (int seat = 0; seat < 4; ++seat) {
            const Seat who = static_cast<Seat>(seat);
            if (who == short_hand) continue;
            for (const CardId id : position.hand(who)) {
                const Card* card = position.card(id);
                if (card == nullptr || !in_suit(*card, suit)) continue;
                if (who == long_hand) {
                    long_best = std::max<long long>(long_best, card->rank().millionths());
                } else {
                    other_best = std::max<long long>(other_best, card->rank().millionths());
                }
            }
        }
        if (long_best > other_best) return true;
    }
    return false;
}

auto cash_run_lead(const Position& position, const bool allow) -> CashRun
{
    CashRun best;
    if (!allow || position.contract() != Contract::NoTrump) return best;
    const Seat me = position.next_player();
    const Seat partner = partner_of(me);
    const int remaining = static_cast<int>(position.tricks_remaining());
    for (int suit = 0; suit < 4; ++suit) {
        std::vector<const Card*> mine;
        std::vector<const Card*> mates;
        int their_count = 0;
        long long their_best = -1;
        // A trump-like card can win any trick whatever the led suit, and a +2
        // also outranks an ace, so either one ends the run. Bombs are left out
        // because a bomb hands the trick to the next seat: an opponent's bomb is
        // a trick for us.
        bool trump_behind = false;
        bool cat_behind = false;
        for (int seat = 0; seat < 4; ++seat) {
            const Seat who = static_cast<Seat>(seat);
            for (const CardId id : position.hand(who)) {
                const Card* card = position.card(id);
                if (card == nullptr) continue;
                if (who != me && who != partner && acts_as_trump(position, *card)) {
                    trump_behind = true;
                }
                if (who != me && who != partner &&
                    card->kind() == CardKind::ThreeSuitCat) {
                    cat_behind = true;
                }
                if (!in_suit(*card, suit)) continue;
                // A bomb that takes the trick gives it to our side, so it is not
                // a stopper; it is also not a card to cash with.
                if (card->kind() == CardKind::CherryBomb) continue;
                if (card->kind() == CardKind::PlusTwo && who != me && who != partner) {
                    ++their_count;
                    their_best = std::max<long long>(their_best, 15'000'000);
                    continue;
                }
                if (who == me) {
                    mine.push_back(card);
                } else if (who == partner) {
                    mates.push_back(card);
                } else {
                    ++their_count;
                    their_best = std::max<long long>(
                        their_best, effective_rank_micro(position, card));
                }
            }
        }
        if (trump_behind) continue;
        if (mine.empty()) continue;
        // The cat is no card of the suit, but it can follow any ordinary lead in
        // no-trump and lose, so one round of the run can be absorbed without
        // spending a real card: the opponents hold up once longer.
        const int rounds = their_count + (cat_behind ? 1 : 0);
        if (rounds < 1 || rounds > 3) continue;
        int run_mine = 0;
        int run_mates = 0;
        for (const Card* card : mine) {
            if (effective_rank_micro(position, card) > their_best) ++run_mine;
        }
        for (const Card* card : mates) {
            if (effective_rank_micro(position, card) > their_best) ++run_mates;
        }
        if (cat_behind && run_mine + run_mates < rounds &&
            run_mine + run_mates >= their_count &&
            mine.size() > mates.size() && best.blocked_suit < 0) {
            // Only from the long hand: the short hand leading this suit runs it
            // through the opponent and is exactly the line to aim for.
            best.blocked_suit = suit;
        }
        // Their last cards must all fall under ours, and the run has to leave
        // something behind: covering their cards is not a gain by itself.
        if (run_mine + run_mates < rounds) continue;
        // Only the leader's own cards can be reeled off consecutively: once the
        // partner takes one of them the lead moves to the other hand, which may
        // have no way back.
        const int tricks = std::min(remaining, static_cast<int>(mine.size()));
        if (tricks < 2) continue;
        // Cash the short side's honours first, but only when the hand can still
        // be reached afterwards: a short holding that is emptied by cashing
        // needs a spare entry in the long hand, otherwise its winners starve
        // (a singleton Q opposite AKx with no entry wins one trick).
        // Short holding with no winner at all: cash the long hand's own top.
        const bool mine_shorter = mine.size() < mates.size();
        const bool mine_longer = mine.size() > mates.size();
        const int short_winners = mine_shorter ? run_mine : run_mates;
        const int long_winners = mine_shorter ? run_mates : run_mine;
        const std::size_t short_len = mine_shorter ? mine.size() : mates.size();
        const Seat short_seat = mine_shorter ? me : partner;
        const Seat long_seat = mine_shorter ? partner : me;
        bool cash_short_first = short_winners > 0;
        if (cash_short_first && short_len == 1U && long_winners > 0 &&
            !has_spare_entry(position, long_seat, short_seat, suit)) {
            cash_short_first = false;
        }
        // Leading low hands the trick to the partner's honour; only do it when
        // the short hand is the one that should cash, and the lead is not
        // already there.
        const bool lead_low = cash_short_first && mine_longer &&
            std::getenv("TUONIU_NO_CASH_LEAD_LOW") == nullptr;
        const Card* choice = nullptr;
        const Card* lowest = nullptr;
        for (const Card* card : mine) {
            const long long rank = effective_rank_micro(position, card);
            if (lowest == nullptr ||
                rank < effective_rank_micro(position, lowest)) {
                lowest = card;
            }
            if (rank <= their_best) continue;
            if (choice == nullptr ||
                rank > effective_rank_micro(position, choice)) {
                choice = card;
            }
        }
        if (choice == nullptr) choice = lowest;
        if (lead_low) choice = lowest;
        if (choice == nullptr) continue;
        if (tricks > best.tricks) {
            best.card = choice;
            best.tricks = tricks;
        }
    }
    return best;
}

auto full_info_lead(
    const Position& position,
    const ActionList& legal,
    double* best_score_out = nullptr,
    const Card** best_card_out = nullptr) -> Action
{
    const Seat me = position.next_player();
    const Seat partner = partner_of(me);
    std::array<Seat, 2> opponents{};
    {
        int slot = 0;
        for (int seat = 0; seat < 4; ++seat) {
            const Seat other = static_cast<Seat>(seat);
            if (other != me && other != partner) {
                opponents[static_cast<std::size_t>(slot++)] = other;
            }
        }
    }

    // Highest live card per suit, and who holds it. Index 4 collects the
    // trump-like cards (jokers and NoTrump-kind cards in a no-trump contract),
    // which outrank every ordinary suit and have to be led as a suit of their
    // own.
    std::array<std::optional<Rank>, 5> top_rank{};
    std::array<Seat, 5> top_seat{};
    std::array<const Card*, 5> top_card{};
    std::array<bool, 5> partner_void{};
    std::array<bool, 5> opponent_void{};
    std::array<int, 5> length{};
    std::array<bool, 5> opponent_has_trump{};
    std::array<bool, 5> partner_has_trump{};
    for (int seat = 0; seat < 4; ++seat) {
        const Seat other = static_cast<Seat>(seat);
        for (const CardId id : position.hand(other)) {
            const Card* card = position.card(id);
            if (card == nullptr) continue;
            const bool trump_card = acts_as_trump(position, *card);
            if (trump_card) {
                if (other == partner) partner_has_trump.fill(true);
                if (other != me && other != partner) opponent_has_trump.fill(true);
            }
            std::size_t suit = 0;
            if (trump_card) {
                suit = 4;
            } else {
                if (card->intrinsic_suits().size() != 1U) continue;
                suit = static_cast<std::size_t>(card->intrinsic_suits().front());
                if (suit >= 4U) continue;
            }
            if (other == me) length[suit] += 1;
            if (!top_rank[suit].has_value() || card->rank() > *top_rank[suit]) {
                top_rank[suit] = card->rank();
                top_seat[suit] = other;
                top_card[suit] = card;
            }
            if (opponent_has_trump[suit] && other != me && other != partner) {
                opponent_void[suit] = false;   // it has a card of the suit
            }
        }
    }
    // Second pass for the voids: a seat void in a suit has no card of it.
    for (std::size_t suit = 0; suit < 4U; ++suit) {
        bool partner_has = false;
        bool opponent_has = false;
        for (int seat = 0; seat < 4; ++seat) {
            const Seat other = static_cast<Seat>(seat);
            if (other != me && other != partner && other == partner) continue;
            bool holds = false;
            for (const CardId id : position.hand(other)) {
                const Card* card = position.card(id);
                if (card == nullptr || card->intrinsic_suits().size() != 1U) continue;
                if (static_cast<std::size_t>(card->intrinsic_suits().front()) == suit) {
                    holds = true;
                    break;
                }
            }
            if (other == partner) partner_has = holds;
            else if (other != me) opponent_has = opponent_has || holds;
        }
        partner_void[suit] = !partner_has;
        opponent_void[suit] = !opponent_has;
    }
    {
        bool partner_has = false;
        bool opponent_has = false;
        for (int seat = 0; seat < 4; ++seat) {
            const Seat other = static_cast<Seat>(seat);
            bool holds = false;
            for (const CardId id : position.hand(other)) {
                const Card* card = position.card(id);
                if (card != nullptr && acts_as_trump(position, *card)) {
                    holds = true;
                    break;
                }
            }
            if (other == partner) partner_has = holds;
            else if (other != me) opponent_has = opponent_has || holds;
        }
        partner_void[4] = !partner_has;
        opponent_void[4] = !opponent_has;
    }

    // Race: when everything the opponents could run plus everything we could
    // run covers the rest of the deal, cashing comes before shape.
    const Partnership side = partnership_of(me);
    const Partnership other =
        side == Partnership::EastWest ? Partnership::NorthSouth : Partnership::EastWest;
    const int remaining = static_cast<int>(position.tricks_remaining());
    const int ours_running = side_sure_winners(position, side);
    const int theirs_running = side_sure_winners(position, other);
    const bool race = ours_running + theirs_running >= remaining;
    if (std::getenv("TUONIU_DEBUG_SCORES") != nullptr) {
        std::fprintf(stderr, "cash ours=%d%c", available_cash(position, side),
                     static_cast<char>(10));
    }
    if (std::getenv("TUONIU_DEBUG_SCORES") != nullptr) {
        for (int probe = 0; probe < 4; ++probe) {
            std::fprintf(stderr, "quick suit %d E=%d W=%d%c", probe,
                suit_quick(position, probe, Seat::East, Partnership::EastWest),
                suit_quick(position, probe, Seat::West, Partnership::EastWest),
                static_cast<char>(10));
        }
    }
    if (std::getenv("TUONIU_DEBUG_SCORES") != nullptr) {
        for (int probe = 0; probe < 4; ++probe) {
            std::fprintf(stderr, "suit %d E=%d W=%d N=%d S=%d%c", probe,
                suit_dd(position, probe, Seat::East, Seat::East, 0),
                suit_dd(position, probe, Seat::West, Seat::West, 0),
                suit_dd(position, probe, Seat::North, Seat::North, 0),
                suit_dd(position, probe, Seat::South, Seat::South, 0),
                static_cast<char>(10));
        }
    }

    // Cash a suit out before anything else: their last cards fall under our top
    // ones, and every card of ours left in the suit wins afterwards.
    const CashRun run = cash_run_lead(
        position, std::getenv("TUONIU_NO_CASH_RUN") == nullptr);
    if (run.card != nullptr) {
        if (std::getenv("TUONIU_DEBUG_CASH") != nullptr) {
            std::fprintf(stderr, "cashrun seat=%d suit=%d rank=%d tricks=%d%c",
                         static_cast<int>(position.next_player()),
                         run.card->intrinsic_suits().empty()
                             ? -1 : static_cast<int>(run.card->intrinsic_suits().front()),
                         static_cast<int>(run.card->rank().millionths() / 1'000'000),
                         run.tricks, static_cast<char>(10));
        }
        for (const Action& action : legal) {
            if (!action.is_pass() && action.card_id().has_value() &&
                *action.card_id() == run.card->id()) {
                return action;
            }
        }
    }

    const Action* best = nullptr;
    double best_score = -1e9;
    const Card* best_card = nullptr;
    bool best_drops_singleton = false;
    std::array<bool, 4> freeze_eligible{true, true, true, true};
    for (int seat = 0; seat < 4; ++seat) {
        for (const CardId id : position.hand(static_cast<Seat>(seat))) {
            const Card* held = position.card(id);
            if (held == nullptr || held->intrinsic_suits().size() != 1U) continue;
            const int held_suit = static_cast<int>(held->intrinsic_suits().front());
            if (held_suit < 0 || held_suit >= 4) continue;
            if (held->kind() != CardKind::Normal &&
                held->kind() != CardKind::Floating) {
                freeze_eligible[static_cast<std::size_t>(held_suit)] = false;
            }
        }
    }
    std::array<std::array<int, 4>, 4> suit_dd_by_leader{};
    std::array<bool, 4> suit_dd_ready{};
    const auto directional_suit_values = [&](const int suit)
        -> const std::array<int, 4>& {
        if (!suit_dd_ready[static_cast<std::size_t>(suit)]) {
            for (int leader = 0; leader < 4; ++leader) {
                const Seat seat = static_cast<Seat>(leader);
                suit_dd_by_leader[static_cast<std::size_t>(suit)]
                    [static_cast<std::size_t>(leader)] =
                    suit_dd(position, suit, seat, seat, 0);
            }
            suit_dd_ready[static_cast<std::size_t>(suit)] = true;
        }
        return suit_dd_by_leader[static_cast<std::size_t>(suit)];
    };
    for (const Action& action : legal) {
        if (action.is_pass() || !action.card_id().has_value()) continue;
        const Card* card = position.card(*action.card_id());
        if (card == nullptr) continue;
        std::size_t suit = 0;
        if (acts_as_trump(position, *card)) {
            suit = 4;
        } else {
            if (card->intrinsic_suits().size() != 1U) continue;
            suit = static_cast<std::size_t>(card->intrinsic_suits().front());
            if (suit >= 4U) continue;
        }
        double score = 0.0;
        if (suit < 4U && freeze_eligible[suit] &&
            std::getenv("TUONIU_FROZEN_LEAD_OFF") == nullptr) {
            const auto& directional = directional_suit_values(
                static_cast<int>(suit));
            const int mine = directional[static_cast<std::size_t>(me)];
            const int partners = directional[static_cast<std::size_t>(partner)];
            const int ns_baseline = std::min(
                directional[static_cast<std::size_t>(Seat::North)],
                directional[static_cast<std::size_t>(Seat::South)]);
            const bool my_lead_is_harmful = mine < ns_baseline;
            const bool partners_lead_is_harmful = partners < ns_baseline;
            const int current_quick = suit_quick(
                position, static_cast<int>(suit), me, side);
            // Establishing means this lead is not cashing now, but creates
            // tricks that our side can take after losing and regaining lead.
            const bool establishes_long_suit = mine > current_quick;
            const bool partner_has_entry = has_spare_entry(
                position, partner, me, static_cast<int>(suit));
            const bool frozen = my_lead_is_harmful && partners_lead_is_harmful;
            const bool half_frozen = my_lead_is_harmful &&
                !partners_lead_is_harmful;
            const bool establishment_exception =
                std::getenv("TUONIU_FROZEN_ESTABLISHMENT_OFF") == nullptr &&
                ((frozen && establishes_long_suit) ||
                 (half_frozen && establishes_long_suit && !partner_has_entry));
            if (my_lead_is_harmful && !establishment_exception) {
                // Keep the lead legal as a last resort, but make every safe
                // suit preferable. The extra term orders multiple forced
                // choices by how many tricks they donate.
                score -= 100.0 + 10.0 * static_cast<double>(ns_baseline - mine);
            }
        }
        // A cat behind one of our runs absorbs a round, so opening the suit
        // from the long hand only wins the covered part; the short hand has to
        // lead it through the opponent later.
        if (run.blocked_suit >= 0 &&
            suit == static_cast<std::size_t>(run.blocked_suit)) {
            score -= 2.0;
        }
        // A cherry bomb outranks every ordinary card but hands the trick to the
        // seat after the thrower, so holding the top bomb is not cashing.
        const bool top_is_bomb =
            top_card[suit] != nullptr &&
            top_card[suit]->kind() == CardKind::CherryBomb;
        const bool top_is_ours = !top_is_bomb && top_rank[suit].has_value() &&
            (top_seat[suit] == me || top_seat[suit] == partner);
        // Cashing a suit our side controls is worth a lot; leading a suit the
        // opponents control gives a trick away.
        // Cashing a top card is only worth it when there is a suit left behind
        // it; a lone honour is one trick and hands the lead back.
        const double cash = top_seat[suit] == me && length[suit] >= 3U ? 3.0 : 2.0;
        score += top_is_ours ? cash : -1.0;
        // A ruff for the partner is nearly as good; a ruff for an opponent is
        // the worst thing a lead can do.
        if (suit < 4U) {
            if (partner_void[suit] && partner_has_trump[suit]) score += 2.0;
            if (opponent_void[suit] && opponent_has_trump[suit]) score -= 4.0;
        }
        // Long-suit value compares the four counts, not just our own length:
        // how long our longer hand is, our side's total, the same for them, and
        // whether they can follow at all.
        {
            int our_total = 0;
            int our_max = 0;
            int their_total = 0;
            int their_max = 0;
            for (int seat = 0; seat < 4; ++seat) {
                const Seat who = static_cast<Seat>(seat);
                int count = 0;
                for (const CardId id : position.hand(who)) {
                    const Card* held = position.card(id);
                    if (held != nullptr && in_suit(*held, static_cast<int>(suit))) {
                        ++count;
                    }
                }
                if (who == me || who == partner) {
                    our_total += count;
                    our_max = std::max(our_max, count);
                } else {
                    their_total += count;
                    their_max = std::max(their_max, count);
                }
            }
            if (their_total == 0) {
                // Both opponents are void: leading this suit wins outright.
                score += 6.0;
            } else {
                score += 0.3 * static_cast<double>(our_total - their_total);
                score += 0.15 * static_cast<double>(our_max - their_max);

                // Do not establish a long suit merely because the other
                // opponent is void. Once our stopper is driven out, all of
                // the concentrated holding sits in one hand and can run. The
                // previous total/max terms barely distinguished 6-0 from
                // 3-3, which made a defender lead directly into a six-card
                // suit when the other opponent had none.
                const int their_min = their_total - their_max;
                const int concentration = their_max - their_min;
                score -= 0.75 * static_cast<double>(
                    std::max(0, concentration - 2));
            }
        }
        // Do not throw the functional cards away as leads: a bomb gives the
        // trick to the next seat and a +2 hands the next seat two cards.
        if (card->kind() == CardKind::CherryBomb) score -= 2.5;
        if (card->kind() == CardKind::PlusTwo) score -= 1.5;
        if (race) {
            // Under the race, a card of ours that beats everything they hold
            // in the suit is a trick that must be taken now.
            long long their_best = -1;
            for (int seat = 0; seat < 4; ++seat) {
                const Seat who = static_cast<Seat>(seat);
                if (partnership_of(who) == side) continue;
                for (const CardId id : position.hand(who)) {
                    const Card* held = position.card(id);
                    if (held == nullptr || !in_suit(*held, static_cast<int>(suit))) continue;
                    their_best = std::max<long long>(
                        their_best, held->rank().millionths());
                }
            }
            if (card->rank().millionths() > their_best) score += 4.0;
        }
        // Inside the chosen suit: cash the highest card when our side holds the
        // top, otherwise lead low. That is what the blind rule gets right.
        // Cash only a top card we own. When the partner holds the top, leading
        // low hands the lead to the partner's winner and keeps ours in hand,
        // which is the transfer the double-dummy line takes.
        // Leading from a touching sequence plays its top card: leading the low
        // end hands the trick to whatever sits just above it.
        bool leading_sequence = false;
        long long sequence_top = -1;
        {
            struct LiveRank {
                long long rank;
                Seat owner;
            };
            std::vector<LiveRank> live_in_suit;
            // Touching means adjacent in the exact live rank order. A
            // floating card anywhere between J and T therefore breaks JT;
            // two cards are not touching merely because their integer parts
            // differ by one.
            for (int seat = 0; seat < 4; ++seat) {
                const Seat who = static_cast<Seat>(seat);
                for (const CardId id : position.hand(who)) {
                    const Card* held = position.card(id);
                    if (held == nullptr || !in_suit(*held, static_cast<int>(suit)) ||
                        (held->kind() != CardKind::Normal &&
                         held->kind() != CardKind::Floating)) {
                        continue;
                    }
                    live_in_suit.push_back({held->rank().millionths(), who});
                }
            }
            std::sort(live_in_suit.begin(), live_in_suit.end(),
                      [](const LiveRank& left, const LiveRank& right) {
                          return left.rank > right.rank;
                      });
            for (std::size_t index = 0; index + 1 < live_in_suit.size(); ++index) {
                if (live_in_suit[index].owner == me &&
                    live_in_suit[index + 1].owner == me) {
                    leading_sequence = true;
                    sequence_top = live_in_suit[index].rank;
                    break;
                }
            }
        }
        // Lead a high card to swallow an opponent's singleton intermediate
        // when our side owns the next link below it. For example, J from J9
        // opposite T, or from JT in one hand, drops a forced singleton
        // 10.142857 and promotes the T.
        bool drops_singleton = false;
        if (suit < 4U &&
            (card->kind() == CardKind::Normal ||
             card->kind() == CardKind::Floating)) {
            const long long candidate_rank = card->rank().millionths();
            long long side_link = -1;
            for (const Seat side_seat : {me, partner}) {
                for (const CardId id : position.hand(side_seat)) {
                    if (id == card->id()) continue;
                    const Card* held = position.card(id);
                    if (held == nullptr || !in_suit(*held, static_cast<int>(suit)) ||
                        (held->kind() != CardKind::Normal &&
                         held->kind() != CardKind::Floating)) {
                        continue;
                    }
                    const long long rank = held->rank().millionths();
                    if (rank < candidate_rank) side_link = std::max(side_link, rank);
                }
            }
            int intermediate_count = 0;
            Seat intermediate_holder = me;
            for (const Seat opponent : opponents) {
                for (const CardId id : position.hand(opponent)) {
                    const Card* held = position.card(id);
                    if (held == nullptr || !in_suit(*held, static_cast<int>(suit)) ||
                        (held->kind() != CardKind::Normal &&
                         held->kind() != CardKind::Floating)) {
                        continue;
                    }
                    const long long rank = held->rank().millionths();
                    if (rank > side_link && rank < candidate_rank) {
                        ++intermediate_count;
                        intermediate_holder = opponent;
                    }
                }
            }
            if (side_link >= 0 && intermediate_count == 1) {
                int holder_length = 0;
                for (const CardId id : position.hand(intermediate_holder)) {
                    const Card* held = position.card(id);
                    if (held != nullptr &&
                        in_suit(*held, static_cast<int>(suit))) {
                        ++holder_length;
                    }
                }
                drops_singleton = holder_length == 1;
            }
        }
        // Attacking a suit the opponents still hold stoppers in is slow: price
        // the trick they keep winning there.
        {
            const int theirs_here = suit_quick(position, static_cast<int>(suit), me, other);
            score -= 0.8 * static_cast<double>(theirs_here);
        }
        const bool want_high = (top_is_ours && top_seat[suit] == me) || leading_sequence;
        bool better_card = best_card == nullptr ||
            (want_high ? card->rank() > best_card->rank()
                       : card->rank() < best_card->rank());
        if (sequence_top > 0 && best_card != nullptr) {
            const bool wanted = card->rank().millionths() == sequence_top;
            const bool best_wanted = best_card->rank().millionths() == sequence_top;
            if (wanted != best_wanted) better_card = wanted;
        }
        if (best_card != nullptr && suit < 4U &&
            in_suit(*best_card, static_cast<int>(suit)) &&
            drops_singleton != best_drops_singleton) {
            better_card = drops_singleton;
        }
        if (std::getenv("TUONIU_DEBUG_SCORES") != nullptr) {
            std::fprintf(stderr, "  option suit=%d rank=%d score=%.2f\n",
                         static_cast<int>(suit), static_cast<int>(card->rank().millionths()),
                         score);
        }
        if (std::getenv("TUONIU_DEBUG_SCORES") != nullptr) {
            std::fprintf(stderr, "opt suit=%d rank=%d score=%.2f hi=%d seq=%d seqtop=%d topours=%d%c",
                         static_cast<int>(suit),
                         static_cast<int>(card->rank().millionths() / 1'000'000),
                         score, want_high ? 1 : 0,
                         leading_sequence ? 1 : 0,
                         static_cast<int>(sequence_top / 1'000'000),
                         top_is_ours ? 1 : 0,
                         static_cast<char>(10));
        }
        if (score > best_score || (score == best_score && better_card)) {
            best_score = score;
            best = &action;
            best_card = card;
            best_drops_singleton = drops_singleton;
        }
    }
    if (best_score_out != nullptr) *best_score_out = best_score;
    if (best_card_out != nullptr) *best_card_out = best_card;
    return best != nullptr ? *best : legal.front();
}

auto is_bomb(const Card* card) -> bool
{
    return card != nullptr && card->kind() == CardKind::CherryBomb;
}

auto is_plus_two(const Card* card) -> bool
{
    return card != nullptr && card->kind() == CardKind::PlusTwo;
}

auto suit_code(const Card& card) -> int
{
    const auto suits = card.intrinsic_suits();
    if (suits.empty()) return -1;
    switch (suits.front()) {
        case Suit::Spades: return 0;
        case Suit::Hearts: return 1;
        case Suit::Diamonds: return 2;
        case Suit::Clubs: return 3;
        default: return -1;
    }
}

auto in_suit(const Card& card, const int code) -> bool
{
    if (code < 0) return false;
    const Suit want = static_cast<Suit>(code);
    for (const Suit suit : card.intrinsic_suits()) {
        if (suit == want) return true;
    }
    return false;
}

auto next_seat_of(const Seat seat) -> Seat
{
    return static_cast<Seat>((static_cast<int>(seat) + 1) % 4);
}

// The rank the trick is compared on: a three-flower cat is the lowest card in a
// no-trump contract, everything else uses its printed rank.
auto effective_rank_micro(const Position& position, const Card* card) -> long long
{
    if (card == nullptr) return -1;
    if (position.contract() == Contract::NoTrump &&
        card->kind() == CardKind::ThreeSuitCat) {
        return 999'999;
    }
    return card->rank().millionths();
}

// Seat and card that own the trick as it stands, following the engine: highest
// effective rank, and a cherry bomb hands the trick to the seat after it.
struct TrickLeader {
    const TrickPlay* play = nullptr;
    const Card* card = nullptr;
    Seat seat = Seat::North;
};

auto trick_leader(const Position& position) -> TrickLeader
{
    TrickLeader leader;
    const Trick& trick = position.current_trick();
    const TrickPlay* lead_play = nullptr;
    for (const TrickPlay& play : trick) {
        if (!play.action.is_pass()) {
            lead_play = &play;
            break;
        }
    }
    if (lead_play == nullptr) return leader;
    const Card* lead_card = position.card(*lead_play->action.card_id());
    const bool wild_lead = lead_card != nullptr &&
        (lead_card->kind() == CardKind::FourSuit ||
         lead_card->intrinsic_suits().size() == 4U);
    int best_class = 0;
    long long best_rank = -1;
    for (const TrickPlay& play : trick) {
        if (play.action.is_pass()) continue;
        const Card* card = position.card(*play.action.card_id());
        if (card == nullptr) continue;
        // Mirrors the engine: a discard never wins, trump outranks the led
        // suit, and a wild lead puts every card in the same class.
        int card_class = 0;
        if (wild_lead) {
            card_class = 1;
        } else if (acts_as_trump(position, *card)) {
            card_class = 3;
        } else if (lead_card != nullptr &&
                   lead_card->kind() == CardKind::ThreeSuitCat) {
            card_class = 1;
        } else {
            card_class = play.action.effective_suit() ==
                    lead_play->action.effective_suit() ? 2 : 0;
        }
        if (card_class == 0) continue;
        const long long rank = effective_rank_micro(position, card);
        if (leader.card == nullptr || card_class > best_class ||
            (card_class == best_class && rank > best_rank)) {
            leader.play = &play;
            leader.card = card;
            leader.seat = play.seat;
            best_class = card_class;
            best_rank = rank;
        }
    }
    if (leader.card != nullptr && leader.card->kind() == CardKind::CherryBomb) {
        leader.seat = next_seat_of(leader.seat);
    }
    return leader;
}

auto card_rank_micro(const Card* card) -> long long
{
    return card == nullptr ? -1 : card->rank().millionths();
}

// Seats that still act after `me` in the trick being played.
auto seats_behind(const Position& position, const Seat me) -> std::array<bool, 4>
{
    std::array<bool, 4> behind{};
    behind.fill(true);
    for (const TrickPlay& play : position.current_trick()) {
        behind[static_cast<std::size_t>(play.seat)] = false;
    }
    behind[static_cast<std::size_t>(me)] = false;
    return behind;
}

// Highest rank an opponent still to play can put on this suit. A seat void in
// the suit that holds a trump-like card can beat anything, which no rank can
// express, so it is reported through ruff_out.
auto opponents_behind_best(
    const Position& position,
    const std::array<bool, 4>& behind,
    const Seat me,
    const int suit_code,
    bool* ruff_out) -> long long
{
    long long best = -1;
    for (int seat = 0; seat < 4; ++seat) {
        if (!behind[static_cast<std::size_t>(seat)]) continue;
        const Seat other = static_cast<Seat>(seat);
        if (other == me || other == partner_of(me)) continue;
        bool holds_suit = false;
        bool holds_trump = false;
        for (const CardId id : position.hand(other)) {
            const Card* card = position.card(id);
            if (card == nullptr) continue;
            if (in_suit(*card, suit_code)) {
                holds_suit = true;
                // A bomb by an opponent hands the trick to the seat after them,
                // which is our side, so it is not something to cover.
                if (card->kind() != CardKind::CherryBomb) {
                    best = std::max<long long>(
                        best, effective_rank_micro(position, card));
                }
            }
            if (acts_as_trump(position, *card) &&
                card->kind() != CardKind::CherryBomb) {
                holds_trump = true;
            }
        }
        if (!holds_suit && holds_trump) *ruff_out = true;
    }
    return best;
}

// True when the leader still holds the rank touching the led card from below,
// i.e. the lead came from a touching sequence (JT9, ...), where covering wastes
// a card that the sequence would have won anyway.
auto leader_holds_touching_lower(
    const Position& position,
    const Seat leader,
    const Card& led,
    const int suit_code) -> bool
{
    const long long led_rank = led.rank().millionths();
    for (const CardId id : position.hand(leader)) {
        const Card* card = position.card(id);
        if (card == nullptr || !in_suit(*card, suit_code)) continue;
        const long long rank = card->rank().millionths();
        if (rank < led_rank && led_rank - rank <= 1'000'000) return true;
    }
    return false;
}

auto heuristic_action(
    const Position& position,
    const int lead_rule,
    const PlayTracker* tracker) -> Action;

// Static quality of being on lead here, for the side that is about to move: the
// best score its own lead candidates reach. Positive means leading is good for
// that side, negative means every lead is poisoned.

// ---- single-suit double dummy with an entry budget ----
auto suit_mask(const Position& position, const int suit_code, const int seat)
    -> std::uint32_t
{
    std::uint32_t mask = 0;
    for (const CardId id : position.hand(static_cast<Seat>(seat))) {
        const Card* card = position.card(id);
        if (card == nullptr || card->kind() != CardKind::Normal) continue;
        if (!in_suit(*card, suit_code)) continue;
        const long long rank = card->rank().millionths() / 1'000'000;
        if (rank < 2 || rank > 14) continue;
        mask |= 1U << static_cast<unsigned>(rank - 2);
    }
    return mask;
}

auto suit_has_nonplain(const Position& position, const int suit_code) -> bool
{
    for (int seat = 0; seat < 4; ++seat) {
        for (const CardId id : position.hand(static_cast<Seat>(seat))) {
            const Card* card = position.card(id);
            if (card == nullptr || !in_suit(*card, suit_code)) continue;
            if (card->kind() != CardKind::Normal) return true;
        }
    }
    return false;
}

// Cards of `seat` in this suit that beat every card the opponents hold there.
auto suit_quick_winners(const Position& position, const Seat seat, const int suit_code)
    -> int
{
    const Seat partner = partner_of(seat);
    long long opponents_best = -1;
    for (int other = 0; other < 4; ++other) {
        const Seat who = static_cast<Seat>(other);
        if (who == seat || who == partner) continue;
        for (const CardId id : position.hand(who)) {
            const Card* card = position.card(id);
            if (card == nullptr || !in_suit(*card, suit_code)) continue;
            opponents_best = std::max<long long>(
                opponents_best, card->rank().millionths());
        }
    }
    int winners = 0;
    for (const CardId id : position.hand(seat)) {
        const Card* card = position.card(id);
        if (card == nullptr || !in_suit(*card, suit_code)) continue;
        if (card->rank().millionths() > opponents_best) ++winners;
    }
    return winners;
}

// Tricks East-West take in this suit with both sides playing optimally.
// `entry_seat` may take the lead `entries` times from another suit; that is the
// only way to change which side of the table leads the suit, which is what a
// finesse is about.
auto suit_dd(const Position& position, const int suit_code, const Seat leader,
             const Seat entry_seat, const int entries) -> int
{
    if (suit_code < 0 || suit_code > 3) return 0;
    // Dense ordering: every ordinary card of the suit (floating cards included,
    // specials excluded) is ranked against the others and given a bridge-like
    // A..2 slot, so the double dummy compares relative order like a real suit.
    std::array<std::uint32_t, 4> masks{};
    {
        std::vector<std::pair<long long, int>> ordered;
        for (int seat = 0; seat < 4; ++seat) {
            for (const CardId id : position.hand(static_cast<Seat>(seat))) {
                const Card* card = position.card(id);
                if (card == nullptr || !in_suit(*card, suit_code)) continue;
                if (card->kind() != CardKind::Normal &&
                    card->kind() != CardKind::Floating) continue;
                ordered.emplace_back(card->rank().millionths(), seat);
            }
        }
        std::sort(ordered.begin(), ordered.end(),
                  [](const auto& a, const auto& b) { return a.first > b.first; });
        for (std::size_t index = 0; index < ordered.size() && index < 13U; ++index) {
            const int slot = 12 - static_cast<int>(index);
            masks[static_cast<std::size_t>(ordered[index].second)] |=
                1U << static_cast<unsigned>(slot);
        }
    }
    if ((masks[0] | masks[1] | masks[2] | masks[3]) == 0U) return 0;

    const auto pack = [](const std::array<std::uint32_t, 4>& m, const int lead,
                         const int budget) {
        std::uint64_t key = 0;
        for (int seat = 0; seat < 4; ++seat) {
            key |= static_cast<std::uint64_t>(m[static_cast<std::size_t>(seat)])
                << (13 * seat);
        }
        key |= static_cast<std::uint64_t>(lead & 3) << 52;
        key |= static_cast<std::uint64_t>(budget & 7) << 54;
        return key;
    };
    std::unordered_map<std::uint64_t, int> memo;
    const int entry = static_cast<int>(entry_seat);

    std::function<int(std::array<std::uint32_t, 4>, int, int)> trick;
    std::function<int(std::array<std::uint32_t, 4>&, int, int, int, int, int)> play;

    play = [&](std::array<std::uint32_t, 4>& m, const int lead, const int budget,
               const int index, const int best_seat, const int best_rank) -> int {
        if (index == 4) {
            const bool ours = best_seat >= 0 &&
                partnership_of(static_cast<Seat>(best_seat)) == Partnership::EastWest;
            return (ours ? 1 : 0) + trick(m, best_seat, budget);
        }
        const int seat = (lead + index) % 4;
        if (m[static_cast<std::size_t>(seat)] == 0U) {
            return play(m, lead, budget, index + 1, best_seat, best_rank);
        }
        const bool ours =
            partnership_of(static_cast<Seat>(seat)) == Partnership::EastWest;
        int best = ours ? -1 : 1000;
        for (int rank = 12; rank >= 0; --rank) {
            const std::uint32_t bit = 1U << static_cast<unsigned>(rank);
            if ((m[static_cast<std::size_t>(seat)] & bit) == 0U) continue;
            m[static_cast<std::size_t>(seat)] &= ~bit;
            const int seat_out = rank > best_rank ? seat : best_seat;
            const int rank_out = rank > best_rank ? rank : best_rank;
            const int value = play(m, lead, budget, index + 1, seat_out, rank_out);
            m[static_cast<std::size_t>(seat)] |= bit;
            if (ours) {
                if (value > best) best = value;
            } else if (value < best) {
                best = value;
            }
        }
        return best;
    };

    trick = [&](const std::array<std::uint32_t, 4> m, const int lead,
                const int budget) -> int {
        if ((m[0] | m[1] | m[2] | m[3]) == 0U) return 0;
        const std::uint64_t key = pack(m, lead, budget);
        const auto found = memo.find(key);
        if (found != memo.end()) return found->second;
        int best = -1;
        if (budget > 0 && m[static_cast<std::size_t>(entry)] != 0U && lead != entry) {
            best = trick(m, entry, budget - 1);
        }
        std::array<std::uint32_t, 4> copy = m;
        const int value = play(copy, lead, budget, 0, -1, -1);
        if (value > best) best = value;
        memo.emplace(key, best);
        return best;
    };

    return trick(masks, static_cast<int>(leader), entries);
}

// Worth of being on lead here: the best this side can get out of one suit,
// priced by compound double dummy with the entries the partner really holds.
// A hand with no entry to the far side cannot repeat a finesse, and the count
// shows it.
auto alive(const int live) -> double { return static_cast<double>(live); }

// Quick tricks in one suit: the run our side takes from `leader` until the
// opponents win a trick. Anything won after that does not count.
auto suit_quick_impl(const Position& position, const int suit, const Seat leader,
                     const Partnership side, std::array<std::uint32_t, 4> masks,
                     int depth) -> int
{
    if (depth > 8) return 0;
    std::uint32_t any = masks[0] | masks[1] | masks[2] | masks[3];
    if (any == 0U) return 0;

    struct Play { int seat; int bit; };
    std::function<int(std::array<std::uint32_t, 4>&, int, int, int, int)> step;
    // (masks, index, best_seat, best_rank, trick_base)
    step = [&](std::array<std::uint32_t, 4>& m, const int index, const int best_seat,
               const int best_rank, const int) -> int {
        if (index == 4) {
            if (best_seat < 0) return 0;
            const bool ours =
                partnership_of(static_cast<Seat>(best_seat)) == side;
            if (!ours) return 0;
            return 1 + suit_quick_impl(position, suit, static_cast<Seat>(best_seat),
                                       side, m, depth + 1);
        }
        const int seat = (static_cast<int>(leader) + index) % 4;
        if (m[static_cast<std::size_t>(seat)] == 0U) {
            return step(m, index + 1, best_seat, best_rank, 0);
        }
        const bool ours = partnership_of(static_cast<Seat>(seat)) == side;
        int best = ours ? -1 : 1000;
        for (int bit = 12; bit >= 0; --bit) {
            const std::uint32_t flag = 1U << static_cast<unsigned>(bit);
            if ((m[static_cast<std::size_t>(seat)] & flag) == 0U) continue;
            m[static_cast<std::size_t>(seat)] &= ~flag;
            const int seat_out = bit > best_rank ? seat : best_seat;
            const int rank_out = bit > best_rank ? bit : best_rank;
            const int value = step(m, index + 1, seat_out, rank_out, 0);
            m[static_cast<std::size_t>(seat)] |= flag;
            if (ours) {
                if (value > best) best = value;
            } else if (value < best) {
                best = value;
            }
        }
        return best < 0 ? 0 : best;
    };

    int best = -1;
    const Seat saved = leader;
    (void)saved;
    std::array<std::uint32_t, 4> copy = masks;
    const int value = step(copy, 0, -1, -1, 0);
    if (value > best) best = value;
    return best < 0 ? 0 : best;
}

auto suit_quick(const Position& position, const int suit, const Seat leader,
                const Partnership side) -> int
{
    std::array<std::uint32_t, 4> masks{};
    for (int seat = 0; seat < 4; ++seat) {
        for (const CardId id : position.hand(static_cast<Seat>(seat))) {
            const Card* card = position.card(id);
            if (card == nullptr || card->kind() != CardKind::Normal) continue;
            if (!in_suit(*card, suit)) continue;
            const long long rank = card->rank().millionths() / 1'000'000;
            if (rank < 2 || rank > 14) continue;
            masks[static_cast<std::size_t>(seat)] |=
                1U << static_cast<unsigned>(rank - 2);
        }
    }
    return suit_quick_impl(position, suit, leader, side, masks, 0);
}

auto side_tricks(const Position& position, const Partnership side) -> int
{
    const Seat a = side == Partnership::EastWest ? Seat::East : Seat::North;
    const Seat b = side == Partnership::EastWest ? Seat::West : Seat::South;
    return static_cast<int>(position.tricks_won(a) + position.tricks_won(b));
}

// Cash-out count with memoisation. Our side keeps counting while it wins; when
// the opponents win a trick the run resumes only if they cannot cash anything
// from their new lead (a sterile win).
auto cash_key(const Position& position, const int leader, const int side) -> std::uint64_t
{
    std::uint64_t hash = 1469598103934665603ULL;
    for (int seat = 0; seat < 4; ++seat) {
        std::vector<std::uint64_t> ids;
        for (const CardId id : position.hand(static_cast<Seat>(seat))) {
            ids.push_back(id.value);
        }
        std::sort(ids.begin(), ids.end());
        hash ^= static_cast<std::uint64_t>(ids.size());
        hash *= 1099511628211ULL;
        for (const std::uint64_t value : ids) {
            hash ^= value;
            hash *= 1099511628211ULL;
        }
    }
    hash ^= static_cast<std::uint64_t>(leader & 3);
    hash *= 1099511628211ULL;
    hash ^= static_cast<std::uint64_t>(side & 1);
    hash *= 1099511628211ULL;
    return hash;
}

auto cash_out(const Position& position, const Partnership side, const int depth,
              std::unordered_map<std::uint64_t, int>& memo) -> int
{
    if (position.is_finished() || depth > 24) return 0;
    const Seat leader = position.next_player();
    const std::uint64_t key = cash_key(position, static_cast<int>(leader), side == Partnership::EastWest ? 0 : 1);
    const auto found = memo.find(key);
    if (found != memo.end()) return found->second;

    const bool ours = partnership_of(leader) == side;
    const std::uint64_t tricks_before = side_tricks(position, side);
    const ActionList legal = position.legal_actions();
    if (legal.empty()) {
        memo[key] = 0;
        return 0;
    }
    int best = ours ? -1 : 1000;
    for (const Action& action : legal) {
        if (action.is_pass() || !action.card_id().has_value()) continue;
        Position copy = position;
        const bool ends = copy.current_trick().size() == 3U;
        if (!copy.apply(action)) continue;
        int value = 0;
        if (ends) {
            const bool we_won = side_tricks(copy, side) > tricks_before;
            if (we_won) {
                value = 1 + cash_out(copy, side, depth + 1, memo);
            } else {
                const Partnership other = side == Partnership::EastWest
                    ? Partnership::NorthSouth : Partnership::EastWest;
                int their_quick = 0;
                for (int probe = 0; probe < 4; ++probe) {
                    their_quick = std::max(their_quick,
                        suit_quick(copy, probe, copy.next_player(), other));
                }
                value = their_quick == 0 ? cash_out(copy, side, depth + 1, memo) : 0;
            }
        } else {
            value = cash_out(copy, side, depth + 1, memo);
        }
        if (ours) {
            if (value > best) best = value;
        } else if (value < best) {
            best = value;
        }
    }
    const int result = best < 0 ? 0 : best;
    memo[key] = result;
    return result;
}

auto available_cash(const Position& position, const Partnership side) -> int
{
    if (partnership_of(position.next_player()) != side) return 0;
    std::unordered_map<std::uint64_t, int> memo;
    return cash_out(position, side, 0, memo);
}

auto lead_quality(const Position& position) -> double
{
    const ActionList legal = position.legal_actions();
    if (legal.empty()) return 0.0;
    const Seat me = position.next_player();
    const Seat partner = partner_of(me);
    int entries = 0;
    for (int suit = 0; suit < 4; ++suit) {
        entries += suit_quick_winners(position, partner, suit);
    }
    entries = std::min(entries, 3);
    double best = -1e9;
    bool priced = false;
    for (int suit = 0; suit < 4; ++suit) {
        const int ours = suit_dd(position, suit, me, partner, entries);
        int live = 0;
        for (int seat = 0; seat < 4; ++seat) {
            for (const CardId id : position.hand(static_cast<Seat>(seat))) {
                const Card* card = position.card(id);
                if (card != nullptr && in_suit(*card, suit)) ++live;
            }
        }
        const double centred = static_cast<double>(ours) - 0.25 * alive(live);
        if (centred > best) {
            best = centred;
            priced = true;
        }
    }
    if (!priced) {
        double score = 0.0;
        full_info_lead(position, legal, &score, nullptr);
        return score;
    }
    return best;
}

// A rank-15 card played earlier in the trick stops a later bomb from winning,
// so the bomb's redirect never happens. That only helps when the bomb belongs
// to the partner and the partner is about to be forced to play it: throwing it
// would otherwise hand the trick to the seat after them, which is an opponent.
// Against an opponent's bomb it is wasted work - their bomb already gives us
// the trick - so the rule must not spend a 15 there.
// A block card forces the seat after the blocker to pass. It only counts when
// it can be played with its effect intact, which means following the led suit
// or ruffing it with a block of the trump suit; a block discarded off-suit has
// no effect at all.
auto holds_block_of(
    const Position& position,
    const Seat seat,
    const Suit led,
    const std::optional<Suit> trump) -> bool
{
    bool void_in_led = true;
    for (const CardId id : position.hand(seat)) {
        const Card* card = position.card(id);
        if (card != nullptr && card->intrinsic_suits().size() == 1U &&
            card->intrinsic_suits().front() == led) {
            void_in_led = false;
        }
    }
    for (const CardId id : position.hand(seat)) {
        const Card* card = position.card(id);
        if (card == nullptr || card->kind() != CardKind::Block) continue;
        if (card->intrinsic_suits().size() != 1U) continue;
        const Suit suit = card->intrinsic_suits().front();
        if (suit == led) return true;
        if (trump.has_value() && suit == *trump && void_in_led) return true;
    }
    return false;
}

auto partner_bomb_forced(const Position& position, const Suit led) -> bool
{
    const Seat partner_seat = partner_of(position.next_player());
    const int step = position.direction() == Direction::Forward ? 1 : 3;
    const Seat before_partner = static_cast<Seat>(
        (static_cast<int>(partner_seat) + 4 - step) % 4);
    // The partner is blocked out of the trick, so the bomb never comes out.
    if (holds_block_of(position, before_partner, led, trump_of(position.contract()))) {
        return false;
    }
    const Seat partner = partner_of(position.next_player());
    const auto& hand = position.hand(partner);
    if (hand.empty()) return false;
    bool saw_bomb = false;
    std::size_t suit_cards = 0;
    std::size_t others = 0;
    for (const CardId id : hand) {
        const Card* card = position.card(id);
        if (card == nullptr) continue;
        const bool bomb = card->kind() == CardKind::CherryBomb;
        const bool in_suit = card->intrinsic_suits().size() == 1U &&
            card->intrinsic_suits().front() == led;
        if (bomb && in_suit) {
            saw_bomb = true;
            ++suit_cards;
        } else if (in_suit) {
            ++suit_cards;
        } else {
            ++others;
        }
    }
    if (!saw_bomb) return false;
    // Forced either because it is the only card left, or because it is the only
    // card the partner holds in the suit being led and following is mandatory.
    if (hand.size() == 1U) return true;
    return suit_cards == 1U && others > 0;
}

auto dominates_bomb(const Position& position, const Card& card) -> bool
{
    if (card.rank().millionths() < 15 * 1'000'000) return false;
    if (card.kind() == CardKind::CherryBomb) return false;
    if (card.intrinsic_suits().size() != 1U) return false;
    return partner_bomb_forced(position, card.intrinsic_suits().front());
}

// A bomb sitting in our partner's hand is a dormant liability: it costs a trick
// as soon as the opponents lead its suit while the bomb is the partner's only
// card of that suit. It only materialises when the partner is actually made to
// play, so a block card held by the seat before the partner cancels it.
auto bomb_liability(const Position& position, const Seat our_partner) -> double
{
    const auto& hand = position.hand(our_partner);
    if (hand.empty()) return 0.0;
    const int step = position.direction() == Direction::Forward ? 1 : 3;
    const Seat before_partner = static_cast<Seat>(
        (static_cast<int>(our_partner) + 4 - step) % 4);
    for (const CardId id : hand) {
        const Card* card = position.card(id);
        if (card == nullptr || card->kind() != CardKind::CherryBomb) continue;
        if (card->intrinsic_suits().size() != 1U) continue;
        const Suit suit = card->intrinsic_suits().front();
        // Forced only when the bomb is the partner's only card of the suit.
        std::size_t suit_cards = 0;
        for (const CardId other : hand) {
            const Card* held = position.card(other);
            if (held == nullptr || held->intrinsic_suits().size() != 1U) continue;
            if (held->intrinsic_suits().front() == suit) ++suit_cards;
        }
        if (suit_cards != 1U) continue;
        // The side on lead has to be able to lead the suit.
        const Seat leader = position.next_player();
        bool leader_holds = false;
        for (const CardId other : position.hand(leader)) {
            const Card* held = position.card(other);
            if (held == nullptr || held->intrinsic_suits().size() != 1U) continue;
            if (held->intrinsic_suits().front() == suit) leader_holds = true;
        }
        if (!leader_holds) continue;
        // A block in front of the partner suppresses the bomb entirely.
        if (holds_block_of(position, before_partner, suit, trump_of(position.contract()))) {
            continue;
        }
        return 1.0;
    }
    return 0.0;
}

// Play a candidate lead out with all four hands, score the trick, then add a
// tempo term for who has to lead next. Constant work per candidate.
// Tricks the two sides would take if this suit were led out on its own, from
// the cards still held. Positive is good for East-West. Used to price the lead
// that concedes a trick: giving one away only pays when the rest of the suit
// comes back with interest.
auto suit_run_balance(
    const Position& position,
    const int suit_code,
    const Seat me) -> int
{
    const Seat partner = partner_of(me);
    std::vector<std::pair<long long, bool>> cards;
    for (int seat = 0; seat < 4; ++seat) {
        for (const CardId id : position.hand(static_cast<Seat>(seat))) {
            const Card* card = position.card(id);
            if (card == nullptr || !in_suit(*card, suit_code)) continue;
            const Seat owner = static_cast<Seat>(seat);
            const bool ours = owner == me || owner == partner;
            cards.emplace_back(card->rank().millionths(), ours);
        }
    }
    std::sort(cards.begin(), cards.end(),
              [](const auto& a, const auto& b) { return a.first > b.first; });
    int balance = 0;
    for (std::size_t index = 0; index < cards.size(); index += 4U) {
        const std::size_t stop = std::min<std::size_t>(index + 4U, cards.size());
        bool ours = cards[index].second;
        for (std::size_t inner = index + 1U; inner < stop; ++inner) {
            if (cards[inner].first == cards[index].first) continue;
            ours = cards[inner].second;
            break;
        }
        balance += ours ? 1 : -1;
    }
    return balance;
}

// Winners our side still has in this suit after the trick, counting only cards
// that beat every card the opponents hold in it. This is what "give first, take
// later" buys: driving out their stopper promotes the leftovers.
auto promoted_side_winners(
    const Position& position,
    const int suit_code,
    const Seat me) -> int
{
    const Seat partner = partner_of(me);
    long long opponents_best = -1;
    for (int seat = 0; seat < 4; ++seat) {
        const Seat other = static_cast<Seat>(seat);
        if (other == me || other == partner) continue;
        for (const CardId id : position.hand(other)) {
            const Card* card = position.card(id);
            if (card == nullptr || !in_suit(*card, suit_code)) continue;
            opponents_best = std::max<long long>(
                opponents_best, effective_rank_micro(position, card));
        }
    }
    int winners = 0;
    for (int seat = 0; seat < 4; ++seat) {
        const Seat other = static_cast<Seat>(seat);
        if (other != me && other != partner) continue;
        for (const CardId id : position.hand(other)) {
            const Card* card = position.card(id);
            if (card == nullptr || !in_suit(*card, suit_code)) continue;
            if (effective_rank_micro(position, card) > opponents_best) ++winners;
        }
    }
    return winners;
}

auto trick_value(const Position& position, const Action& lead) -> double
{
    Position copy = position;
    const Seat me = position.next_player();
    const Seat partner = partner_of(me);
    const auto our_tricks = [me, partner](const Position& p) {
        return static_cast<int>(p.tricks_won(me) + p.tricks_won(partner));
    };
    const int before = our_tricks(copy);
    if (!copy.apply(lead)) return -1e9;
    int guard = 0;
    while (copy.current_trick().size() < 4U && guard < 8) {
        ++guard;
        const ActionList legal = copy.legal_actions();
        if (legal.empty()) break;
        const Action next = heuristic_action(copy, 0, nullptr);
        if (!copy.apply(next)) break;
    }
    const int delta = our_tricks(copy) - before;
    double value = delta > 0 ? 1.0 : -1.0;
    // A conceded trick is only worth its price if the same suit pays us back
    // later; if the opponents hold the rest of the suit, the gift is worse than
    // the bare trick.
    if (delta <= 0 && lead.card_id().has_value()) {
        const Card* led = position.card(*lead.card_id());
        const int led_suit = led != nullptr ? suit_code(*led) : -1;
        if (led_suit >= 0) {
            const int balance = suit_run_balance(copy, led_suit, me);
            if (balance < 0) value += 0.6 * static_cast<double>(balance);
        }
    }
    // Winner leads next, so value the position it has to lead from.
    const double quality = lead_quality(copy);
    const Seat leader = copy.next_player();
    const bool we_lead = leader == me || leader == partner;
    value += (we_lead ? 1.0 : -1.0) * 0.35 * quality;
    // If the opponents take the lead with our partner still holding a forced
    // bomb, that bomb is worth a trick to them unless we covered it here.
    if (!we_lead) {
        value -= bomb_liability(copy, partner);
    }
    return value;
}

auto heuristic_action(
    const Position& position,
    const int lead_rule,
    const PlayTracker* tracker) -> Action
{
    const Seat me = position.next_player();
    const Seat partner = partner_of(me);
    const ActionList legal = position.legal_actions();
    if (legal.empty()) {
        return Action::pass();
    }
    if (legal.size() == 1U) {
        return legal.front();
    }

    const Trick& trick = position.current_trick();
    const auto card_for = [&](const Action& action) -> const Card* {
        return action.is_pass() ? nullptr : card_of(position, *action.card_id());
    };

    if (trick.empty() && lead_rule == 3) {
        return full_info_lead(position, legal);
    }
    if (trick.empty() && lead_rule == 4) {
        const Action* best = nullptr;
        double best_value = -1e9;
        for (const Action& action : legal) {
            if (action.is_pass() || !action.card_id().has_value()) continue;
            const Card* card = position.card(*action.card_id());
            if (card == nullptr) continue;
            double value = trick_value(position, action);
            if (dominates_bomb(position, *card)) value += 0.4;

            if (value > best_value) {
                best_value = value;
                best = &action;
            }
        }
        return best != nullptr ? *best : legal.front();
    }
    if (trick.empty()) {
        // Leading. With the play tracker we know which seats have shown out of
        // a suit: leading a suit an opponent is void in hands it a ruff, and
        // leading one the partner is void in gives the partner a ruff.
        if (tracker != nullptr) {
            const Seat partner_seat = partner;
            std::array<bool, 5> opponent_void{};
            std::array<bool, 5> partner_void{};
            for (std::size_t suit = 0; suit < 4U; ++suit) {
                for (int seat = 0; seat < 4; ++seat) {
                    const Seat other = static_cast<Seat>(seat);
                    if (other == me || other == partner_seat) {
                        if (other == partner_seat) {
                            partner_void[suit] =
                                partner_void[suit] ||
                                !tracker->seen_suit[static_cast<std::size_t>(seat)][suit];
                        }
                        continue;
                    }
                    if (!tracker->seen_suit[static_cast<std::size_t>(seat)][suit]) {
                        opponent_void[suit] = true;
                    }
                }
            }
            // Prefer a suit the partner is void in (a ruff), then any suit an
            // opponent is not void in.
            const Action* ruff_lead = nullptr;
            const Action* safe_lead = nullptr;
            for (const Action& action : legal) {
                const Card* card = card_for(action);
                if (card == nullptr || card->intrinsic_suits().size() != 1U) continue;
                const std::size_t suit =
                    static_cast<std::size_t>(card->intrinsic_suits().front());
                if (suit >= 4U) continue;
                if (opponent_void[suit]) continue;
                if (partner_void[suit] && ruff_lead == nullptr) {
                    ruff_lead = &action;
                }
                if (safe_lead == nullptr) safe_lead = &action;
            }
            if (ruff_lead != nullptr) return *ruff_lead;
            if (safe_lead != nullptr) {
                // Among safe suits keep the original preference: the highest
                // card of the suit.
                const Card* best = nullptr;
                const Action* best_action = nullptr;
                for (const Action& action : legal) {
                    const Card* card = card_for(action);
                    if (card == nullptr || card->intrinsic_suits().size() != 1U) continue;
                    const std::size_t suit =
                        static_cast<std::size_t>(card->intrinsic_suits().front());
                    if (suit >= 4U || opponent_void[suit]) continue;
                    if (best == nullptr || card->rank() > best->rank()) {
                        best = card;
                        best_action = &action;
                    }
                }
                if (best_action != nullptr) return *best_action;
            }
        }
        // Three candidate rules, measured against double dummy:
        // 0 cash the highest card, 1 lead the lowest card, 2 lead the lowest
        // card of the longest suit.
        if (lead_rule == 2) {
            std::array<int, 5> length{};
            for (const Action& action : legal) {
                const Card* card = card_for(action);
                if (card == nullptr || card->intrinsic_suits().empty()) continue;
                length[static_cast<std::size_t>(card->intrinsic_suits().front())] += 1;
            }
            const Action* chosen = nullptr;
            const Card* chosen_card = nullptr;
            for (const Action& action : legal) {
                const Card* card = card_for(action);
                if (card == nullptr || card->intrinsic_suits().empty()) continue;
                const std::size_t suit =
                    static_cast<std::size_t>(card->intrinsic_suits().front());
                if (chosen_card == nullptr) {
                    chosen = &action;
                    chosen_card = card;
                    continue;
                }
                const std::size_t best_suit =
                    static_cast<std::size_t>(chosen_card->intrinsic_suits().front());
                if (length[suit] > length[best_suit] ||
                    (length[suit] == length[best_suit] &&
                     card->rank() < chosen_card->rank())) {
                    chosen = &action;
                    chosen_card = card;
                }
            }
            if (chosen != nullptr) return *chosen;
            return legal.front();
        }
        const Card* best_lead = nullptr;
        for (const Action& action : legal) {
            const Card* card = card_for(action);
            if (card == nullptr) continue;
            const bool better = lead_rule == 0
                ? (best_lead == nullptr || card->rank() > best_lead->rank())
                : (best_lead == nullptr || card->rank() < best_lead->rank());
            if (better) best_lead = card;
        }
        if (best_lead != nullptr) {
            for (const Action& action : legal) {
                const Card* card = card_for(action);
                if (card != nullptr && card->id() == best_lead->id()) return action;
            }
        }
        return legal.front();
    }

    // Following. Who is winning right now?
    const TrickLeader leader = trick_leader(position);
    const bool partner_winning = leader.play != nullptr && leader.seat == partner;
    const Card* winning_card = leader.card;
    int led_suit_for_follow = -1;
    for (const TrickPlay& play : trick) {
        if (play.action.is_pass()) continue;
        const Card* led = card_for(play.action);
        led_suit_for_follow = led != nullptr ? suit_code(*led) : -1;
        break;
    }

    // Cheapest card that beats the current winner; lowest card otherwise.
    const Action* cheapest_winner = nullptr;
    const Card* cheapest_winner_card = nullptr;
    const Action* plus_two_winner = nullptr;
    const Card* plus_two_winner_card = nullptr;
    const Action* lowest = nullptr;
    const Card* lowest_card = nullptr;
    const Action* lowest_bomb = nullptr;
    const Card* lowest_bomb_card = nullptr;

    // ---- what a discard costs ----
    // The ranking follows what the card actually does for us:
    //   1. a stopper, or the last small card guarding it: dropping it hands the
    //      opponents their run in that suit;
    //   2. my own trick: a card above every card they hold in the suit;
    //   3. everything else, where the lowest card is the right one to let go.
    std::array<int, 4> their_count{};
    std::array<long long, 4> their_best{};
    std::array<int, 4> my_len{};
    std::array<long long, 4> my_best{};
    std::array<long long, 4> partner_best{};
    std::array<long long, 4> side_best{};
    std::array<int, 4> their_run{};
    std::array<int, 4> legacy_their_run{};
    {
        their_best.fill(-1);
        my_best.fill(-1);
        partner_best.fill(-1);
        side_best.fill(-1);
        for (int seat = 0; seat < 4; ++seat) {
            const Seat who = static_cast<Seat>(seat);
            for (const CardId id : position.hand(who)) {
                const Card* card = position.card(id);
                if (card == nullptr || card->kind() == CardKind::CherryBomb ||
                    card->kind() == CardKind::PlusTwo) {
                    continue;
                }
                const int suit = suit_code(*card);
                if (suit < 0) continue;
                const long long rank = effective_rank_micro(position, card);
                if (who == me) {
                    ++my_len[suit];
                    my_best[suit] = std::max(my_best[suit], rank);
                } else if (who == partner) {
                    partner_best[suit] = std::max(partner_best[suit], rank);
                } else {
                    ++their_count[suit];
                    their_best[suit] = std::max(their_best[suit], rank);
                }
            }
        }
        // Cards they cash before my stopper bites in that suit.
        for (int suit = 0; suit < 4; ++suit) {
            side_best[suit] = std::max(my_best[suit], partner_best[suit]);
            int run = 0;
            int legacy_run = 0;
            for (int seat = 0; seat < 4; ++seat) {
                const Seat who = static_cast<Seat>(seat);
                if (who == me || who == partner) continue;
                for (const CardId id : position.hand(who)) {
                    const Card* card = position.card(id);
                    if (card == nullptr || card->kind() == CardKind::CherryBomb ||
                        card->kind() == CardKind::PlusTwo) {
                        continue;
                    }
                    if (suit_code(*card) != suit) continue;
                    const long long rank = effective_rank_micro(position, card);
                    if (rank > side_best[suit]) ++run;
                    if (rank > my_best[suit]) ++legacy_run;
                }
            }
            their_run[suit] = run;
            legacy_their_run[suit] = legacy_run;
        }
    }
    // A sure trick of my own is an entry: without one I can never get the lead
    // to play a link towards the partner's winners.
    bool i_have_entry = false;
    for (int suit = 0; suit < 4; ++suit) {
        if (my_best[suit] > their_best[suit]) i_have_entry = true;
    }
    const auto discard_cost = [&](const Card* card) -> double {
        if (card == nullptr) return 1e9;
        const int suit = suit_code(*card);
        if (suit < 0) return 1e9;
        const long long rank = effective_rank_micro(position, card);
        double cost = static_cast<double>(rank) / 1'000'000.0;
        if (std::getenv("TUONIU_DISCARD_PARTNERSHIP_OFF") != nullptr) {
            const bool legacy_stopper = legacy_their_run[suit] >= 1 &&
                my_len[suit] > legacy_their_run[suit];
            if (their_count[suit] > 0 && rank > their_best[suit]) {
                cost += 10.0;
            } else if (legacy_stopper &&
                       my_len[suit] == legacy_their_run[suit] + 1) {
                cost += 11.0;
            } else if (legacy_stopper && rank == my_best[suit]) {
                cost += 12.0;
            }
            if (my_len[suit] == 1 && partner_best[suit] > their_best[suit] &&
                i_have_entry) {
                cost += 6.0;
            }
            return cost;
        }
        const bool i_hold_side_stopper = my_best[suit] > partner_best[suit];
        const bool stopper = i_hold_side_stopper && their_run[suit] >= 1 &&
            my_len[suit] > their_run[suit];
        const bool self_contained_length_winner = i_hold_side_stopper &&
            my_best[suit] > their_best[suit] && my_len[suit] > their_count[suit] &&
            rank < my_best[suit];
        if (their_count[suit] > 0 && rank > their_best[suit]) {
            // My own trick: nothing they hold in the suit beats it.
            cost += 10.0;
        } else if (self_contained_length_winner) {
            // I hold both the partnership's top card and enough length to
            // exhaust every opposing card without needing an entry to the
            // partner. The remaining small cards are established winners too.
            cost += 10.0;
        } else if (stopper && my_len[suit] == their_run[suit] + 1) {
            // The last small card holding the stopper up.
            cost += 11.0;
        } else if (stopper && rank == my_best[suit]) {
            // The stopper itself.
            cost += 12.0;
        }
        if (my_len[suit] == 1 && partner_best[suit] > their_best[suit] && i_have_entry) {
            // My only card in a suit the partner can cash: drop it and the
            // partner's winners are cut off whenever I hold the lead.
            cost += 6.0;
        }
        return cost;
    };
    const Action* dullest = nullptr;
    double dullest_cost = 1e9;
    for (const Action& action : legal) {
        const Card* card = card_for(action);
        if (card == nullptr) continue;
        const double cost = discard_cost(card);
        if (cost < dullest_cost) {
            dullest_cost = cost;
            dullest = &action;
        }
    }
    // Void in the led suit: the throw-away is the least useful card. Otherwise
    // the fallback is the lowest card of the suit being followed.
    const bool void_in_led = led_suit_for_follow >= 0 && std::none_of(
        position.hand(me).begin(), position.hand(me).end(),
        [&](const CardId id) {
            const Card* card = position.card(id);
            return card != nullptr && in_suit(*card, led_suit_for_follow);
        });
    // Functional specials (bombs, +2, block, reverse, cat, wild cards) change
    // what a trick is worth, so the ranking only reads ordinary and floating
    // cards; other decks keep the lowest-card throw-away.
    bool special_deal = false;
    for (int seat = 0; seat < 4; ++seat) {
        for (const CardId id : position.hand(static_cast<Seat>(seat))) {
            const Card* card = position.card(id);
            if (card != nullptr && card->kind() != CardKind::Normal &&
                card->kind() != CardKind::Floating) {
                special_deal = true;
            }
        }
    }

    for (const Action& action : legal) {
        const Card* card = card_for(action);
        if (card == nullptr) continue;
        if (lowest_card == nullptr || card->rank() < lowest_card->rank()) {
            lowest_card = card;
            lowest = &action;
        }
        if (is_bomb(card) &&
            (lowest_bomb_card == nullptr || card->rank() < lowest_bomb_card->rank())) {
            lowest_bomb_card = card;
            lowest_bomb = &action;
        }
        // A card of another suit is a discard: it cannot win by rank.
        if (led_suit_for_follow >= 0 && !in_suit(*card, led_suit_for_follow) &&
            !acts_as_trump(position, *card)) {
            continue;
        }
        if (winning_card == nullptr || !beats(*card, *winning_card)) continue;
        // A bomb that "wins" hands the trick to the seat after the thrower, and
        // that seat is always an opponent because partnerships sit opposite, so
        // a bomb is never a card to win with. A +2 hands the next seat two
        // cards, so it is only used when nothing else beats the winner.
        if (is_bomb(card)) {
            continue;
        } else if (is_plus_two(card)) {
            if (plus_two_winner_card == nullptr ||
                card->rank() < plus_two_winner_card->rank()) {
                plus_two_winner_card = card;
                plus_two_winner = &action;
            }
        } else if (cheapest_winner_card == nullptr ||
                   card->rank() < cheapest_winner_card->rank()) {
            cheapest_winner_card = card;
            cheapest_winner = &action;
        }
    }

    // The throw-away is the least useful card when the hand cannot follow;
    // otherwise the fallback is the lowest card of the suit being followed.
    const bool special_blocked = special_deal &&
        std::getenv("TUONIU_DISCARD_FORCE") == nullptr;
    const Action* throw_away = (void_in_led && !special_blocked &&
        std::getenv("TUONIU_DISCARD_OFF") == nullptr) ? dullest : lowest;

    // A winning bomb played by the opponents already redirects the trick to
    // our side. Do not trump it or spend a rank above it: that would suppress
    // the redirect and turn a donated trick back into an ordinary contest.
    if (winning_card != nullptr && is_bomb(winning_card) &&
        leader.play != nullptr &&
        partnership_of(leader.play->seat) != partnership_of(me) &&
        partnership_of(leader.seat) == partnership_of(me) &&
        std::getenv("TUONIU_OPPONENT_BOMB_DUCK_OFF") == nullptr) {
        const bool bomb_is_trump = acts_as_trump(position, *winning_card);
        const Action* preserve_redirect = nullptr;
        const Card* preserve_card = nullptr;
        for (const Action& action : legal) {
            const Card* card = card_for(action);
            if (card == nullptr || is_bomb(card)) continue;
            const bool card_is_trump = acts_as_trump(position, *card);
            const bool overtakes =
                (card_is_trump && !bomb_is_trump) ||
                (card_is_trump == bomb_is_trump &&
                 action.mode() != PlayMode::Discard &&
                 effective_rank_micro(position, card) >
                     effective_rank_micro(position, winning_card));
            if (overtakes) continue;
            if (preserve_card == nullptr ||
                effective_rank_micro(position, card) <
                    effective_rank_micro(position, preserve_card)) {
                preserve_card = card;
                preserve_redirect = &action;
            }
        }
        if (preserve_redirect != nullptr) return *preserve_redirect;
    }

    // ---- partnership follow rules ----
    // The led suit, who led, and how far the trick has got.
    const Card* led_card = nullptr;
    Seat leader_seat = me;
    int plays_before = 0;
    for (const TrickPlay& play : trick) {
        if (play.action.is_pass()) continue;
        ++plays_before;
        if (led_card == nullptr) {
            led_card = card_of(position, *play.action.card_id());
            leader_seat = play.seat;
        }
    }
    const int led_suit = led_card != nullptr ? suit_code(*led_card) : -1;
    if (led_suit >= 0) {
        const std::array<bool, 4> behind = seats_behind(position, me);
        int opponents_behind = 0;
        for (int seat = 0; seat < 4; ++seat) {
            if (!behind[static_cast<std::size_t>(seat)]) continue;
            if (static_cast<Seat>(seat) != partner) ++opponents_behind;
        }
        bool ruff_behind = false;
        const long long behind_best =
            opponents_behind_best(position, behind, me, led_suit, &ruff_behind);
        const long long top_now = effective_rank_micro(position, winning_card);

        // Cheapest card that holds the trick even against the seats behind.
        const Action* safe = nullptr;
        const Card* safe_card = nullptr;
        if (!ruff_behind) {
            for (const Action& action : legal) {
                const Card* card = card_for(action);
                if (card == nullptr || is_bomb(card) || is_plus_two(card)) continue;
                // A card of another suit is a discard: it never holds the
                // trick, however high it ranks.
                if (!in_suit(*card, led_suit) && !acts_as_trump(position, *card)) {
                    continue;
                }
                const long long rank = effective_rank_micro(position, card);
                if (rank <= top_now || rank <= behind_best) continue;
                if (safe_card == nullptr ||
                    rank < effective_rank_micro(position, safe_card)) {
                    safe_card = card;
                    safe = &action;
                }
            }
        }

        const bool last_to_play = opponents_behind == 0;
        if (last_to_play) {
            // Fourth hand: the cheapest card that beats what is on the table.
            if (partner_winning) {
                const Action* low_non_bomb = nullptr;
                const Card* low_non_bomb_card = nullptr;
                for (const Action& action : legal) {
                    const Card* card = card_for(action);
                    if (card == nullptr || is_bomb(card)) continue;
                    if (low_non_bomb_card == nullptr ||
                        effective_rank_micro(position, card) <
                            effective_rank_micro(position, low_non_bomb_card)) {
                        low_non_bomb_card = card;
                        low_non_bomb = &action;
                    }
                }
                if (low_non_bomb != nullptr) return *low_non_bomb;
                return throw_away != nullptr ? *throw_away : legal.front();
            }
            if (safe != nullptr) return *safe;
            if (cheapest_winner != nullptr) return *cheapest_winner;
            if (plus_two_winner != nullptr) return *plus_two_winner;
            if (lowest_bomb != nullptr) return *lowest_bomb;
            return throw_away != nullptr ? *throw_away : legal.front();
        }

        // Third hand can still profitably spend a high card that cannot hold
        // the trick: it forces the fourth hand's higher control now. This is
        // useful both when the partner's lead is still winning provisionally
        // and when the second hand has already overtaken it.
        const bool third_hand = leader_seat == partner && plays_before == 2;
        const Action* forcing_cover = nullptr;
        const Card* forcing_cover_card = nullptr;
        if (third_hand && safe == nullptr && !ruff_behind &&
            std::getenv("TUONIU_THIRD_HAND_COVER_OFF") == nullptr) {
            for (const Action& action : legal) {
                const Card* card = card_for(action);
                if (card == nullptr || is_bomb(card) || is_plus_two(card)) continue;
                if (!in_suit(*card, led_suit) && !acts_as_trump(position, *card)) {
                    continue;
                }
                const long long rank = effective_rank_micro(position, card);
                if (rank <= top_now) continue;
                if (forcing_cover_card == nullptr ||
                    rank > effective_rank_micro(position, forcing_cover_card)) {
                    forcing_cover_card = card;
                    forcing_cover = &action;
                }
            }
            const Card* duck = throw_away != nullptr ? card_for(*throw_away) : nullptr;
            if (forcing_cover_card == nullptr || duck == nullptr ||
                card_rank_micro(duck) >= 9'000'000 ||
                card_rank_micro(forcing_cover_card) < 9'000'000) {
                forcing_cover = nullptr;
            }
            // Spending an honour is useful only when it makes fourth hand use
            // a strictly higher winner. If partner's current card and our
            // proposed cover both force the same card, keep the cover.
            if (forcing_cover != nullptr) {
                const long long forcing_rank =
                    effective_rank_micro(position, forcing_cover_card);
                std::optional<long long> response_to_partner;
                std::optional<long long> response_to_cover;
                for (int seat = 0; seat < 4; ++seat) {
                    if (!behind[static_cast<std::size_t>(seat)] ||
                        static_cast<Seat>(seat) == partner) {
                        continue;
                    }
                    for (const CardId id : position.hand(static_cast<Seat>(seat))) {
                        const Card* held = position.card(id);
                        if (held == nullptr || !in_suit(*held, led_suit)) continue;
                        const long long rank = effective_rank_micro(position, held);
                        if (rank > top_now &&
                            (!response_to_partner.has_value() ||
                             rank < *response_to_partner)) {
                            response_to_partner = rank;
                        }
                        if (rank > forcing_rank &&
                            (!response_to_cover.has_value() ||
                             rank < *response_to_cover)) {
                            response_to_cover = rank;
                        }
                    }
                }
                if (!response_to_partner.has_value() ||
                    !response_to_cover.has_value() ||
                    *response_to_cover <= *response_to_partner) {
                    forcing_cover = nullptr;
                }
            }
            // Do not spend the only cover for a card retained by an opponent
            // who has already played to this trick. Forcing the fourth hand's
            // higher card would promote that cross-hand intermediate control.
            if (forcing_cover != nullptr) {
                const long long forcing_rank =
                    effective_rank_micro(position, forcing_cover_card);
                for (int seat = 0; seat < 4 && forcing_cover != nullptr; ++seat) {
                    const Seat who = static_cast<Seat>(seat);
                    if (who == me || who == partner ||
                        behind[static_cast<std::size_t>(seat)]) {
                        continue;
                    }
                    for (const CardId id : position.hand(who)) {
                        const Card* held = position.card(id);
                        if (held == nullptr || !in_suit(*held, led_suit)) continue;
                        const long long rank = effective_rank_micro(position, held);
                        if (rank > top_now && rank < forcing_rank) {
                            forcing_cover = nullptr;
                            break;
                        }
                    }
                }
            }
        }

        if (partner_winning) {
            if (ruff_behind || behind_best > top_now) {
                // Otherwise take it over with the cheapest card that holds, and
                // if none holds, force the fourth hand's control when useful.
                if (safe != nullptr) return *safe;
                if (forcing_cover != nullptr) return *forcing_cover;
                return throw_away != nullptr ? *throw_away : legal.front();
            }
            // A bomb outranks the partner's card and redirects the trick to the
            // seat after the thrower, so it would give this trick away.
            const Action* low_non_bomb = nullptr;
            const Card* low_non_bomb_card = nullptr;
            for (const Action& action : legal) {
                const Card* card = card_for(action);
                if (card == nullptr || is_bomb(card)) continue;
                if (low_non_bomb_card == nullptr ||
                    effective_rank_micro(position, card) <
                        effective_rank_micro(position, low_non_bomb_card)) {
                    low_non_bomb_card = card;
                    low_non_bomb = &action;
                }
            }
            if (low_non_bomb != nullptr) return *low_non_bomb;
            return throw_away != nullptr ? *throw_away : legal.front();
        }

        const bool second_hand = leader_seat != partner && plays_before == 1;
        if (second_hand) {
            // Second hand ducks in tempo. It covers only an honour that is not
            // the tail of a touching sequence, or when the suit is nearly gone.
            int suit_length = 0;
            for (const CardId id : position.hand(me)) {
                const Card* card = position.card(id);
                if (card != nullptr && in_suit(*card, led_suit)) ++suit_length;
            }
            const bool honour = card_rank_micro(led_card) >= 10'000'000;
            const bool from_sequence = led_card != nullptr &&
                leader_holds_touching_lower(position, leader_seat, *led_card, led_suit);
            // Covering a low card is right when a higher card of ours stays
            // behind the cover: the fourth hand must then spend an honour, and
            // we still control the suit.
            int covered_and_held = 0;
            if (cheapest_winner_card != nullptr) {
                for (const CardId id : position.hand(me)) {
                    const Card* card = position.card(id);
                    if (card != nullptr && in_suit(*card, led_suit) &&
                        card->rank() > cheapest_winner_card->rank()) {
                        ++covered_and_held;
                    }
                }
            }
            if (cheapest_winner != nullptr && covered_and_held > 0) {
                return *cheapest_winner;
            }
            if (honour && (!from_sequence || suit_length <= 2) &&
                cheapest_winner != nullptr) {
                return *cheapest_winner;
            }
            return throw_away != nullptr ? *throw_away : legal.front();
        }

        // Third hand (the partner led): stop the fourth hand from winning the
        // trick cheaply. Hold it if a card can, otherwise do not spend a card
        // that the fourth hand beats anyway.
        if (safe != nullptr) return *safe;
        // A floating point is just another rank here. Use the highest forcing
        // card rather than another spot card that exerts no pressure at all.
        if (forcing_cover != nullptr) return *forcing_cover;
        return throw_away != nullptr ? *throw_away : legal.front();
    }

    if (partner_winning) {
        // Dump a bomb while the partner is taking the trick: it is harmless as
        // a discard and stops being a liability later.
        if (lowest_bomb != nullptr) return *lowest_bomb;
        return throw_away != nullptr ? *throw_away : legal.front();
    }
    if (cheapest_winner != nullptr) {
        return *cheapest_winner;
    }
    if (plus_two_winner != nullptr) {
        return *plus_two_winner;
    }
    if (lowest_bomb != nullptr) return *lowest_bomb;
    return lowest != nullptr ? *lowest : legal.front();
}

struct Diagnose {
    // categories: leading, following-partner-winning, following-opponent-winning
    std::array<std::uint64_t, 3> decisions{};
    std::array<std::uint64_t, 3> regret{};
    std::array<std::uint64_t, 3> mistakes{};
    std::uint64_t discard_decisions = 0;
    std::uint64_t discard_regret = 0;
    std::uint64_t discard_mistakes = 0;
};

auto is_discard_decision(const Position& position) -> bool
{
    if (position.current_trick().empty()) return false;
    bool has_discard = false;
    for (const Action& action : position.legal_actions()) {
        if (action.is_pass()) continue;
        if (action.mode() == PlayMode::Follow) return false;
        if (action.mode() == PlayMode::Discard) has_discard = true;
    }
    return has_discard;
}

auto category_of(const Position& position) -> int
{
    if (position.current_trick().empty()) return 0;
    const Seat me = position.next_player();
    const Seat partner = partner_of(me);
    const TrickLeader leader = trick_leader(position);
    if (leader.play != nullptr && leader.seat == partner) return 1;
    return 2;
}

}  // namespace


auto suit_letter(const Suit suit) -> char
{
    switch (suit) {
        case Suit::Spades: return 'S';
        case Suit::Hearts: return 'H';
        case Suit::Diamonds: return 'D';
        case Suit::Clubs: return 'C';
        default: return '?';
    }
}

auto rank_text(const Rank rank) -> std::string
{
    const long long micro = rank.millionths();
    if (micro % 1'000'000 != 0) {
        char buffer[32];
        std::snprintf(buffer, sizeof(buffer), "%.3f", static_cast<double>(micro) / 1e6);
        return buffer;
    }
    const long long value = micro / 1'000'000;
    switch (value) {
        case 10: return "T";
        case 11: return "J";
        case 12: return "Q";
        case 13: return "K";
        case 14: return "A";
        default: return std::to_string(value);
    }
}

auto card_text(const Card& card) -> std::string
{
    std::string name;
    switch (card.kind()) {
        case CardKind::Block: name = "BLOCK"; break;
        case CardKind::Reverse: name = "REVERSE"; break;
        case CardKind::PlusTwo: name = "+2"; break;
        case CardKind::CherryBomb: name = "BOMB"; break;
        case CardKind::FourSuit: name = "4SUIT"; break;
        case CardKind::BigJoker: name = "BIGJOKER"; break;
        case CardKind::SmallJoker: name = "SMALLJOKER"; break;
        case CardKind::ThreeSuitCat: name = "CAT"; break;
        case CardKind::NoTrump: name = "NT" + rank_text(card.rank()); break;
        case CardKind::Floating: name = "FL"; break;
        case CardKind::Normal: break;
    }
    // A block, reverse, +2 or bomb is still tied to one suit; name it so the
    // deal cannot be misread.
    for (const Suit suit : card.intrinsic_suits()) {
        if (static_cast<int>(suit) >= 4) continue;
        if (card.kind() != CardKind::Normal) name.push_back('-');
        name.push_back(suit_letter(suit));
    }
    if (card.kind() == CardKind::Normal || card.kind() == CardKind::Floating) {
        name += rank_text(card.rank());
    }
    return name;
}

auto seat_letter(const Seat seat) -> char
{
    switch (seat) {
        case Seat::North: return 'N';
        case Seat::East: return 'E';
        case Seat::South: return 'S';
        case Seat::West: return 'W';
    }
    return '?';
}

auto print_deal(const Position& position) -> void
{
    std::printf("contract %d  trump %s  tricks %u\n",
                static_cast<int>(position.contract()),
                position.contract() == Contract::NoTrump ? "none" : "suit",
                position.tricks_remaining());
    for (int seat = 0; seat < 4; ++seat) {
        const Seat who = static_cast<Seat>(seat);
        std::printf("  %c:", seat_letter(who));
        for (const CardId id : position.hand(who)) {
            const Card* card = position.card(id);
            if (card != nullptr) std::printf(" %s", card_text(*card).c_str());
        }
        std::printf("\n");
    }
}

auto kind_code(const Card& card) -> int
{
    switch (card.kind()) {
        case CardKind::Normal: return 0;
        case CardKind::Floating: return 10;
        case CardKind::CherryBomb: return 1;
        case CardKind::PlusTwo: return 2;
        case CardKind::Block: return 3;
        case CardKind::ThreeSuitCat: return 4;
        case CardKind::BigJoker: return 5;
        case CardKind::SmallJoker: return 6;
        case CardKind::NoTrump: return 7;
        case CardKind::FourSuit: return 8;
        case CardKind::Reverse: return 9;
    }
    return 9;
}

struct SuitTops {
    long long highest = -1;
    long long eWBest = -1;
    long long nSBest = -1;
};

auto suit_tops(const Position& position, const int code) -> SuitTops
{
    SuitTops tops;
    for (int seat = 0; seat < 4; ++seat) {
        for (const CardId id : position.hand(static_cast<Seat>(seat))) {
            const Card* card = position.card(id);
            if (card == nullptr || !in_suit(*card, code)) continue;
            const long long rank = card->rank().millionths();
            if (rank > tops.highest) tops.highest = rank;
            if (seat == 1 || seat == 3) {
                if (rank > tops.eWBest) tops.eWBest = rank;
            } else if (rank > tops.nSBest) {
                tops.nSBest = rank;
            }
        }
    }
    return tops;
}

auto side_length(const Position& position, const int code, const bool ew) -> int
{
    int count = 0;
    for (int seat = 0; seat < 4; ++seat) {
        if (((seat == 1 || seat == 3)) != ew) continue;
        for (const CardId id : position.hand(static_cast<Seat>(seat))) {
            const Card* card = position.card(id);
            if (card != nullptr && in_suit(*card, code)) ++count;
        }
    }
    return count;
}

auto trick_top_rank(const Position& position) -> long long
{
    long long top = -1;
    for (const TrickPlay& play : position.current_trick()) {
        if (play.action.is_pass()) continue;
        const Card* card = position.card(*play.action.card_id());
        if (card != nullptr) {
            top = std::max<long long>(top, card->rank().millionths());
        }
    }
    return top;
}

auto heuristic_choose(const touniu_solver::Position& position, const int policy)
    -> touniu_solver::Action
{
    return heuristic_action(position, policy, nullptr);
}

#if !defined(TOUNIU_HEURISTIC_POLICY_ONLY)

auto main(const int argc, char** argv) -> int
{
    const Args args = parse_args(argc, argv);
    std::uint64_t total_loss = 0;
    std::uint64_t total_dd = 0;
    std::uint64_t total_actual = 0;
    int counted = 0;
    Diagnose diagnose;
    PlayTracker tracker;

    for (int deal_index = 0; deal_index < args.deals; ++deal_index) {
        const Scenario scenario = build_scenario(
            args, args.seed + static_cast<std::uint32_t>(deal_index));
        auto position = Position::create(scenario);
        if (!position.has_value()) continue;

        // Double-dummy value for EW from the opening position.
        auto dd_position = Position::create(scenario);
        const int dd_value = solve(*dd_position, Partnership::EastWest, true).tricks;
        int bombs_ew = 0, bombs_ns = 0, free_ew = 0, free_ns = 0, cats_ew = 0, cats_ns = 0;
        int jokers_ew = 0, jokers_ns = 0, nt_ew = 0, nt_ns = 0;
        int blocks_ew = 0, blocks_ns = 0, rev_ew = 0, rev_ns = 0, four_ew = 0, four_ns = 0;
        for (int seat = 0; seat < 4; ++seat) {
            const bool ew = seat == 0 || seat == 2;
            for (const CardId id : position->hand(static_cast<Seat>(seat))) {
                const Card* card = position->card(id);
                if (card == nullptr) continue;
                if (card->kind() == CardKind::CherryBomb) (ew ? bombs_ew : bombs_ns) += 1;
                if (card->kind() == CardKind::PlusTwo) (ew ? free_ew : free_ns) += 1;
                if (card->kind() == CardKind::ThreeSuitCat) (ew ? cats_ew : cats_ns) += 1;
                if (card->kind() == CardKind::BigJoker ||
                    card->kind() == CardKind::SmallJoker) {
                    (ew ? jokers_ew : jokers_ns) += 1;
                }
                if (card->kind() == CardKind::NoTrump) (ew ? nt_ew : nt_ns) += 1;
                if (card->kind() == CardKind::Block) (ew ? blocks_ew : blocks_ns) += 1;
                if (card->kind() == CardKind::Reverse) (ew ? rev_ew : rev_ns) += 1;
                if (card->kind() == CardKind::FourSuit) (ew ? four_ew : four_ns) += 1;
            }
        }

        const Diagnose diagnose_before = diagnose;
        if (args.show == deal_index) {
            std::printf("=== deal %d ===\n", deal_index);
            print_deal(*position);
        }
        int guard = 0;
        bool first_done = false;
        int trick_number = 1;
        while (!position->is_finished() && guard < 400) {
            ++guard;
            const Seat mover = position->next_player();
            std::optional<Action> action;
            if (!args.first_card.empty() && !first_done) {
                first_done = true;
                for (const Action& candidate_action : position->legal_actions()) {
                    if (candidate_action.is_pass() ||
                        !candidate_action.card_id().has_value()) {
                        continue;
                    }
                    const Card* candidate_card =
                        position->card(*candidate_action.card_id());
                    if (candidate_card != nullptr &&
                        card_matches_spec(*candidate_card, args.first_card)) {
                        action = candidate_action;
                        break;
                    }
                }
                if (!action.has_value()) {
                    std::fprintf(stderr, "--first-card %s is not legal\n",
                                 args.first_card.c_str());
                }
            }
            if (!action.has_value() &&
                partnership_of(mover) == Partnership::EastWest) {
                const bool leading_now = position->current_trick().empty();
                if ((args.dd_leads && leading_now) ||
                    (args.dd_follows && !leading_now)) {
                    auto probe = *position;
                    const SearchResult result =
                        solve(probe, Partnership::EastWest, true);
                    action = result.best_action;
                } else {
                    const ActionList legal_before = position->legal_actions();
                    (void) legal_before;
                    const auto heur_start = std::chrono::steady_clock::now();
                    action = heuristic_action(
                    *position,
                    args.simulate ? 4 : (args.full_info ? 3 : args.lead_rule),
                    args.void_aware ? &tracker : nullptr);
                    if (std::getenv("TUONIU_DEBUG_SCORES") != nullptr) {
                        const auto heur_end = std::chrono::steady_clock::now();
                        const auto micros = std::chrono::duration_cast<std::chrono::microseconds>(
                            heur_end - heur_start).count();
                        std::fprintf(stderr, "heur cards=%d us=%lld%c",
                                     static_cast<int>(position->tricks_remaining()),
                                     static_cast<long long>(micros),
                                     static_cast<char>(10));
                    }
                    if (args.signals && action.has_value()) {
                        action = convention_pick(
                            *position,
                            *action,
                            position->legal_actions(),
                            leading_now);
                    }
                }
                if (!action.has_value()) return 1;
                if (args.per_card) {
                    // Every legal card of this decision, priced against the
                    // best one: exactly "how many tricks does it give away".
                    Position probe = *position;
                    const int best_value =
                        solve(probe, Partnership::EastWest, true).tricks;
                    std::printf("trick %d %c to play:\n",
                                static_cast<int>(position->tricks_remaining()),
                                seat_letter(mover));
                    for (const Action& candidate : position->legal_actions()) {
                        int value = best_value;
                        if (candidate.is_pass()) {
                            if (position->apply(candidate)) {
                                Position after = *position;
                                value = solve(after, Partnership::EastWest, true).tricks;
                                position->undo();
                            }
                        } else if (candidate.card_id().has_value()) {
                            if (position->apply(candidate)) {
                                Position after = *position;
                                value = solve(after, Partnership::EastWest, true).tricks;
                                position->undo();
                            }
                        }
                        const Card* played = candidate.is_pass()
                            ? nullptr : position->card(*candidate.card_id());
                        std::printf("  %-12s %2d  (-%d)\n",
                                    played != nullptr ? card_text(*played).c_str() : "pass",
                                    value, best_value - value);
                    }
                }
if (args.diagnose) {
                    const int category = category_of(*position);
                    const bool discard_decision = is_discard_decision(*position);
                    // Both values are the final EW trick count from this point.
                    const int best = solve(*position, Partnership::EastWest, true).tricks;
                    int chosen = best;
                    if (position->apply(*action)) {
                        chosen = solve(*position, Partnership::EastWest, true).tricks;
                        position->undo();
                    }
                    if (args.show == deal_index && best > chosen) {
                        const Card* picked_now =
                            action->is_pass() || !action->card_id().has_value()
                            ? nullptr : position->card(*action->card_id());
                        Position best_copy_now = *position;
                        const SearchResult best_result_now =
                            solve(best_copy_now, Partnership::EastWest, true);
                        const Card* better_now =
                            best_result_now.best_action.has_value() &&
                                !best_result_now.best_action->is_pass()
                            ? position->card(*best_result_now.best_action->card_id())
                            : nullptr;
                        static const char* const names[3] = {
                            "lead", "follow-partner", "follow-opponent"};
                        std::printf("  !! %s T%d: value %d best %d | played %s | dd %s\n",
                                    names[category],
                                    args.cards -
                                        static_cast<int>(position->tricks_remaining()) + 1,
                                    chosen, best,
                                    picked_now != nullptr ? card_text(*picked_now).c_str() : "pass",
                                    better_now != nullptr ? card_text(*better_now).c_str() : "pass");
                    }
                    const bool record_mistake = best > chosen &&
                        args.show != deal_index &&
                        (args.per_deal || (category == 0 && diagnose.mistakes[0] < 6U));
                    if (record_mistake) {
                        const Card* picked =
                            action->is_pass() || !action->card_id().has_value()
                            ? nullptr : position->card(*action->card_id());
                        Position best_copy = *position;
                        const SearchResult best_result =
                            solve(best_copy, Partnership::EastWest, true);
                        const Card* better =
                            best_result.best_action.has_value() &&
                                !best_result.best_action->is_pass()
                            ? position->card(*best_result.best_action->card_id())
                            : nullptr;
                        const int trick = args.cards -
                            static_cast<int>(position->tricks_remaining()) + 1;
                        if (!args.per_deal) {
                            std::printf(
                                "  !! %s %d: value %d best %d | played %s | dd %s\n",
                                category == 0 ? "lead" : "follow", trick, chosen, best,
                                picked != nullptr ? card_text(*picked).c_str() : "pass",
                                better != nullptr ? card_text(*better).c_str() : "pass");
                        } else if (category == 0) {
                            const int cs = picked != nullptr ? suit_code(*picked) : -1;
                            const int ds = better != nullptr ? suit_code(*better) : -1;
                            const SuitTops ct = suit_tops(*position, cs);
                            const SuitTops dt = suit_tops(*position, ds);
                            const long long cr = picked != nullptr
                                ? picked->rank().millionths() : -1;
                            const long long dr = better != nullptr
                                ? better->rank().millionths() : -1;
                            std::printf(
                                "LM trick=%d regret=%d same=%d cs=%d cr=%lld ckind=%d"
                                " ctop=%d clost=%d chole=%d ds=%d dr=%lld dkind=%d dtop=%d"
                                " cwlen=%d csl=%d dwlen=%d dsl=%d nsbest=%lld\n",
                                trick, best - chosen, cs == ds, cs, cr,
                                picked != nullptr ? kind_code(*picked) : -1,
                                picked != nullptr && cr == ct.highest,
                                cs >= 0 && ct.nSBest > cr,
                                cs >= 0 && cr == ct.highest,
                                ds, dr,
                                better != nullptr ? kind_code(*better) : -1,
                                better != nullptr && dr == dt.highest,
                                side_length(*position, cs, true),
                                side_length(*position, cs, false),
                                side_length(*position, ds, true),
                                side_length(*position, ds, false),
                                ct.nSBest);
                        } else {
                            const long long top = trick_top_rank(*position);
                            const int ps = picked != nullptr ? suit_code(*picked) : -1;
                            const SuitTops pt = suit_tops(*position, ps);
                            const long long pr = picked != nullptr
                                ? picked->rank().millionths() : -1;
                            const long long dr = better != nullptr
                                ? better->rank().millionths() : -1;
                            std::printf(
                                "FM cat=%d trick=%d regret=%d pk=%d pr=%lld ptop=%d"
                                " pwin=%d pwaste=%d dk=%d dr=%lld dwin=%d\n",
                                category, trick, best - chosen,
                                picked != nullptr ? kind_code(*picked) : -1, pr,
                                picked != nullptr && pr == pt.highest,
                                pr > top, picked != nullptr && pr == pt.highest,
                                better != nullptr ? kind_code(*better) : -1, dr,
                                dr > top);
                        }
                    }
                    diagnose.decisions[static_cast<std::size_t>(category)] += 1;
                    diagnose.regret[static_cast<std::size_t>(category)] +=
                        static_cast<std::uint64_t>(std::max(0, best - chosen));
                    if (best > chosen) {
                        diagnose.mistakes[static_cast<std::size_t>(category)] += 1;
                    }
                    if (discard_decision) {
                        diagnose.discard_decisions += 1;
                        diagnose.discard_regret +=
                            static_cast<std::uint64_t>(std::max(0, best - chosen));
                        if (best > chosen) diagnose.discard_mistakes += 1;
                    }
                }
            } else if (!action.has_value()) {
                const SearchResult result =
                    solve(*position, Partnership::NorthSouth, true);
                action = result.best_action;
            }
            if (!action.has_value()) break;
            if (args.show == deal_index) {
                const Card* shown = action->is_pass() || !action->card_id().has_value()
                    ? nullptr : position->card(*action->card_id());
                std::printf("  T%d %c: %s\n", trick_number, seat_letter(mover),
                            shown != nullptr ? card_text(*shown).c_str() : "pass");
            }
            tracker.observe(*position, *action);
            const bool ended_trick = position->current_trick().size() == 3U;
            if (!position->apply(*action)) break;
            if (ended_trick) {
                ++trick_number;
            }
        }

        const int aw = static_cast<int>(position->tricks_won(Seat::West));
        const int ae = static_cast<int>(position->tricks_won(Seat::East));
        const int actual = aw + ae;
        total_dd += static_cast<std::uint64_t>(dd_value);
        total_actual += static_cast<std::uint64_t>(actual);
        const int loss = std::max(0, dd_value - actual);
        total_loss += static_cast<std::uint64_t>(loss);
        ++counted;
        if (args.per_deal) {
            std::printf(
                "{\"loss\":%d,\"dd\":%d,\"actual\":%d,\"contract\":%d,"
                "\"bombsEw\":%d,\"bombsNs\":%d,\"freeEw\":%d,\"freeNs\":%d,"
                "\"catsEw\":%d,\"catsNs\":%d,"
                "\"jokersEw\":%d,\"jokersNs\":%d,\"ntEw\":%d,\"ntNs\":%d,"
                "\"blocksEw\":%d,\"blocksNs\":%d,\"revEw\":%d,\"revNs\":%d,"
                "\"fourEw\":%d,\"fourNs\":%d,"
                "\"leadDecisions\":%llu,\"leadRegret\":%llu,"
                "\"partnerDecisions\":%llu,\"partnerRegret\":%llu,"
                "\"opponentDecisions\":%llu,\"opponentRegret\":%llu,"
                "\"discardDecisions\":%llu,\"discardRegret\":%llu}\n",
                loss, dd_value, actual, static_cast<int>(scenario.contract),
                bombs_ew, bombs_ns, free_ew, free_ns, cats_ew, cats_ns,
                jokers_ew, jokers_ns, nt_ew, nt_ns,
                blocks_ew, blocks_ns, rev_ew, rev_ns, four_ew, four_ns,
                diagnose.decisions[0] - diagnose_before.decisions[0],
                diagnose.regret[0] - diagnose_before.regret[0],
                diagnose.decisions[1] - diagnose_before.decisions[1],
                diagnose.regret[1] - diagnose_before.regret[1],
                diagnose.decisions[2] - diagnose_before.decisions[2],
                diagnose.regret[2] - diagnose_before.regret[2],
                diagnose.discard_decisions - diagnose_before.discard_decisions,
                diagnose.discard_regret - diagnose_before.discard_regret);
        }
        if (args.verbose) {
            std::printf(
                "deal %d: dd %d actual %d loss %d\n",
                deal_index, dd_value, actual, loss);
        }
    }

    if (counted == 0) {
        std::printf("{\"deals\":0}\n");
        return 1;
    }
    std::printf(
        "{\"deals\":%d,\"cards\":%d,\"specials\":%d,\"ddTricks\":%.3f,"
        "\"actualTricks\":%.3f,\"meanLoss\":%.3f}\n",
        counted, args.cards, args.specials,
        static_cast<double>(total_dd) / counted,
        static_cast<double>(total_actual) / counted,
        static_cast<double>(total_loss) / counted);
    if (args.diagnose) {
        const char* names[3] = {"leading", "partner-winning", "opponent-winning"};
        for (std::size_t index = 0; index < 3; ++index) {
            const std::uint64_t decisions = diagnose.decisions[index];
            std::printf(
                "  %-16s decisions %4llu  mistakes %4llu  regret %6.3f/decision\n",
                names[index],
                static_cast<unsigned long long>(decisions),
                static_cast<unsigned long long>(diagnose.mistakes[index]),
                decisions == 0 ? 0.0
                               : static_cast<double>(diagnose.regret[index]) / decisions);
        }
        std::printf(
            "  %-16s decisions %4llu  mistakes %4llu  regret %6.3f/decision\n",
            "discard",
            static_cast<unsigned long long>(diagnose.discard_decisions),
            static_cast<unsigned long long>(diagnose.discard_mistakes),
            diagnose.discard_decisions == 0 ? 0.0
                : static_cast<double>(diagnose.discard_regret) /
                    diagnose.discard_decisions);
    }
    return 0;
}

#endif  // TOUNIU_HEURISTIC_POLICY_ONLY
