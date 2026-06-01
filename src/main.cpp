#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <thread>
#endif

namespace {

constexpr int INF = 32000;
constexpr int MATE_SCORE = 30000;
constexpr int MAX_PLY = 96;

enum Color : int { WHITE = 0, BLACK = 1, NO_COLOR = 2 };
enum PieceType : int { PAWN = 0, KNIGHT = 1, BISHOP = 2, ROOK = 3, QUEEN = 4, KING = 5, NO_PIECE_TYPE = 6 };
enum Piece : int {
    EMPTY = 0,
    WP = 1, WN = 2, WB = 3, WR = 4, WQ = 5, WK = 6,
    BP = 7, BN = 8, BB = 9, BR = 10, BQ = 11, BK = 12
};

enum MoveFlag : uint8_t {
    QUIET = 0,
    CAPTURE = 1 << 0,
    DOUBLE_PUSH = 1 << 1,
    KING_CASTLE = 1 << 2,
    QUEEN_CASTLE = 1 << 3,
    EN_PASSANT = 1 << 4,
    PROMOTION = 1 << 5
};

enum CastlingRight : int {
    WHITE_OO = 1 << 0,
    WHITE_OOO = 1 << 1,
    BLACK_OO = 1 << 2,
    BLACK_OOO = 1 << 3
};

constexpr std::array<int, 6> PIECE_VALUE = {100, 320, 330, 500, 900, 0};
constexpr const char* STANDARD_START_FEN = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

constexpr int file_of(int sq) { return sq & 7; }
constexpr int rank_of(int sq) { return sq >> 3; }
constexpr int make_square(int file, int rank) { return rank * 8 + file; }
constexpr bool on_board(int file, int rank) { return file >= 0 && file < 8 && rank >= 0 && rank < 8; }
constexpr Color opposite(Color c) { return c == WHITE ? BLACK : WHITE; }

Piece make_piece(Color color, PieceType type) {
    if (type == NO_PIECE_TYPE) return EMPTY;
    return static_cast<Piece>(1 + static_cast<int>(type) + (color == BLACK ? 6 : 0));
}

Color color_of(Piece piece) {
    if (piece >= WP && piece <= WK) return WHITE;
    if (piece >= BP && piece <= BK) return BLACK;
    return NO_COLOR;
}

PieceType type_of(Piece piece) {
    if (piece == EMPTY) return NO_PIECE_TYPE;
    int v = static_cast<int>(piece);
    if (v > 6) v -= 6;
    return static_cast<PieceType>(v - 1);
}

char piece_to_char(Piece piece) {
    switch (piece) {
        case WP: return 'P';
        case WN: return 'N';
        case WB: return 'B';
        case WR: return 'R';
        case WQ: return 'Q';
        case WK: return 'K';
        case BP: return 'p';
        case BN: return 'n';
        case BB: return 'b';
        case BR: return 'r';
        case BQ: return 'q';
        case BK: return 'k';
        default: return '.';
    }
}

Piece char_to_piece(char c) {
    switch (c) {
        case 'P': return WP;
        case 'N': return WN;
        case 'B': return WB;
        case 'R': return WR;
        case 'Q': return WQ;
        case 'K': return WK;
        case 'p': return BP;
        case 'n': return BN;
        case 'b': return BB;
        case 'r': return BR;
        case 'q': return BQ;
        case 'k': return BK;
        default: return EMPTY;
    }
}

char promotion_to_char(PieceType piece) {
    switch (piece) {
        case KNIGHT: return 'n';
        case BISHOP: return 'b';
        case ROOK: return 'r';
        case QUEEN: return 'q';
        default: return '\0';
    }
}

PieceType char_to_promotion(char c) {
    switch (std::tolower(static_cast<unsigned char>(c))) {
        case 'n': return KNIGHT;
        case 'b': return BISHOP;
        case 'r': return ROOK;
        case 'q': return QUEEN;
        default: return NO_PIECE_TYPE;
    }
}

std::string square_to_string(int sq) {
    std::string out;
    out.push_back(static_cast<char>('a' + file_of(sq)));
    out.push_back(static_cast<char>('1' + rank_of(sq)));
    return out;
}

int parse_square(const std::string& text) {
    if (text.size() < 2) return -1;
    int file = text[0] - 'a';
    int rank = text[1] - '1';
    if (!on_board(file, rank)) return -1;
    return make_square(file, rank);
}

struct Move {
    int from = -1;
    int to = -1;
    PieceType promotion = NO_PIECE_TYPE;
    uint8_t flags = QUIET;
};

bool same_move(const Move& a, const Move& b) {
    return a.from == b.from && a.to == b.to && a.promotion == b.promotion;
}

bool is_capture(const Move& move) {
    return (move.flags & CAPTURE) != 0;
}

bool is_promotion(const Move& move) {
    return (move.flags & PROMOTION) != 0;
}

std::string move_to_uci(const Move& move) {
    if (move.from < 0 || move.to < 0) return "0000";
    std::string text = square_to_string(move.from) + square_to_string(move.to);
    if (move.promotion != NO_PIECE_TYPE) text.push_back(promotion_to_char(move.promotion));
    return text;
}

Move parse_uci_move(const std::string& text) {
    Move move;
    if (text.size() < 4) return move;
    move.from = parse_square(text.substr(0, 2));
    move.to = parse_square(text.substr(2, 2));
    if (text.size() >= 5) move.promotion = char_to_promotion(text[4]);
    return move;
}

uint64_t splitmix64(uint64_t& seed) {
    uint64_t z = (seed += 0x9e3779b97f4a7c15ULL);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    return z ^ (z >> 31);
}

struct Zobrist {
    std::array<std::array<uint64_t, 64>, 13> piece{};
    uint64_t side = 0;
    std::array<uint64_t, 16> castling{};
    std::array<uint64_t, 8> ep_file{};

    Zobrist() {
        uint64_t seed = 0x20260530d00df00dULL;
        for (auto& by_square : piece) {
            for (uint64_t& key : by_square) key = splitmix64(seed);
        }
        side = splitmix64(seed);
        for (uint64_t& key : castling) key = splitmix64(seed);
        for (uint64_t& key : ep_file) key = splitmix64(seed);
    }
};

const Zobrist& zobrist() {
    static const Zobrist keys;
    return keys;
}

struct Undo {
    Move move;
    Piece captured = EMPTY;
    int castling = 0;
    int ep_square = -1;
    int halfmove_clock = 0;
    int fullmove_number = 1;
    std::array<int, 2> king_square{};
    uint64_t hash = 0;
    Color side_to_move = WHITE;
};

class Board {
public:
    std::array<Piece, 64> squares{};
    Color side_to_move = WHITE;
    int castling = 0;
    int ep_square = -1;
    int halfmove_clock = 0;
    int fullmove_number = 1;
    std::array<int, 2> king_square{{-1, -1}};
    uint64_t hash = 0;

