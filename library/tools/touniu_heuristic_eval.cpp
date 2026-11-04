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
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

#include <touniu_solver/position.hpp>
#include <touniu_solver/search.hpp>

namespace {

using namespace touniu_solver;

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
        }
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
    if (specials <= 0) {
        return deck;
    }
    const std::size_t base = deck.size();
    for (const Suit suit : suits) {
        deck.push_back(Card::block(CardId{id++}, suit));
        deck.push_back(Card::reverse(CardId{id++}, suit));
        deck.push_back(Card::plus_two(CardId{id++}, suit));
        deck.push_back(Card::cherry_bomb(CardId{id++}, suit));
    }
    for (const int rank : {5, 7, 9, 11}) {
        deck.push_back(Card::four_suit(CardId{id++}, Rank::integer(rank)));
    }
    deck.push_back(Card::big_joker(CardId{id++}));
    deck.push_back(Card::small_joker(CardId{id++}));
    deck.push_back(Card::three_suit_cat(CardId{id++}));
    for (const int rank : {4, 6, 8, 10, 12}) {
        deck.push_back(Card::no_trump(CardId{id++}, Rank::integer(rank)));
    }
    if (static_cast<std::size_t>(specials) < deck.size() - base) {
        deck.erase(
            deck.begin() + static_cast<std::ptrdiff_t>(base + static_cast<std::size_t>(specials)),
            deck.end());
    }
    return deck;
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

auto heuristic_action(const Position& position, const int lead_rule) -> Action
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

    if (trick.empty()) {
        // Leading. Three candidate rules, measured against double dummy:
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
    const TrickPlay* winner = nullptr;
    for (const TrickPlay& play : trick) {
        if (play.action.is_pass()) continue;
        if (winner == nullptr) {
            winner = &play;
            continue;
        }
        const Card* have = card_of(position, *winner->action.card_id());
        const Card* other = card_of(position, *play.action.card_id());
        if (have == nullptr || other == nullptr) continue;
        if (other->rank() > have->rank()) winner = &play;
    }
    const bool partner_winning = winner != nullptr && winner->seat == partner;
    const Card* winning_card =
        winner == nullptr ? nullptr : card_of(position, *winner->action.card_id());

    // Cheapest card that beats the current winner; lowest card otherwise.
    const Action* cheapest_winner = nullptr;
    const Card* cheapest_winner_card = nullptr;
    const Action* lowest = nullptr;
    const Card* lowest_card = nullptr;
    for (const Action& action : legal) {
        const Card* card = card_for(action);
        if (card == nullptr) continue;
        if (lowest_card == nullptr || card->rank() < lowest_card->rank()) {
            lowest_card = card;
            lowest = &action;
        }
        if (winning_card != nullptr && beats(*card, *winning_card) &&
            (cheapest_winner_card == nullptr || card->rank() < cheapest_winner_card->rank())) {
            cheapest_winner_card = card;
            cheapest_winner = &action;
        }
    }

    if (partner_winning) {
        return lowest != nullptr ? *lowest : legal.front();
    }
    if (cheapest_winner != nullptr) {
        return *cheapest_winner;
    }
    return lowest != nullptr ? *lowest : legal.front();
}

struct Diagnose {
    // categories: leading, following-partner-winning, following-opponent-winning
    std::array<std::uint64_t, 3> decisions{};
    std::array<std::uint64_t, 3> regret{};
    std::array<std::uint64_t, 3> mistakes{};
};

auto category_of(const Position& position) -> int
{
    if (position.current_trick().empty()) return 0;
    const Seat me = position.next_player();
    const Seat partner = partner_of(me);
    // Whoever is currently winning the trick.
    const TrickPlay* winner = nullptr;
    for (const TrickPlay& play : position.current_trick()) {
        if (play.action.is_pass()) continue;
        if (winner == nullptr) {
            winner = &play;
            continue;
        }
        const Card* have = position.card(*winner->action.card_id());
        const Card* other = position.card(*play.action.card_id());
        if (have != nullptr && other != nullptr && other->rank() > have->rank()) {
            winner = &play;
        }
    }
    if (winner != nullptr && winner->seat == partner) return 1;
    return 2;
}

}  // namespace

