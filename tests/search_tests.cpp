#define MRHENDERSON_TESTING
#include "../src/main.cpp"
#include <random>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

Board position(const std::string& fen) {
    Board board;
    require(board.set_from_fen(fen), "Invalid test position");
    return board;
}

void play(Board& board, const std::string& uci) {
    const Move parsed = parse_uci_move(uci);
    for (const Move& move : board.generate_legal()) {
        if (!same_move(parsed, move)) continue;
        Undo undo;
        board.make_move(move, undo);
        board.position_history.push_back(board.repetition_key());
        return;
    }
    throw std::runtime_error("Illegal test move: " + uci);
}

struct SearchTests {
    static void prepare(Searcher& searcher, const Board& board) {
        searcher.stop = nullptr;
        searcher.time_limit_ms = 0;
        searcher.aborted = false;
        searcher.nodes = 0;
        searcher.prepare_history(board);
    }

    static void terminal_positions() {
        Searcher searcher;
        for (int clock : {0, 100}) {
            Board mate = position("7k/6Q1/6K1/8/8/8/8/8 b - - " + std::to_string(clock) + " 1");
            prepare(searcher, mate);
            require(searcher.quiescence(mate, -INF, INF, 5) == -MATE_SCORE + 5,
                "Quiescence must recognize mate before a draw claim");
            require(searcher.negamax(mate, 2, -INF, INF, 5) == -MATE_SCORE + 5,
                "Main search must recognize mate before a draw claim");
        }
        Board stalemate = position("7k/5Q2/6K1/8/8/8/8/8 b - - 0 1");
        prepare(searcher, stalemate);
        require(searcher.quiescence(stalemate, 100, 200, 0) == 0,
            "Stalemate must override stand pat and the alpha bound");
        Board evasion = position("4r1k1/8/8/8/8/8/8/4K3 w - - 0 1");
        prepare(searcher, evasion);
        const int score = searcher.quiescence(evasion, -INF, INF, 0);
        require(score > -MATE_SCORE + MAX_PLY && score < 0,
            "Quiescence must search quiet check evasions");
    }

    static void repetition_and_draw_context() {
        Searcher searcher;
        Board board;
        for (int cycle = 0; cycle < 2; ++cycle) {
            for (const char* move : {"g1f3", "g8f6", "f3g1", "f6g8"}) play(board, move);
            prepare(searcher, board);
            require(searcher.is_draw(board) == (cycle == 1), "Incorrect repetition count");
        }
        require(searcher.negamax(board, 3, -INF, INF, 0) == 0, "Repetition must score a draw");
        const uint64_t repeated_key = searcher.tt_key(board);
        Board fresh = position(board.to_fen());
        prepare(searcher, fresh);
        require(repeated_key != searcher.tt_key(fresh), "TT must distinguish repetition histories");
        const uint64_t fresh_key = searcher.tt_key(fresh);
        fresh.halfmove_clock = 99;
        prepare(searcher, fresh);
        require(fresh_key != searcher.tt_key(fresh), "TT must distinguish halfmove clocks");
        fresh.halfmove_clock = 100;
        prepare(searcher, fresh);
        require(searcher.negamax(fresh, 3, -INF, INF, 0) == 0, "Fifty-move rule must score a draw");
        play(board, "e2e4");
        prepare(searcher, board);
        require(!searcher.is_draw(board), "Pawn move must reset reversible repetition history");
        const std::string before = board.to_fen();
        const auto keys = searcher.position_keys;
        const auto contexts = searcher.history_contexts;
        Undo undo;
        searcher.push_move(board, board.generate_legal().front(), undo);
        searcher.pop_move(board, undo);
        require(board.to_fen() == before && searcher.position_keys == keys &&
            searcher.history_contexts == contexts, "Search undo must restore draw history");
    }

    static void en_passant_repetition() {
        for (const char* fen : {
            "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1",
            "k3r3/8/8/3pP3/8/8/8/4K3 w - d6 0 1"}) {
            Board board = position(fen);
            const uint64_t ep_key = board.repetition_key();
            board.ep_square = -1;
            board.hash = board.compute_hash();
            require(ep_key == board.repetition_key(), "Unavailable en passant must not affect repetition");
        }
        Board legal_ep = position("k7/8/8/3pP3/8/8/8/4K3 w - d6 0 1");
        const uint64_t ep_key = legal_ep.repetition_key();
        legal_ep.ep_square = -1;
        legal_ep.hash = legal_ep.compute_hash();
        require(ep_key != legal_ep.repetition_key(), "Legal en passant must distinguish repetition");
    }

    static void transpositions_and_mates() {
        Searcher searcher;
        for (int score : {120, MATE_SCORE - 12, -MATE_SCORE + 12}) {
            require(Searcher::score_from_tt(Searcher::score_to_tt(score, 5), 5) == score,
                "TT score round trip failed");
        }
        require(Searcher::score_from_tt(Searcher::score_to_tt(-MATE_SCORE + 12, 5), 9) ==
            -MATE_SCORE + 16, "Losing mate distance must adjust to the probing ply");
        Board board = position("7k/8/5KQ1/8/8/8/8/8 w - - 0 1");
        prepare(searcher, board);
        const int first = searcher.negamax(board, 2, -INF, INF, 3);
        require(first == MATE_SCORE - 4, "Mate in one must outrank other wins");
        require(searcher.negamax(board, 2, -INF, INF, 7) == first - 4,
            "Cached mate distance must adjust to the probing ply");
        searcher.store(17, 50, 1, Searcher::TT_EXACT, Move{});
        const uint64_t collision = 17 + searcher.table.size();
        searcher.store(collision, 1, 2, Searcher::TT_EXACT, Move{});
        require(searcher.probe(collision) != nullptr, "Stale colliding TT entry must be replaceable");
    }