    Board() { set_startpos(); }

    void set_startpos() {
        set_from_fen(STANDARD_START_FEN);
    }

    bool set_from_fen(const std::string& fen) {
        squares.fill(EMPTY);
        king_square = {{-1, -1}};
        side_to_move = WHITE;
        castling = 0;
        ep_square = -1;
        halfmove_clock = 0;
        fullmove_number = 1;

        std::istringstream in(fen);
        std::string board_part;
        std::string active_color;
        std::string castling_part;
        std::string ep_part;
        if (!(in >> board_part >> active_color >> castling_part >> ep_part)) return false;
        if (!(in >> halfmove_clock)) halfmove_clock = 0;
        if (!(in >> fullmove_number)) fullmove_number = 1;

        int rank = 7;
        int file = 0;
        for (char c : board_part) {
            if (c == '/') {
                if (file != 8) return false;
                --rank;
                file = 0;
                continue;
            }
            if (std::isdigit(static_cast<unsigned char>(c))) {
                file += c - '0';
                if (file > 8) return false;
                continue;
            }
            Piece piece = char_to_piece(c);
            if (piece == EMPTY || !on_board(file, rank)) return false;
            int sq = make_square(file, rank);
            squares[sq] = piece;
            if (type_of(piece) == KING) king_square[color_of(piece)] = sq;
            ++file;
        }

        if (rank != 0 || file != 8) return false;
        if (king_square[WHITE] < 0 || king_square[BLACK] < 0) return false;

        side_to_move = active_color == "b" ? BLACK : WHITE;
        if (castling_part.find('K') != std::string::npos) castling |= WHITE_OO;
        if (castling_part.find('Q') != std::string::npos) castling |= WHITE_OOO;
        if (castling_part.find('k') != std::string::npos) castling |= BLACK_OO;
        if (castling_part.find('q') != std::string::npos) castling |= BLACK_OOO;
        ep_square = ep_part == "-" ? -1 : parse_square(ep_part);
        hash = compute_hash();
        return true;
    }

    std::string to_fen() const {
        std::ostringstream out;
        for (int rank = 7; rank >= 0; --rank) {
            int empty = 0;
            for (int file = 0; file < 8; ++file) {
                Piece piece = squares[make_square(file, rank)];
                if (piece == EMPTY) {
                    ++empty;
                } else {
                    if (empty) {
                        out << empty;
                        empty = 0;
                    }
                    out << piece_to_char(piece);
                }
            }
            if (empty) out << empty;
            if (rank) out << '/';
        }
        out << (side_to_move == WHITE ? " w " : " b ");
        std::string rights;
        if (castling & WHITE_OO) rights += 'K';
        if (castling & WHITE_OOO) rights += 'Q';
        if (castling & BLACK_OO) rights += 'k';
        if (castling & BLACK_OOO) rights += 'q';
        out << (rights.empty() ? "-" : rights) << ' ';
        out << (ep_square >= 0 ? square_to_string(ep_square) : "-");
        out << ' ' << halfmove_clock << ' ' << fullmove_number;
        return out.str();
    }

    uint64_t compute_hash() const {
        const Zobrist& z = zobrist();
        uint64_t key = 0;
        for (int sq = 0; sq < 64; ++sq) {
            if (squares[sq] != EMPTY) key ^= z.piece[static_cast<int>(squares[sq])][sq];
        }
        if (side_to_move == BLACK) key ^= z.side;
        key ^= z.castling[castling & 15];
        if (ep_square >= 0) key ^= z.ep_file[file_of(ep_square)];
        return key;
    }

    bool is_square_attacked(int sq, Color by) const {
        const int f = file_of(sq);
        const int r = rank_of(sq);

        const int pawn_rank = r + (by == WHITE ? -1 : 1);
        if (pawn_rank >= 0 && pawn_rank < 8) {
            for (int df : {-1, 1}) {
                const int pf = f + df;
                if (on_board(pf, pawn_rank) && squares[make_square(pf, pawn_rank)] == make_piece(by, PAWN)) {
                    return true;
                }
            }
        }

        static constexpr std::array<std::array<int, 2>, 8> knight_steps{{
            {{1, 2}}, {{2, 1}}, {{2, -1}}, {{1, -2}},
            {{-1, -2}}, {{-2, -1}}, {{-2, 1}}, {{-1, 2}}
        }};
        for (const auto& step : knight_steps) {
            const int nf = f + step[0];
            const int nr = r + step[1];
            if (on_board(nf, nr) && squares[make_square(nf, nr)] == make_piece(by, KNIGHT)) return true;
        }

        static constexpr std::array<std::array<int, 2>, 8> king_steps{{
            {{1, 1}}, {{1, 0}}, {{1, -1}}, {{0, -1}},
            {{-1, -1}}, {{-1, 0}}, {{-1, 1}}, {{0, 1}}
        }};
        for (const auto& step : king_steps) {
            const int kf = f + step[0];
            const int kr = r + step[1];
            if (on_board(kf, kr) && squares[make_square(kf, kr)] == make_piece(by, KING)) return true;
        }

        static constexpr std::array<std::array<int, 2>, 4> bishop_dirs{{
            {{1, 1}}, {{1, -1}}, {{-1, -1}}, {{-1, 1}}
        }};
        for (const auto& dir : bishop_dirs) {
            int sf = f + dir[0];
            int sr = r + dir[1];
            while (on_board(sf, sr)) {
                Piece piece = squares[make_square(sf, sr)];
                if (piece != EMPTY) {
                    if (color_of(piece) == by) {
                        PieceType type = type_of(piece);
                        if (type == BISHOP || type == QUEEN) return true;
                    }
                    break;
                }
                sf += dir[0];
                sr += dir[1];
            }
        }

        static constexpr std::array<std::array<int, 2>, 4> rook_dirs{{
            {{1, 0}}, {{0, -1}}, {{-1, 0}}, {{0, 1}}
        }};
        for (const auto& dir : rook_dirs) {
            int sf = f + dir[0];
            int sr = r + dir[1];
            while (on_board(sf, sr)) {
                Piece piece = squares[make_square(sf, sr)];
                if (piece != EMPTY) {
                    if (color_of(piece) == by) {
                        PieceType type = type_of(piece);
                        if (type == ROOK || type == QUEEN) return true;
                    }
                    break;
                }
                sf += dir[0];
                sr += dir[1];
            }
        }

        return false;
    }

    bool is_in_check(Color color) const {
        return is_square_attacked(king_square[color], opposite(color));
    }

    std::vector<Move> generate_legal(bool tactical_only = false) {
        std::vector<Move> pseudo;
        pseudo.reserve(96);
        generate_pseudo(pseudo, tactical_only);

        std::vector<Move> legal;
        legal.reserve(pseudo.size());
        const Color moving_color = side_to_move;
        for (const Move& move : pseudo) {
            Undo undo;
            make_move(move, undo);
            if (!is_in_check(moving_color)) legal.push_back(move);
            undo_move(undo);
        }
        return legal;
    }