auto main(const int argc, char** argv) -> int
{
    const Args args = parse_args(argc, argv);
    std::uint64_t total_loss = 0;
    std::uint64_t total_dd = 0;
    std::uint64_t total_actual = 0;
    int counted = 0;
    Diagnose diagnose;

    for (int deal_index = 0; deal_index < args.deals; ++deal_index) {
        const Scenario scenario = build_scenario(
            args, args.seed + static_cast<std::uint32_t>(deal_index));
        auto position = Position::create(scenario);
        if (!position.has_value()) continue;

        // Double-dummy value for EW from the opening position.
        auto dd_position = Position::create(scenario);
        const int dd_value = solve(*dd_position, Partnership::EastWest, true).tricks;

        int guard = 0;
        while (!position->is_finished() && guard < 400) {
            ++guard;
            const Seat mover = position->next_player();
            std::optional<Action> action;
            if (partnership_of(mover) == Partnership::EastWest) {
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
                    action = heuristic_action(*position, args.lead_rule);
                    if (args.signals && action.has_value()) {
                        action = convention_pick(
                            *position,
                            *action,
                            position->legal_actions(),
                            leading_now);
                    }
                }
                if (!action.has_value()) return 1;
                if (args.diagnose) {
                    const int category = category_of(*position);
                    // Both values are the final EW trick count from this point.
                    const int best = solve(*position, Partnership::EastWest, true).tricks;
                    int chosen = best;
                    if (position->apply(*action)) {
                        chosen = solve(*position, Partnership::EastWest, true).tricks;
                        position->undo();
                    }
                    if (category == 0 && best > chosen && diagnose.mistakes[0] < 6U) {
                        const Card* picked = action->is_pass()
                            ? nullptr : position->card(*action->card_id());
                        Position best_copy = *position;
                        const SearchResult best_result =
                            solve(best_copy, Partnership::EastWest, true);
                        const Card* better =
                            best_result.best_action.has_value() &&
                                !best_result.best_action->is_pass()
                            ? position->card(*best_result.best_action->card_id())
                            : nullptr;
                        const int trick = 9 - static_cast<int>(position->tricks_remaining());
                        std::printf("  lead %d: value %d best %d | ", trick, chosen, best);
                        if (picked != nullptr) {
                            std::printf(
                                "picked suit %d rank %d | ",
                                static_cast<int>(picked->intrinsic_suits().front()),
                                picked->rank().millionths() / 1000000);
                        } else {
                            std::printf("picked pass | ");
                        }
                        if (better != nullptr) {
                            std::printf(
                                "dd suit %d rank %d\n",
                                static_cast<int>(better->intrinsic_suits().front()),
                                better->rank().millionths() / 1000000);
                        } else {
                            std::printf("dd pass\n");
                        }
                    }
                    diagnose.decisions[static_cast<std::size_t>(category)] += 1;
                    diagnose.regret[static_cast<std::size_t>(category)] +=
                        static_cast<std::uint64_t>(std::max(0, best - chosen));
                    if (best > chosen) {
                        diagnose.mistakes[static_cast<std::size_t>(category)] += 1;
                    }
                }
            } else {
                const SearchResult result =
                    solve(*position, Partnership::NorthSouth, true);
                action = result.best_action;
            }
            if (!action.has_value()) break;
            if (!position->apply(*action)) break;
        }

        const int aw = static_cast<int>(position->tricks_won(Seat::West));
        const int ae = static_cast<int>(position->tricks_won(Seat::East));
        const int actual = aw + ae;
        total_dd += static_cast<std::uint64_t>(dd_value);
        total_actual += static_cast<std::uint64_t>(actual);
        const int loss = std::max(0, dd_value - actual);
        total_loss += static_cast<std::uint64_t>(loss);
        ++counted;
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
    }
    return 0;
}
