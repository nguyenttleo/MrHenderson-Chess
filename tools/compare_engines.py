"""Compare two UCI engines with paired openings; requires python-chess."""

import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
import json
from pathlib import Path
import time

import chess
import chess.engine
import chess.pgn


OPENINGS = [
    ("Italian", "e4 e5 Nf3 Nc6 Bc4 Bc5"),
    ("Queens Gambit", "d4 d5 c4 e6 Nc3 Nf6"),
    ("Catalan", "d4 Nf6 c4 e6 g3 d5 Bg2 Be7"),
    ("Sicilian Open", "e4 c5 Nf3 d6 d4 cxd4 Nxd4 Nf6"),
    ("French", "e4 e6 d4 d5 Nc3 Nf6"),
    ("Caro Kann", "e4 c6 d4 d5 Nc3 dxe4 Nxe4"),
    ("Ruy Lopez", "e4 e5 Nf3 Nc6 Bb5 a6 Ba4 Nf6"),
    ("Kings Indian", "d4 Nf6 c4 g6 Nc3 Bg7 e4 d6"),
    ("Slav", "d4 d5 c4 c6 Nf3 Nf6 Nc3"),
    ("English", "c4 e5 Nc3 Nf6 g3 d5 cxd5 Nxd5"),
    ("Scotch", "e4 e5 Nf3 Nc6 d4 exd4 Nxd4"),
    ("Scandinavian", "e4 d5 exd5 Qxd5 Nc3 Qa5"),
    ("Nimzo Indian", "d4 Nf6 c4 e6 Nc3 Bb4 e3 O-O"),
    ("Grunfeld", "d4 Nf6 c4 g6 Nc3 d5 cxd5 Nxd5"),
    ("Pirc", "e4 d6 d4 Nf6 Nc3 g6 Nf3 Bg7"),
    ("Vienna", "e4 e5 Nc3 Nf6 Bc4 Nc6"),
    ("Petrov", "e4 e5 Nf3 Nf6 Nxe5 d6 Nf3 Nxe4"),
    ("London", "d4 d5 Nf3 Nf6 Bf4 e6 e3 Bd6"),
    ("Dutch", "d4 f5 g3 Nf6 Bg2 e6 Nf3 Be7"),
    ("Reti", "Nf3 d5 g3 Nf6 Bg2 g6 O-O Bg7"),
]


def opening_board(index):
    name, line = OPENINGS[index]
    board = chess.Board()
    for move in line.split():
        board.push_san(move)
    return name, board


def play_game(number, args):
    opening, board = opening_board(number // 2)
    candidate_white = number % 2 == 0
    engines = [chess.engine.SimpleEngine.popen_uci(str(path), timeout=30)
               for path in (args.baseline, args.candidate)]
    clocks = {chess.WHITE: args.clock, chess.BLACK: args.clock}
    depths = [[], []]
    for engine in engines:
        engine.configure({"Hash": 64})
    termination = "move cap"
    score = None
    started = time.monotonic()
    try:
        while board.ply() < 400 and not board.is_game_over(claim_draw=True):
            index = int(board.turn == candidate_white)
            limit = (chess.engine.Limit(white_clock=clocks[chess.WHITE],
                     black_clock=clocks[chess.BLACK], white_inc=args.increment,
                     black_inc=args.increment) if args.clock else
                     chess.engine.Limit(time=args.movetime))
            begin = time.monotonic()
            result = engines[index].play(board, limit, info=chess.engine.INFO_BASIC)
            elapsed = time.monotonic() - begin
            if args.clock:
                clocks[board.turn] -= elapsed
                if clocks[board.turn] < 0:
                    score = float(index == 0)
                    termination = "time forfeit"
                    break
                clocks[board.turn] += args.increment
            if result.move not in board.legal_moves:
                score = float(index == 0)
                termination = "illegal move"
                break
            if "depth" in result.info:
                depths[index].append(result.info["depth"])
            board.push(result.move)
        outcome = board.outcome(claim_draw=True)
        if outcome is not None:
            score = 0.5 if outcome.winner is None else float(outcome.winner == candidate_white)
            termination = outcome.termination.name
        result_text = ("*" if score is None else "1/2-1/2" if score == 0.5 else
                       "1-0" if bool(score) == candidate_white else "0-1")
        record = {"game": number + 1, "opening": opening,
                  "candidate_color": "white" if candidate_white else "black",
                  "score": score, "result": result_text, "termination": termination,
                  "plies": board.ply(), "seconds": round(time.monotonic() - started, 2),
                  "mean_depth_baseline": round(sum(depths[0]) / len(depths[0]), 2) if depths[0] else None,
                  "mean_depth_candidate": round(sum(depths[1]) / len(depths[1]), 2) if depths[1] else None}
        game = chess.pgn.Game.from_board(board)
        game.headers.update({"Event": "Paired engine comparison", "Round": str(number + 1),
                             "White": "Candidate" if candidate_white else "Baseline",
                             "Black": "Baseline" if candidate_white else "Candidate",
                             "Result": result_text, "Termination": termination,
                             "TimeControl": f"{args.clock}+{args.increment}" if args.clock else "-"})
        game.headers["TimeLimit"] = "clock" if args.clock else f"{args.movetime} seconds per move"
        return record, str(game)
    finally:
        for engine in engines:
            engine.quit()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("baseline", type=Path)
    parser.add_argument("candidate", type=Path)
    parser.add_argument("--games", type=int, default=40)
    parser.add_argument("--movetime", type=float, default=0.15)
    parser.add_argument("--clock", type=float, default=0)
    parser.add_argument("--increment", type=float, default=0.1)
    parser.add_argument("--jobs", type=int, default=2)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if args.games < 2 or args.games > 2 * len(OPENINGS) or args.games % 2:
        parser.error("--games must be even and between 2 and 40")
    if args.jobs < 1 or args.movetime <= 0 or args.clock < 0 or args.increment < 0:
        parser.error("Invalid clock, movetime, or job count")
    args.baseline = args.baseline.resolve(strict=True)
    args.candidate = args.candidate.resolve(strict=True)
    for index in range(args.games // 2):
        opening_board(index)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    results = []
    games = {}
    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        futures = [pool.submit(play_game, number, args) for number in range(args.games)]
        for future in as_completed(futures):
            record, pgn = future.result()
            results.append(record)
            games[record["game"]] = pgn
            results.sort(key=lambda item: item["game"])
            args.output.with_suffix(".json").write_text(json.dumps(results, indent=2), encoding="utf-8")
            args.output.with_suffix(".pgn").write_text("\n\n".join(games[key] for key in sorted(games)) + "\n", encoding="utf-8")
            print(json.dumps(record), flush=True)
    completed = [record for record in results if record["score"] is not None]
    summary = {"wins": sum(record["score"] == 1 for record in completed),
               "draws": sum(record["score"] == 0.5 for record in completed),
               "losses": sum(record["score"] == 0 for record in completed),
               "unfinished": len(results) - len(completed),
               "points": sum(record["score"] for record in completed)}
    print(json.dumps(summary), flush=True)


if __name__ == "__main__":
    main()