    void make_move(const Move& move, Undo& undo) {
        undo.move = move;
        undo.castling = castling;
        undo.ep_square = ep_square;
        undo.halfmove_clock = halfmove_clock;
        undo.fullmove_number = fullmove_number;
        undo.king_square = king_square;
        undo.hash = hash;
        undo.side_to_move = side_to_move;

        const Piece moving = squares[move.from];
        const Color us = side_to_move;
        const Color them = opposite(us);
        const PieceType moving_type = type_of(moving);
        const int capture_square = (move.flags & EN_PASSANT) ? move.to + (us == WHITE ? -8 : 8) : move.to;
        undo.captured = (move.flags & EN_PASSANT) ? squares[capture_square] : squares[move.to];

        squares[move.from] = EMPTY;
        if (move.flags & EN_PASSANT) squares[capture_square] = EMPTY;

        Piece placed = moving;
        if (move.flags & PROMOTION) placed = make_piece(us, move.promotion);
        squares[move.to] = placed;

        if (moving_type == KING) {
            king_square[us] = move.to;
            castling &= us == WHITE ? ~(WHITE_OO | WHITE_OOO) : ~(BLACK_OO | BLACK_OOO);
            if (move.flags & KING_CASTLE) {
                const int rook_from = us == WHITE ? make_square(7, 0) : make_square(7, 7);
                const int rook_to = us == WHITE ? make_square(5, 0) : make_square(5, 7);
                squares[rook_to] = squares[rook_from];
                squares[rook_from] = EMPTY;
            } else if (move.flags & QUEEN_CASTLE) {
                const int rook_from = us == WHITE ? make_square(0, 0) : make_square(0, 7);
                const int rook_to = us == WHITE ? make_square(3, 0) : make_square(3, 7);
                squares[rook_to] = squares[rook_from];
                squares[rook_from] = EMPTY;
            }
        }

        auto clear_rook_right = [&](int sq) {
            if (sq == make_square(0, 0)) castling &= ~WHITE_OOO;
            if (sq == make_square(7, 0)) castling &= ~WHITE_OO;
            if (sq == make_square(0, 7)) castling &= ~BLACK_OOO;
            if (sq == make_square(7, 7)) castling &= ~BLACK_OO;
        };

        if (moving_type == ROOK) clear_rook_right(move.from);
        if (undo.captured != EMPTY && type_of(undo.captured) == ROOK) clear_rook_right(capture_square);

        ep_square = -1;
        if (move.flags & DOUBLE_PUSH) ep_square = move.from + (us == WHITE ? 8 : -8);

        halfmove_clock = (moving_type == PAWN || undo.captured != EMPTY) ? 0 : halfmove_clock + 1;
        if (us == BLACK) ++fullmove_number;
        side_to_move = them;
        hash = compute_hash();
    }

    void undo_move(const Undo& undo) {
        const Move& move = undo.move;
        side_to_move = undo.side_to_move;
        castling = undo.castling;
        ep_square = undo.ep_square;
        halfmove_clock = undo.halfmove_clock;
        fullmove_number = undo.fullmove_number;
        king_square = undo.king_square;
        hash = undo.hash;

        const Color us = side_to_move;
        Piece moved_back = squares[move.to];
        if (move.flags & PROMOTION) moved_back = make_piece(us, PAWN);

        squares[move.from] = moved_back;
        if (move.flags & EN_PASSANT) {
            const int capture_square = move.to + (us == WHITE ? -8 : 8);
            squares[move.to] = EMPTY;
            squares[capture_square] = undo.captured;
        } else {
            squares[move.to] = undo.captured;
        }

        if (move.flags & KING_CASTLE) {
            const int rook_from = us == WHITE ? make_square(7, 0) : make_square(7, 7);
            const int rook_to = us == WHITE ? make_square(5, 0) : make_square(5, 7);
            squares[rook_from] = squares[rook_to];
            squares[rook_to] = EMPTY;
        } else if (move.flags & QUEEN_CASTLE) {
            const int rook_from = us == WHITE ? make_square(0, 0) : make_square(0, 7);
            const int rook_to = us == WHITE ? make_square(3, 0) : make_square(3, 7);
            squares[rook_from] = squares[rook_to];
            squares[rook_to] = EMPTY;
        }
    }

private:
    void add_move(std::vector<Move>& moves, int from, int to, uint8_t flags = QUIET, PieceType promotion = NO_PIECE_TYPE) const {
        moves.push_back(Move{from, to, promotion, flags});
    }

    void add_promotions(std::vector<Move>& moves, int from, int to, uint8_t flags) const {
        add_move(moves, from, to, static_cast<uint8_t>(flags | PROMOTION), QUEEN);
        add_move(moves, from, to, static_cast<uint8_t>(flags | PROMOTION), ROOK);
        add_move(moves, from, to, static_cast<uint8_t>(flags | PROMOTION), BISHOP);
        add_move(moves, from, to, static_cast<uint8_t>(flags | PROMOTION), KNIGHT);
    }

    void generate_pseudo(std::vector<Move>& moves, bool tactical_only) const {
        for (int sq = 0; sq < 64; ++sq) {
            Piece piece = squares[sq];
            if (piece == EMPTY || color_of(piece) != side_to_move) continue;
            switch (type_of(piece)) {
                case PAWN: generate_pawn_moves(moves, sq, tactical_only); break;
                case KNIGHT: generate_step_moves(moves, sq, tactical_only, knight_offsets()); break;
                case BISHOP: generate_slider_moves(moves, sq, tactical_only, bishop_dirs()); break;
                case ROOK: generate_slider_moves(moves, sq, tactical_only, rook_dirs()); break;
                case QUEEN: generate_slider_moves(moves, sq, tactical_only, queen_dirs()); break;
                case KING:
                    generate_step_moves(moves, sq, tactical_only, king_offsets());
                    if (!tactical_only) generate_castling(moves);
                    break;
                default: break;
            }
        }
    }

    static const std::array<std::array<int, 2>, 8>& knight_offsets() {
        static constexpr std::array<std::array<int, 2>, 8> offsets{{
            {{1, 2}}, {{2, 1}}, {{2, -1}}, {{1, -2}},
            {{-1, -2}}, {{-2, -1}}, {{-2, 1}}, {{-1, 2}}
        }};
        return offsets;
    }

    static const std::array<std::array<int, 2>, 8>& king_offsets() {
        static constexpr std::array<std::array<int, 2>, 8> offsets{{
            {{1, 1}}, {{1, 0}}, {{1, -1}}, {{0, -1}},
            {{-1, -1}}, {{-1, 0}}, {{-1, 1}}, {{0, 1}}
        }};
        return offsets;
    }