    static void root_personality_safety() {
        std::mt19937 rng(2400);
        Board board;
        for (int sample = 0; sample < 12; ++sample) {
            if (sample > 0) {
                for (int step = 0; step < 3; ++step) {
                    const auto moves = board.generate_legal();
                    require(!moves.empty(), "Unexpected terminal random test position");
                    play(board, move_to_uci(moves[rng() % moves.size()]));
                }
            }
            for (const char* personality : {"Balanced", "Sleepy", "Hungry", "Grumpy"}) {
                Searcher searcher;
                searcher.set_personality(personality);
                searcher.set_tuna_mode(sample % 2 ? 0 : 100);
                SearchLimits limits;
                limits.depth = 2;
                std::atomic<bool> stop{false};
                const SearchResult result = searcher.search(board, limits, stop);
                int best = -INF;
                int selected = -INF;
                Searcher oracle;
                oracle.resize_hash(1);
                for (const Move& move : board.generate_legal()) {
                    oracle.clear_hash();
                    prepare(oracle, board);
                    Board child = board;
                    Undo undo;
                    oracle.push_move(child, move, undo);
                    const int exact = -oracle.negamax(child, limits.depth - 1, -INF, INF, 1);
                    best = std::max(best, exact);
                    if (same_move(move, result.best_move)) selected = exact;
                }
                const int margin = std::abs(best) >= MATE_SCORE - MAX_PLY ? 0 : searcher.style_margin();
                require(selected >= best - margin, "Personality selected an unsafe bound score");
                require(selected == result.score, "Reported root score must be the selected move's exact score");
            }
        }
    }

    static void personality_and_naps() {
        Searcher searcher;
        require(searcher.config.personality == SLEEPY && searcher.config.tuna_mode == 60,
            "Default personality must remain intact");
        searcher.set_personality("Hungry");
        const int hungry = searcher.capture_personality_bonus();
        searcher.set_personality("Sleepy");
        require(hungry > searcher.capture_personality_bonus(), "Hungry must retain its capture preference");
        searcher.set_chatty("Off");
        Board board;
        SearchLimits limits;
        limits.white_time_ms = limits.black_time_ms = 60000;
        searcher.time_limit_ms = 1000;
        require(searcher.can_take_nap(board, limits), "Chat setting must not remove sleepy behavior");
        Board ahead = position("rnb1kbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
        require(searcher.choose_time_limit(ahead, limits) == 1980,
            "A static material lead must not shorten the initial search budget");
        limits.movetime_ms = 1000;
        require(!searcher.can_take_nap(board, limits), "Fixed movetime must remain fixed");
        limits.movetime_ms = 0;
        limits.white_time_ms = 1000;
        require(!searcher.can_take_nap(board, limits), "Naps must use the moving side's clock");
        Board mate = position("7k/8/5KQ1/8/8/8/8/8 w - - 0 1");
        std::atomic<bool> stop{false};
        limits = SearchLimits{};
        limits.depth = 3;
        const SearchResult result = searcher.search(mate, limits, stop);
        play(mate, move_to_uci(result.best_move));
        require(mate.is_in_check(BLACK) && !mate.has_legal_move(), "Personality must preserve immediate mate");
    }

    static void uci_history() {
        std::istringstream input("position startpos moves g1f3 g8f6 f3g1 f6g8 g1f3 g8f6 f3g1 f6g8\n"
            "go depth 2\nquit\n");
        std::ostringstream output;
        auto* old_input = std::cin.rdbuf(input.rdbuf());
        auto* old_output = std::cout.rdbuf(output.rdbuf());
        {
            UciEngine engine;
            engine.loop();
        }
        std::cin.rdbuf(old_input);
        std::cin.clear();
        std::cout.rdbuf(old_output);
        require(output.str().find("info depth 0 score cp 0") != std::string::npos,
            "UCI position history must reach the search thread");
        require(output.str().find("bestmove ") != std::string::npos,
            "A drawn root position must still produce a UCI bestmove");
    }

    static void move_generation() {
        Board start;
        require(perft(start, 4) == 197281, "Start-position perft failed");
        Board kiwipete = position("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1");
        require(perft(kiwipete, 3) == 97862, "Castling and tactical perft failed");
        Board endgame = position("8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1");
        require(perft(endgame, 3) == 2812, "En passant and endgame perft failed");
    }
};

}

int main() {
    try {
        SearchTests::move_generation();
        SearchTests::terminal_positions();
        SearchTests::repetition_and_draw_context();
        SearchTests::en_passant_repetition();
        SearchTests::transpositions_and_mates();
        SearchTests::root_personality_safety();
        SearchTests::personality_and_naps();
        SearchTests::uci_history();
        std::cout << "All search regression tests passed" << std::endl;
    } catch (const std::exception& error) {
        std::cerr << error.what() << std::endl;
        return 1;
    }
}
