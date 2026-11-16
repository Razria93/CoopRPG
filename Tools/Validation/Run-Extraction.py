"""Verify an interrupted and completed shared extraction in a dedicated three-process session."""

import argparse
import importlib.util
import json
from pathlib import Path
import socket
import sys
import time


sys.dont_write_bytecode = True
spec = importlib.util.spec_from_file_location("shared_chest", Path(__file__).with_name("Run-SharedChest.py"))
shared = importlib.util.module_from_spec(spec)
spec.loader.exec_module(shared)


def run(stage, output, mover):
    case = shared.Case(stage, output)
    roles = ("a", "b")
    players = {}
    details = {}
    errors = []
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        sock.bind(("127.0.0.1", 0))
        port = sock.getsockname()[1]

    def server(code, **fields):
        return case.events("server", code, **fields)

    def input_until(role, commands, description, predicate, release=""):
        case.signal(role, commands)
        try:
            case.wait(description, predicate)
        finally:
            case.signal(role, release)

    def place(role, inside):
        value = str(int(inside))
        position = "inside" if inside else "outside"
        before = len(server("TestExtractionPosition", Inside=value, RequesterPlayerId=players[role]))
        input_until(role, "Input.-key W\nInput.-key S\nCoop.Test.ExtractionPosition " + position,
                    f"{role} is positioned {position}", lambda: len(server(
                        "TestExtractionPosition", Inside=value, RequesterPlayerId=players[role])) > before)

    try:
        case.start("server", ["/Game/Maps/L_Movement?EnemyCombat", f"-port={port}", "-MULTIHOME=127.0.0.1",
                              "-CoopCombatTest"], 25)
        case.wait("dedicated server listening", lambda: (output / "server.log").exists()
                  and f"listening on port {port}" in (output / "server.log").read_text(encoding="utf-8", errors="replace"), 55)
        for role in roles:
            case.start(role, [f"127.0.0.1:{port}", "-RenderOffscreen", "-windowed", "-ResX=960", "-ResY=540",
                              "-CoopCombatTest"], 30,
                       [(110, "Input.+key R"), (111, "Input.-key R"),
                        (120, "Input.+key W"), (153, "Input.-key W")])
        case.wait("both players are ready", lambda: server("LootRoundPhase", Phase="Playing"), 55)
        round_id = server("LootRoundPhase", Phase="Playing")[0]["fields"]["Round"]
        key = {"Round": round_id}
        for role in roles:
            players[role] = case.events(role, "LootReadyRequested", **key)[0]["fields"]["LocalPlayerId"]
            input_until(role, shared.press("E"), f"{role} opens the chest",
                        lambda: case.events(role, "ChestViewReplicated", Open="1"))
            input_until(role, shared.press("G"), f"{role} equips a weapon",
                        lambda: server("ChestWeaponGranted", RequesterPlayerId=players[role]))
            input_until(role, shared.press("Q"), f"{role} closes the chest",
                        lambda: case.events(role, "ChestViewReplicated", Open="0"))

        input_until("a", "Coop.Test.Vitals 10 100", "server sets the enemy to 10 HP",
                    lambda: server("TestVitals", HP="10.00", AttackerHP="100.00"))
        input_until("a", "Input.+key W", "enemy starts an attack near the approaching player",
                    lambda: server("EnemyAttackStarted"), "Input.-key W")
        input_until("a", "Input.-key W\n" + shared.press("LeftMouseButton"), "enemy dies and drops loot",
                    lambda: server("EnemyLootSpawned", **key))
        drop_id = server("EnemyLootSpawned", **key)[0]["fields"]["DropId"]
        for role in roles:
            case.wait(f"{role} receives the drop", lambda: case.events(role, "EnemyLootReplicated", DropId=drop_id))
            place(role, True)

        time.sleep(3.20)
        case.check("two living players cannot start extraction without team loot",
                   not server("EnemyLootGranted", **key) and not server("ExtractionStarted", **key)
                   and not server("ExtractionResult", **key))

        input_until("a", "Coop.Test.LootPosition near", "player a is inside loot pickup range",
                    lambda: server("TestEnemyLootPosition", DropId=drop_id, Position="Near",
                                   RequesterPlayerId=players["a"]))
        input_until("a", shared.press("T"), "real client request grants one loot to the team",
                    lambda: server("EnemyLootGranted", **key, DropId=drop_id, TeamLoot="0->1"))
        time.sleep(0.45)
        case.check("one player in the zone with the loot cannot start extraction",
                   bool(server("EnemyLootGranted", **key)) and not server("ExtractionStarted", **key)
                   and not server("ExtractionResult", **key))

        place("a", True)
        case.wait("server begins a joint hold", lambda: len(server("ExtractionStarted", **key)) == 1)
        for role in roles:
            case.wait(f"{role} receives the first progress deadline",
                      lambda: case.events(role, "ExtractionProgressReplicated", **key, Phase="Started"))

        input_until(mover, "Input.+key W", "normal client movement leaves the zone and resets progress",
                    lambda: len(server("ExtractionReset", **key)) == 1, "Input.-key W")
        for role in roles:
            case.wait(f"{role} receives zero progress",
                      lambda: case.events(role, "ExtractionProgressReplicated", **key, Phase="Reset", Deadline="0.000"))
        reset = server("ExtractionReset", **key)[0]
        case.check("interruption clears an incomplete hold and awards no score",
                   0.0 < float(reset["fields"]["Elapsed"]) < 3.0 and not server("ExtractionResult", **key))

        input_until(mover, "Input.-key W\nInput.+key S", "normal client movement reenters and starts a fresh hold",
                    lambda: len(server("ExtractionStarted", **key)) == 2, "Input.-key S")
        case.wait("server finalizes success and one point", lambda: server(
            "ExtractionResult", **key, Outcome="Success", Score="1"))
        for role in roles:
            case.wait(f"{role} receives success and one point", lambda: case.events(
                role, "ExtractionResultReplicated", **key, Outcome="Success", Score="1"), 3)
        for role in roles:
            path = output / f"{role}-result.png"
            input_until(role, f"shot showui -nosuffix filename={path.as_posix()}", f"{role} captures the result HUD",
                        lambda: path.is_file() and path.stat().st_size > 0)
        case.check("both result HUDs were captured", all((output / f"{role}-result.png").is_file() for role in roles))
        for role in roles:
            input_until(role, "Input.-key S\n" + shared.press("T"), f"{role} loot request after result is rejected",
                        lambda: server("EnemyLootRejected", **key, Reason="NotPlaying", RequesterPlayerId=players[role]))

        starts = server("ExtractionStarted", **key)
        final_events = server("ExtractionResult", **key)
        final = final_events[0]
        held = float(final["fields"]["Held"])
        case.check("reentry requires a new full three-second hold",
                   len(starts) == 2 and len(server("ExtractionReset", **key)) == 1
                   and float(starts[0]["fields"]["Time"]) < float(reset["fields"]["Time"])
                   < float(starts[1]["fields"]["Time"]) and 3.0 <= held < 3.2)
        case.check("server finalizes once and both clients see exactly one identical outcome",
                   len(final_events) == 1 and all(len(case.events(
                       role, "ExtractionResultReplicated", **key, Outcome="Success", Score="1")) == 1 for role in roles))
        case.check("late loot input reaches the server but leaves the finalized result and team loot unchanged",
                   len(server("EnemyLootGranted", **key)) == 1 and all(server("EnemyLootRejected", **key,
                       Reason="NotPlaying", RequesterPlayerId=players[role], TeamLoot="1") for role in roles))
        observations = {
            role: {
                "progress": case.events(role, "ExtractionProgressReplicated", **key),
                "result": case.events(role, "ExtractionResultReplicated", **key),
            } for role in roles
        }
        details = {"round": round_id, "drop": drop_id, "starts": starts, "reset": reset, "result": final,
                   "observations": observations}
    except (RuntimeError, KeyError, IndexError, OSError, ValueError) as exc:
        errors.append(str(exc))
    finally:
        case.finish()

    failures = errors + [item["description"] for item in case.checks if not item["passed"]]
    result = {"status": "FAIL" if failures else "PASS", "count": len(case.checks), "mover": mover,
              "players": players, "details": details, "failures": failures, "checks": case.checks}
    (output / "result.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"status": result["status"], "checks": result["count"], "mover": mover,
                      "failures": failures}, ensure_ascii=False), flush=True)
    return bool(failures)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("stage", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--mover", choices=("a", "b"), default="b")
    args = parser.parse_args()
    sys.exit(run(args.stage.resolve(), args.output.resolve(), args.mover))