    static const std::array<std::array<int, 2>, 4>& bishop_dirs() {
        static constexpr std::array<std::array<int, 2>, 4> dirs{{
            {{1, 1}}, {{1, -1}}, {{-1, -1}}, {{-1, 1}}
        }};
        return dirs;
    }

    static const std::array<std::array<int, 2>, 4>& rook_dirs() {
        static constexpr std::array<std::array<int, 2>, 4> dirs{{
            {{1, 0}}, {{0, -1}}, {{-1, 0}}, {{0, 1}}
        }};
        return dirs;
    }

    static const std::array<std::array<int, 2>, 8>& queen_dirs() {
        static constexpr std::array<std::array<int, 2>, 8> dirs{{
            {{1, 1}}, {{1, -1}}, {{-1, -1}}, {{-1, 1}},
            {{1, 0}}, {{0, -1}}, {{-1, 0}}, {{0, 1}}
        }};
        return dirs;
    }

    void generate_pawn_moves(std::vector<Move>& moves, int sq, bool tactical_only) const {
        const Color us = side_to_move;
        const Color them = opposite(us);
        const int direction = us == WHITE ? 1 : -1;
        const int start_rank = us == WHITE ? 1 : 6;
        const int promotion_from_rank = us == WHITE ? 6 : 1;
        const int f = file_of(sq);
        const int r = rank_of(sq);

        const int one_rank = r + direction;
        if (on_board(f, one_rank)) {
            const int one = make_square(f, one_rank);
            if (squares[one] == EMPTY) {
                if (r == promotion_from_rank) {
                    add_promotions(moves, sq, one, QUIET);
                } else if (!tactical_only) {
                    add_move(moves, sq, one);
                    const int two_rank = r + 2 * direction;
                    if (r == start_rank && on_board(f, two_rank)) {
                        const int two = make_square(f, two_rank);
                        if (squares[two] == EMPTY) add_move(moves, sq, two, DOUBLE_PUSH);
                    }
                }
            }
        }

        for (int df : {-1, 1}) {
            const int cf = f + df;
            const int cr = r + direction;
            if (!on_board(cf, cr)) continue;
            const int to = make_square(cf, cr);
            Piece target = squares[to];
            if (target != EMPTY && color_of(target) == them) {
                if (r == promotion_from_rank) {
                    add_promotions(moves, sq, to, CAPTURE);
                } else {
                    add_move(moves, sq, to, CAPTURE);
                }
            }
            if (to == ep_square) add_move(moves, sq, to, static_cast<uint8_t>(CAPTURE | EN_PASSANT));
        }
    }

    template <std::size_t N>
    void generate_step_moves(std::vector<Move>& moves, int sq, bool tactical_only, const std::array<std::array<int, 2>, N>& offsets) const {
        const Color them = opposite(side_to_move);
        const int f = file_of(sq);
        const int r = rank_of(sq);
        for (const auto& offset : offsets) {
            const int nf = f + offset[0];
            const int nr = r + offset[1];
            if (!on_board(nf, nr)) continue;
            const int to = make_square(nf, nr);
            Piece target = squares[to];
            if (target == EMPTY) {
                if (!tactical_only) add_move(moves, sq, to);
            } else if (color_of(target) == them) {
                add_move(moves, sq, to, CAPTURE);
            }
        }
    }

    template <std::size_t N>
    void generate_slider_moves(std::vector<Move>& moves, int sq, bool tactical_only, const std::array<std::array<int, 2>, N>& dirs) const {
        const Color them = opposite(side_to_move);
        const int f = file_of(sq);
        const int r = rank_of(sq);
        for (const auto& dir : dirs) {
            int nf = f + dir[0];
            int nr = r + dir[1];
            while (on_board(nf, nr)) {
                const int to = make_square(nf, nr);
                Piece target = squares[to];
                if (target == EMPTY) {
                    if (!tactical_only) add_move(moves, sq, to);
                } else {
                    if (color_of(target) == them) add_move(moves, sq, to, CAPTURE);
                    break;
                }
                nf += dir[0];
                nr += dir[1];
            }
        }
    }

    void generate_castling(std::vector<Move>& moves) const {
        if (side_to_move == WHITE) {
            if ((castling & WHITE_OO) &&
                squares[make_square(4, 0)] == WK &&
                squares[make_square(5, 0)] == EMPTY &&
                squares[make_square(6, 0)] == EMPTY &&
                !is_square_attacked(make_square(4, 0), BLACK) &&
                !is_square_attacked(make_square(5, 0), BLACK) &&
                !is_square_attacked(make_square(6, 0), BLACK)) {
                add_move(moves, make_square(4, 0), make_square(6, 0), KING_CASTLE);
            }
            if ((castling & WHITE_OOO) &&
                squares[make_square(4, 0)] == WK &&
                squares[make_square(3, 0)] == EMPTY &&
                squares[make_square(2, 0)] == EMPTY &&
                squares[make_square(1, 0)] == EMPTY &&
                !is_square_attacked(make_square(4, 0), BLACK) &&
                !is_square_attacked(make_square(3, 0), BLACK) &&
                !is_square_attacked(make_square(2, 0), BLACK)) {
                add_move(moves, make_square(4, 0), make_square(2, 0), QUEEN_CASTLE);
            }
        } else {
            if ((castling & BLACK_OO) &&
                squares[make_square(4, 7)] == BK &&
                squares[make_square(5, 7)] == EMPTY &&
                squares[make_square(6, 7)] == EMPTY &&
                !is_square_attacked(make_square(4, 7), WHITE) &&
                !is_square_attacked(make_square(5, 7), WHITE) &&
                !is_square_attacked(make_square(6, 7), WHITE)) {
                add_move(moves, make_square(4, 7), make_square(6, 7), KING_CASTLE);
            }
            if ((castling & BLACK_OOO) &&
                squares[make_square(4, 7)] == BK &&
                squares[make_square(3, 7)] == EMPTY &&
                squares[make_square(2, 7)] == EMPTY &&
                squares[make_square(1, 7)] == EMPTY &&
                !is_square_attacked(make_square(4, 7), WHITE) &&
                !is_square_attacked(make_square(3, 7), WHITE) &&
                !is_square_attacked(make_square(2, 7), WHITE)) {
                add_move(moves, make_square(4, 7), make_square(2, 7), QUEEN_CASTLE);
            }
        }
    }
};

int center_score(int file, int rank) {
    const int file_distance = std::abs(file * 2 - 7);
    const int rank_distance = std::abs(rank * 2 - 7);
    return 14 - file_distance - rank_distance;
}

int piece_square_value(PieceType type, int sq, Color color, bool endgame) {
    const int file = file_of(sq);
    const int rank = color == WHITE ? rank_of(sq) : 7 - rank_of(sq);
    const int center = center_score(file, rank);
    switch (type) {
        case PAWN:
            return rank * 10 - std::abs(file * 2 - 7) * 2;
        case KNIGHT:
            return center * 8 - (file == 0 || file == 7 ? 12 : 0) - (rank == 0 || rank == 7 ? 12 : 0);
        case BISHOP:
            return center * 5 + (rank >= 2 ? 6 : 0);
        case ROOK:
            return rank * 3 + (rank == 6 ? 16 : 0);
        case QUEEN:
            return center * 2;
        case KING:
            if (endgame) return center * 7;
            return -center * 5 - rank * 8 + ((file == 1 || file == 2 || file == 6 || file == 7) ? 18 : 0);
        default:
            return 0;
    }
}

struct MaterialProfile {
    int white_material = 0;
    int black_material = 0;
    int non_king_pieces = 0;
};

MaterialProfile material_profile(const Board& board) {
    MaterialProfile profile;
    for (Piece piece : board.squares) {
        if (piece == EMPTY) continue;
        const PieceType type = type_of(piece);
        if (type != KING) ++profile.non_king_pieces;
        const int value = PIECE_VALUE[type];
        if (color_of(piece) == WHITE) profile.white_material += value;
        else profile.black_material += value;
    }
    return profile;
}

int mr_henderson_simplification_bonus(const MaterialProfile& profile) {
    const int balance = profile.white_material - profile.black_material;
    const int lead = std::abs(balance);
    if (lead < 140) return 0;

    const int captured_non_king_pieces = std::max(0, 30 - profile.non_king_pieces);
    const int bonus_per_capture = std::min(8, 2 + lead / 300);
    const int bonus = captured_non_king_pieces * bonus_per_capture;
    return balance > 0 ? bonus : -bonus;
}

int evaluate(const Board& board) {
    int score = 0;
    int non_pawn_material = 0;
    std::array<int, 8> white_pawns{};
    std::array<int, 8> black_pawns{};
    int white_bishops = 0;
    int black_bishops = 0;

    for (int sq = 0; sq < 64; ++sq) {
        Piece piece = board.squares[sq];
        if (piece == EMPTY) continue;
        PieceType type = type_of(piece);
        Color color = color_of(piece);
        if (type != PAWN && type != KING) non_pawn_material += PIECE_VALUE[type];
        if (type == PAWN) {
            if (color == WHITE) ++white_pawns[file_of(sq)];
            else ++black_pawns[file_of(sq)];
        } else if (type == BISHOP) {
            if (color == WHITE) ++white_bishops;
            else ++black_bishops;
        }
    }

    const bool endgame = non_pawn_material <= 2400;
    for (int sq = 0; sq < 64; ++sq) {
        Piece piece = board.squares[sq];
        if (piece == EMPTY) continue;
        const PieceType type = type_of(piece);
        const Color color = color_of(piece);
        int value = PIECE_VALUE[type] + piece_square_value(type, sq, color, endgame);

        if (type == PAWN) {
            const int file = file_of(sq);
            const int rank = color == WHITE ? rank_of(sq) : 7 - rank_of(sq);
            const auto& friendly = color == WHITE ? white_pawns : black_pawns;
            const auto& enemy = color == WHITE ? black_pawns : white_pawns;
            if (friendly[file] > 1) value -= 10 * (friendly[file] - 1);
            const bool isolated = (file == 0 || friendly[file - 1] == 0) && (file == 7 || friendly[file + 1] == 0);
            if (isolated) value -= 12;

            bool passed = true;
            for (int df = -1; df <= 1; ++df) {
                const int ef = file + df;
                if (ef < 0 || ef > 7 || enemy[ef] == 0) continue;
                for (int esq = 0; esq < 64; ++esq) {
                    Piece ep = board.squares[esq];
                    if (ep == EMPTY || color_of(ep) != opposite(color) || type_of(ep) != PAWN || file_of(esq) != ef) continue;
                    const int enemy_rank = color == WHITE ? rank_of(esq) : 7 - rank_of(esq);
                    if (enemy_rank > rank) passed = false;
                }
            }
            static constexpr std::array<int, 8> passed_bonus{{0, 5, 12, 22, 38, 65, 110, 0}};
            if (passed) value += passed_bonus[rank];
        }

        score += color == WHITE ? value : -value;
    }

    if (white_bishops >= 2) score += 30;
    if (black_bishops >= 2) score -= 30;
    score += mr_henderson_simplification_bonus(material_profile(board));
    score += board.side_to_move == WHITE ? 10 : -10;
    return board.side_to_move == WHITE ? score : -score;
}

struct SearchLimits {
    int depth = 0;
    int64_t movetime_ms = 0;
    int64_t white_time_ms = -1;
    int64_t black_time_ms = -1;
    int64_t white_inc_ms = 0;
    int64_t black_inc_ms = 0;
    int moves_to_go = 0;
    bool infinite = false;
};

struct TTEntry {
    uint64_t key = 0;
    int depth = -1;
    int score = 0;
    uint8_t flag = 0;
    Move best;
    bool used = false;
};

struct SearchResult {
    Move best_move;
    int score = 0;
    int depth = 0;
    uint64_t nodes = 0;
};

class Searcher {
public:
    Searcher() { resize_hash(64); }

    void resize_hash(int megabytes) {
        const int mb = std::max(1, megabytes);
        const std::size_t bytes = static_cast<std::size_t>(mb) * 1024ULL * 1024ULL;
        const std::size_t entries = std::max<std::size_t>(1, bytes / sizeof(TTEntry));
        table.assign(entries, TTEntry{});
    }

    void clear_hash() {
        std::fill(table.begin(), table.end(), TTEntry{});
    }

    void set_move_overhead(int ms) {
        move_overhead_ms = std::max(0, ms);
    }

    SearchResult search(Board board, const SearchLimits& limits, std::atomic<bool>& stop_signal) {
        stop = &stop_signal;
        nodes = 0;
        std::fill(killers.begin(), killers.end(), std::array<Move, 2>{});
        for (auto& by_from : history) {
            for (auto& by_to : by_from) by_to.fill(0);
        }

        start_time = Clock::now();
        time_limit_ms = choose_time_limit(board, limits);
        nap_mode = should_take_nap(board, limits);
        if (nap_mode) {
            std::cout << "info string i am ahead. too much work. small nap." << std::endl;
        }

        std::vector<Move> root_moves = board.generate_legal();
        SearchResult result;
        result.nodes = 0;
        if (root_moves.empty()) return result;

        Move best_move = root_moves.front();
        int best_score = -INF;
        const int max_depth = limits.depth > 0 ? limits.depth : 64;

        for (int depth = 1; depth <= max_depth; ++depth) {
            if (should_stop()) break;

            order_root_moves(board, root_moves, best_move);
            int alpha = -INF;
            int beta = INF;
            struct RootCandidate {
                Move move;
                int score = -INF;
                int style = -INF;
            };
            std::vector<RootCandidate> candidates;
            candidates.reserve(root_moves.size());

            for (const Move& move : root_moves) {
                Undo undo;
                board.make_move(move, undo);
                int score = -negamax(board, depth - 1, -beta, -alpha, 1);
                board.undo_move(undo);

                if (should_stop()) break;
                candidates.push_back(RootCandidate{move, score, root_style_score(board, move, score)});
                if (score > alpha) alpha = score;
            }

            if (should_stop()) break;
            if (candidates.empty()) break;

            const int best_raw_score = std::max_element(candidates.begin(), candidates.end(),
                [](const RootCandidate& a, const RootCandidate& b) {
                    return a.score < b.score;
                })->score;

            const RootCandidate* selected = nullptr;
            for (const RootCandidate& candidate : candidates) {
                if (candidate.score < best_raw_score - 12) continue;
                if (!selected ||
                    candidate.style > selected->style ||
                    (candidate.style == selected->style && candidate.score > selected->score)) {
                    selected = &candidate;
                }
            }

            best_move = selected ? selected->move : candidates.front().move;
            best_score = selected ? selected->score : candidates.front().score;
            result.best_move = best_move;
            result.score = best_score;
            result.depth = depth;
            result.nodes = nodes;
            print_info(depth, best_score, nodes, best_move);

            if (std::abs(best_score) > MATE_SCORE - MAX_PLY) break;
            if (!limits.infinite && time_limit_ms > 0 && elapsed_ms() > time_limit_ms * 7 / 10 && depth >= 4) break;
        }

        if (result.best_move.from < 0) {
            result.best_move = best_move;
            result.score = best_score;
            result.depth = std::max(1, result.depth);
            result.nodes = nodes;
        }
        return result;
    }

private:
    using Clock = std::chrono::steady_clock;

    static constexpr uint8_t TT_EXACT = 0;
    static constexpr uint8_t TT_LOWER = 1;
    static constexpr uint8_t TT_UPPER = 2;

    std::vector<TTEntry> table;
    uint64_t nodes = 0;
    Clock::time_point start_time{};
    int64_t time_limit_ms = 0;
    int move_overhead_ms = 20;
    bool nap_mode = false;
    std::atomic<bool>* stop = nullptr;
    std::array<std::array<Move, 2>, MAX_PLY> killers{};
    std::array<std::array<std::array<int, 64>, 64>, 2> history{};

    int64_t choose_time_limit(const Board& board, const SearchLimits& limits) const {
        if (limits.infinite || limits.depth > 0) return limits.movetime_ms;
        if (limits.movetime_ms > 0) return limits.movetime_ms;

        const int64_t remaining = board.side_to_move == WHITE ? limits.white_time_ms : limits.black_time_ms;
        const int64_t increment = board.side_to_move == WHITE ? limits.white_inc_ms : limits.black_inc_ms;
        if (remaining <= 0) return 0;

        const int moves = limits.moves_to_go > 0 ? limits.moves_to_go : 30;
        int64_t target = remaining / moves + increment * 3 / 4;
        target = std::min(target, remaining / 4);
        target = std::max<int64_t>(10, target - move_overhead_ms);
        if (remaining > move_overhead_ms + 5) target = std::min(target, remaining - move_overhead_ms);

        const int static_score = evaluate(board);
        if (static_score >= 320 && target >= 250 && remaining >= 8000) {
            target = target * (static_score >= 800 ? 60 : 75) / 100;
            target = std::max<int64_t>(120, target);
        }

        return std::max<int64_t>(1, target);
    }

    bool should_take_nap(const Board& board, const SearchLimits& limits) const {
        if (limits.infinite || limits.depth > 0) return false;
        if (limits.movetime_ms > 0) return false;
        return evaluate(board) >= 320 && time_limit_ms > 0;
    }

    int64_t elapsed_ms() const {
        return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start_time).count();
    }

    bool should_stop() const {
        if (stop && stop->load(std::memory_order_relaxed)) return true;
        if (time_limit_ms <= 0) return false;
        if ((nodes & 2047ULL) != 0) return false;
        return elapsed_ms() >= time_limit_ms;
    }

    TTEntry* probe(uint64_t key) {
        if (table.empty()) return nullptr;
        TTEntry& entry = table[key % table.size()];
        if (entry.used && entry.key == key) return &entry;
        return nullptr;
    }

    void store(uint64_t key, int depth, int score, uint8_t flag, const Move& best) {
        if (table.empty()) return;
        TTEntry& entry = table[key % table.size()];
        if (!entry.used || depth >= entry.depth) {
            entry.used = true;
            entry.key = key;
            entry.depth = depth;
            entry.score = score;
            entry.flag = flag;
            entry.best = best;
        }
    }

    int score_move(const Board& board, const Move& move, const Move& tt_move, int ply) const {
        if (same_move(move, tt_move)) return 1'000'000;
        int score = 0;
        if (is_capture(move)) {
            Piece victim = (move.flags & EN_PASSANT) ? make_piece(opposite(board.side_to_move), PAWN) : board.squares[move.to];
            Piece attacker = board.squares[move.from];
            score += 100'000 + PIECE_VALUE[type_of(victim)] * 10 - PIECE_VALUE[type_of(attacker)];
        }
        if (is_promotion(move)) score += 80'000 + PIECE_VALUE[move.promotion];
        if (ply < MAX_PLY) {
            if (same_move(move, killers[ply][0])) score += 70'000;
            if (same_move(move, killers[ply][1])) score += 69'000;
        }
        score += history[board.side_to_move][move.from][move.to];
        return score;
    }

    int root_style_score(const Board& board, const Move& move, int searched_score) const {
        int style = searched_score;
        const int static_score = evaluate(board);

        if (is_capture(move)) {
            const Piece victim = (move.flags & EN_PASSANT)
                ? make_piece(opposite(board.side_to_move), PAWN)
                : board.squares[move.to];
            const int victim_value = victim == EMPTY ? 0 : PIECE_VALUE[type_of(victim)];
            style += 4 + std::min(10, victim_value / 100);
            if (static_score >= 160) style += std::min(12, static_score / 90);
        }

        if (is_promotion(move)) style += 5;
        return style;
    }

    void order_moves(const Board& board, std::vector<Move>& moves, const Move& tt_move, int ply) const {
        std::stable_sort(moves.begin(), moves.end(), [&](const Move& a, const Move& b) {
            return score_move(board, a, tt_move, ply) > score_move(board, b, tt_move, ply);
        });
    }

    void order_root_moves(const Board& board, std::vector<Move>& moves, const Move& previous_best) const {
        std::stable_sort(moves.begin(), moves.end(), [&](const Move& a, const Move& b) {
            int a_score = same_move(a, previous_best) ? 2'000'000 : score_move(board, a, Move{}, 0);
            int b_score = same_move(b, previous_best) ? 2'000'000 : score_move(board, b, Move{}, 0);
            return a_score > b_score;
        });
    }

    void update_quiet_heuristics(const Board& board, const Move& move, int depth, int ply) {
        if (is_capture(move)) return;
        if (ply < MAX_PLY && !same_move(move, killers[ply][0])) {
            killers[ply][1] = killers[ply][0];
            killers[ply][0] = move;
        }
        int& slot = history[board.side_to_move][move.from][move.to];
        slot += depth * depth;
        if (slot > 100000) slot /= 2;
    }

    int negamax(Board& board, int depth, int alpha, int beta, int ply) {
        if (ply >= MAX_PLY - 1) return evaluate(board);
        ++nodes;
        if (should_stop()) return evaluate(board);

        const bool in_check = board.is_in_check(board.side_to_move);
        if (in_check) ++depth;
        if (depth <= 0) return quiescence(board, alpha, beta, ply);

        const int alpha_original = alpha;
        Move tt_move;
        if (TTEntry* entry = probe(board.hash)) {
            tt_move = entry->best;
            if (entry->depth >= depth) {
                if (entry->flag == TT_EXACT) return entry->score;
                if (entry->flag == TT_LOWER && entry->score >= beta) return entry->score;
                if (entry->flag == TT_UPPER && entry->score <= alpha) return entry->score;
            }
        }

        std::vector<Move> moves = board.generate_legal();
        if (moves.empty()) {
            return in_check ? -MATE_SCORE + ply : 0;
        }
        order_moves(board, moves, tt_move, ply);

        Move best_move;
        int best_score = -INF;
        for (const Move& move : moves) {
            Undo undo;
            board.make_move(move, undo);
            int score = -negamax(board, depth - 1, -beta, -alpha, ply + 1);
            board.undo_move(undo);

            if (should_stop()) return evaluate(board);
            if (score > best_score) {
                best_score = score;
                best_move = move;
            }
            if (score > alpha) alpha = score;
            if (alpha >= beta) {
                update_quiet_heuristics(board, move, depth, ply);
                break;
            }
        }

        uint8_t flag = TT_EXACT;
        if (best_score <= alpha_original) flag = TT_UPPER;
        else if (best_score >= beta) flag = TT_LOWER;
        store(board.hash, depth, best_score, flag, best_move);
        return best_score;
    }

    int quiescence(Board& board, int alpha, int beta, int ply) {
        if (ply >= MAX_PLY - 1) return evaluate(board);
        ++nodes;
        if (should_stop()) return evaluate(board);

        const bool in_check = board.is_in_check(board.side_to_move);
        if (!in_check) {
            const int stand_pat = evaluate(board);
            if (stand_pat >= beta) return beta;
            if (stand_pat > alpha) alpha = stand_pat;
        }

        std::vector<Move> moves = board.generate_legal(!in_check);
        Move no_tt;
        order_moves(board, moves, no_tt, ply);

        for (const Move& move : moves) {
            Undo undo;
            board.make_move(move, undo);
            int score = -quiescence(board, -beta, -alpha, ply + 1);
            board.undo_move(undo);

            if (should_stop()) return evaluate(board);
            if (score >= beta) return beta;
            if (score > alpha) alpha = score;
        }
        return alpha;
    }

    static std::string score_to_uci(int score) {
        std::ostringstream out;
        if (score > MATE_SCORE - 1000) {
            out << "mate " << (MATE_SCORE - score + 1) / 2;
        } else if (score < -MATE_SCORE + 1000) {
            out << "mate " << -(MATE_SCORE + score + 1) / 2;
        } else {
            out << "cp " << score;
        }
        return out.str();
    }

    void print_info(int depth, int score, uint64_t searched_nodes, const Move& best_move) const {
        const int64_t ms = std::max<int64_t>(1, elapsed_ms());
        const uint64_t nps = searched_nodes * 1000ULL / static_cast<uint64_t>(ms);
        std::cout << "info depth " << depth
                  << " score " << score_to_uci(score)
                  << " nodes " << searched_nodes
                  << " nps " << nps
                  << " time " << ms
                  << " pv " << move_to_uci(best_move)
                  << std::endl;
    }
};

uint64_t perft(Board& board, int depth) {
    if (depth == 0) return 1;
    std::vector<Move> moves = board.generate_legal();
    if (depth == 1) return moves.size();
    uint64_t nodes = 0;
    for (const Move& move : moves) {
        Undo undo;
        board.make_move(move, undo);
        nodes += perft(board, depth - 1);
        board.undo_move(undo);
    }
    return nodes;
}

void perft_divide(Board& board, int depth) {
    std::vector<Move> moves = board.generate_legal();
    uint64_t total = 0;
    for (const Move& move : moves) {
        Undo undo;
        board.make_move(move, undo);
        const uint64_t nodes = depth <= 1 ? 1 : perft(board, depth - 1);
        board.undo_move(undo);
        total += nodes;
        std::cout << move_to_uci(move) << ": " << nodes << std::endl;
    }
    std::cout << "total: " << total << std::endl;
}

std::vector<std::string> split_tokens(const std::string& line) {
    std::istringstream in(line);
    std::vector<std::string> tokens;
    std::string token;
    while (in >> token) tokens.push_back(token);
    return tokens;
}

std::string join_tokens(const std::vector<std::string>& tokens, std::size_t begin, std::size_t end) {
    std::ostringstream out;
    for (std::size_t i = begin; i < end; ++i) {
        if (i != begin) out << ' ';
        out << tokens[i];
    }
    return out.str();
}

class UciEngine {
public:
    ~UciEngine() { stop_search(); }

    void loop() {
        std::string line;
        while (std::getline(std::cin, line)) {
            if (line.empty()) continue;
            if (line == "uci") {
                handle_uci();
            } else if (line == "isready") {
                std::cout << "readyok" << std::endl;
            } else if (line == "ucinewgame") {
                stop_search();
                searcher.clear_hash();
                board.set_startpos();
            } else if (line.rfind("setoption", 0) == 0) {
                stop_search();
                handle_setoption(line);
            } else if (line.rfind("position", 0) == 0) {
                stop_search();
                handle_position(line);
            } else if (line.rfind("go", 0) == 0) {
                handle_go(line);
            } else if (line == "stop") {
                stop_search();
            } else if (line == "quit") {
                stop_search();
                break;
            } else if (line == "d") {
                print_board();
            } else if (line.rfind("perft", 0) == 0) {
                handle_perft(line);
            } else if (line.rfind("divide", 0) == 0) {
                handle_divide(line);
            } else if (line == "eval") {
                std::cout << "info string eval " << evaluate(board) << std::endl;
            }
        }
    }

private:
    Board board;
    Searcher searcher;
    std::atomic<bool> stop_signal{false};
#if defined(_WIN32)
    HANDLE search_thread = nullptr;
#else
    std::thread search_thread;
#endif

    struct SearchJob {
        UciEngine* engine = nullptr;
        Board board;
        SearchLimits limits;
    };

#if defined(_WIN32)
    static DWORD WINAPI search_entry(LPVOID parameter) {
        std::unique_ptr<SearchJob> job(static_cast<SearchJob*>(parameter));
        SearchResult result = job->engine->searcher.search(job->board, job->limits, job->engine->stop_signal);
        std::cout << "bestmove " << move_to_uci(result.best_move) << std::endl;
        return 0;
    }
#endif

    void handle_uci() {
        std::cout << "id name MrHenderson" << std::endl;
        std::cout << "id author Leonardo Nguyen" << std::endl;
        std::cout << "option name Hash type spin default 64 min 1 max 1024" << std::endl;
        std::cout << "option name Move Overhead type spin default 20 min 0 max 5000" << std::endl;
        std::cout << "option name Clear Hash type button" << std::endl;
        std::cout << "uciok" << std::endl;
    }

    void handle_setoption(const std::string& line) {
        std::vector<std::string> tokens = split_tokens(line);
        std::string name;
        std::string value;
        bool reading_name = false;
        bool reading_value = false;
        for (std::size_t i = 1; i < tokens.size(); ++i) {
            if (tokens[i] == "name") {
                reading_name = true;
                reading_value = false;
                continue;
            }
            if (tokens[i] == "value") {
                reading_name = false;
                reading_value = true;
                continue;
            }
            if (reading_name) {
                if (!name.empty()) name += ' ';
                name += tokens[i];
            } else if (reading_value) {
                if (!value.empty()) value += ' ';
                value += tokens[i];
            }
        }

        if (name == "Hash" && !value.empty()) {
            searcher.resize_hash(std::atoi(value.c_str()));
        } else if (name == "Move Overhead" && !value.empty()) {
            searcher.set_move_overhead(std::atoi(value.c_str()));
        } else if (name == "Clear Hash") {
            searcher.clear_hash();
        }
    }

    void handle_position(const std::string& line) {
        std::vector<std::string> tokens = split_tokens(line);
        if (tokens.size() < 2) return;

        std::size_t index = 1;
        if (tokens[index] == "startpos") {
            board.set_startpos();
            ++index;
        } else if (tokens[index] == "fen") {
            ++index;
            std::size_t fen_begin = index;
            while (index < tokens.size() && tokens[index] != "moves") ++index;
            if (!board.set_from_fen(join_tokens(tokens, fen_begin, index))) {
                board.set_startpos();
            }
        }

        if (index < tokens.size() && tokens[index] == "moves") ++index;
        for (; index < tokens.size(); ++index) {
            apply_uci_move(tokens[index]);
        }
    }

    void apply_uci_move(const std::string& text) {
        Move parsed = parse_uci_move(text);
        std::vector<Move> moves = board.generate_legal();
        for (const Move& move : moves) {
            if (same_move(parsed, move)) {
                Undo undo;
                board.make_move(move, undo);
                return;
            }
        }
    }

    void handle_go(const std::string& line) {
        stop_search();
        std::vector<std::string> tokens = split_tokens(line);
        SearchLimits limits;

        for (std::size_t i = 1; i < tokens.size(); ++i) {
            const std::string& token = tokens[i];
            auto next_int = [&]() -> int64_t {
                if (i + 1 >= tokens.size()) return 0;
                return std::atoll(tokens[++i].c_str());
            };

            if (token == "depth") limits.depth = static_cast<int>(next_int());
            else if (token == "movetime") limits.movetime_ms = next_int();
            else if (token == "wtime") limits.white_time_ms = next_int();
            else if (token == "btime") limits.black_time_ms = next_int();
            else if (token == "winc") limits.white_inc_ms = next_int();
            else if (token == "binc") limits.black_inc_ms = next_int();
            else if (token == "movestogo") limits.moves_to_go = static_cast<int>(next_int());
            else if (token == "infinite") limits.infinite = true;
            else if (token == "perft") {
                const int depth = static_cast<int>(next_int());
                Board copy = board;
                const uint64_t nodes = perft(copy, depth);
                std::cout << "info string perft depth " << depth << " nodes " << nodes << std::endl;
                return;
            }
        }

        stop_signal.store(false, std::memory_order_relaxed);
        Board search_board = board;
#if defined(_WIN32)
        auto* job = new SearchJob{this, search_board, limits};
        search_thread = CreateThread(nullptr, 0, &UciEngine::search_entry, job, 0, nullptr);
        if (!search_thread) {
            delete job;
            SearchResult result = searcher.search(search_board, limits, stop_signal);
            std::cout << "bestmove " << move_to_uci(result.best_move) << std::endl;
        }
#else
        search_thread = std::thread([this, search_board, limits]() mutable {
            SearchResult result = searcher.search(search_board, limits, stop_signal);
            std::cout << "bestmove " << move_to_uci(result.best_move) << std::endl;
        });
#endif
    }

    void stop_search() {
        stop_signal.store(true, std::memory_order_relaxed);
#if defined(_WIN32)
        if (search_thread) {
            WaitForSingleObject(search_thread, INFINITE);
            CloseHandle(search_thread);
            search_thread = nullptr;
        }
#else
        if (search_thread.joinable()) search_thread.join();
#endif
        stop_signal.store(false, std::memory_order_relaxed);
    }

    void handle_perft(const std::string& line) {
        std::vector<std::string> tokens = split_tokens(line);
        if (tokens.size() < 2) return;
        const int depth = std::atoi(tokens[1].c_str());
        Board copy = board;
        const auto start = std::chrono::steady_clock::now();
        const uint64_t nodes = perft(copy, depth);
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
        std::cout << "perft " << depth << " " << nodes << " nodes " << std::max<int64_t>(1, elapsed) << " ms" << std::endl;
    }

    void handle_divide(const std::string& line) {
        std::vector<std::string> tokens = split_tokens(line);
        if (tokens.size() < 2) return;
        const int depth = std::atoi(tokens[1].c_str());
        Board copy = board;
        perft_divide(copy, depth);
    }

    void print_board() const {
        for (int rank = 7; rank >= 0; --rank) {
            std::cout << rank + 1 << "  ";
            for (int file = 0; file < 8; ++file) {
                std::cout << piece_to_char(board.squares[make_square(file, rank)]) << ' ';
            }
            std::cout << std::endl;
        }
        std::cout << "\n   a b c d e f g h\n";
        std::cout << "Fen: " << board.to_fen() << std::endl;
        std::cout << "Key: " << board.hash << std::endl;
    }
};

}  // namespace

int main() {
    std::ios::sync_with_stdio(false);
    std::cin.tie(nullptr);

    UciEngine engine;
    engine.loop();
    return 0;
}
