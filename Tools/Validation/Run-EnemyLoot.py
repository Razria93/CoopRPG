"""Verify one team reward per drop with a packaged Server and two Clients."""

import argparse
from datetime import datetime
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


def run(stage, output, far_role, outbound_lag):
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

    def input_until(role, commands, description, predicate):
        case.signal(role, commands)
        try:
            case.wait(description, predicate)
        finally:
            case.signal(role, "")

    try:
        case.start("server", ["/Game/Maps/L_Movement?EnemyCombat", f"-port={port}", "-MULTIHOME=127.0.0.1",
                              "-CoopCombatTest", f"-PktLag={outbound_lag}"], 25)
        case.wait("dedicated server listening", lambda: (output / "server.log").exists()
                  and f"listening on port {port}" in (output / "server.log").read_text(encoding="utf-8", errors="replace"), 55)
        for role in roles:
            case.start(role, [f"127.0.0.1:{port}", "-RenderOffscreen", "-windowed", "-ResX=960", "-ResY=540",
                              "-CoopCombatTest"], 30,
                       [(110, "Input.+key R"), (111, "Input.-key R"),
                        (120, "Input.+key W"), (153, "Input.-key W")])
        case.wait("both players are ready", lambda: server("LootRoundPhase", Phase="Playing"), 55)
        round_id = server("LootRoundPhase", Phase="Playing")[0]["fields"]["Round"]
        for role in roles:
            players[role] = case.events(role, "LootReadyRequested", Round=round_id)[0]["fields"]["LocalPlayerId"]
            input_until(role, shared.press("E"), f"{role} opens the chest",
                        lambda: case.events(role, "ChestViewReplicated", Open="1"))
            input_until(role, shared.press("G"), f"{role} equips a weapon",
                        lambda: server("ChestWeaponGranted", RequesterPlayerId=players[role]))
            input_until(role, shared.press("Q"), f"{role} closes the chest",
                        lambda: case.events(role, "ChestViewReplicated", Open="0"))
        case.check("two ready and armed players", len(set(players.values())) == len(server("ChestWeaponGranted")) == 2)

        input_until("a", "Coop.Test.Vitals 10 100", "server sets the enemy to 10 HP",
                    lambda: server("TestVitals", HP="10.00", AttackerHP="100.00"))
        case.signal("a", "Input.+key W")
        try:
            case.wait("enemy starts an attack near the approaching player", lambda: server("EnemyAttackStarted"))
        finally:
            case.signal("a", "Input.-key W")
        input_until("a", "Input.-key W\n" + shared.press("LeftMouseButton"), "server spawns the defeated enemy's loot",
                    lambda: server("EnemyLootSpawned", Round=round_id))
        spawned = server("EnemyLootSpawned", Round=round_id)[0]
        drop_id = spawned["fields"]["DropId"]
        identity = {"Round": round_id, "DropId": drop_id}
        for role in roles:
            case.wait(f"{role} receives the same DropId", lambda: case.events(role, "EnemyLootReplicated", DropId=drop_id))
        case.check("one server drop and matching ID observed by both clients",
                   len(server("EnemyLootSpawned")) == 1 and all(
                       len(case.events(role, "EnemyLootReplicated", DropId=drop_id)) == 1 for role in roles))

        def place(role, position, predicate):
            input_until(role, "Input.-key W\nCoop.Test.LootPosition " + position.lower(), f"{role} moves {position}",
                        lambda: (events := server("TestEnemyLootPosition", DropId=drop_id, Position=position,
                                                   RequesterPlayerId=players[role]))
                        and predicate(float(events[-1]["fields"]["Distance"])))

        place(far_role, "Far", lambda distance: distance > 250)
        input_until(far_role, shared.press("T"), "five out-of-range requests are rejected while reward stays zero",
                    lambda: len(server("EnemyLootRejected", **identity, Reason="OutOfRange",
                                       RequesterPlayerId=players[far_role], TeamLoot="0")) >= 5)
        invalid = server("EnemyLootRejected", **identity, Reason="OutOfRange", TeamLoot="0")
        case.check("five out-of-range Client RPCs leave team count zero",
                   bool(case.events(far_role, "EnemyLootTakeRequested", **identity))
                   and len(invalid) >= 5 and not server("EnemyLootGranted"))
        for role in roles:
            place(role, "Near", lambda distance: distance < 250)

        # Release both gates before waiting for either response, then stop issuing inputs from each client.
        starts = {role: len(case.events(role, "EnemyLootTakeRequested", **identity)) for role in roles}
        for role in roles:
            case.signal(role, shared.press("T"))
        try:
            for role in roles:
                case.wait(f"{role} sends its competing request",
                          lambda: len(case.events(role, "EnemyLootTakeRequested", **identity)) > starts[role])
                case.signal(role, "")
        finally:
            for role in roles:
                case.signal(role, "")
        case.wait("server decides the competing requests", lambda: server("EnemyLootGranted", **identity)
                  and server("EnemyLootRejected", **identity, Reason="AlreadyClaimed"))
        grant = server("EnemyLootGranted", **identity)[0]
        winner = grant["fields"]["RequesterPlayerId"]
        loser = next(role for role, player in players.items() if player != winner)
        case.wait("server rejects the other player's request", lambda: server("EnemyLootRejected", **identity,
                  Reason="AlreadyClaimed", RequesterPlayerId=players[loser], TeamLoot="1"))
        loss = server("EnemyLootRejected", **identity, Reason="AlreadyClaimed", RequesterPlayerId=players[loser])[0]
        server_gap_ms = round((datetime.fromisoformat(loss["time"]) - datetime.fromisoformat(grant["time"]))
                              .total_seconds() * 1000)
        case.check("first valid request grants 0->1, competing request loses within 250 ms",
                   grant["fields"]["TeamLoot"] == "0->1" and 0 <= server_gap_ms <= 250)

        def team(role):
            return case.events(role, "TeamLootCountReplicated", Before="0", After="1")

        for role in roles:
            case.wait(f"{role} sees one team reward and the claimed drop",
                      lambda: team(role) and case.events(role, "EnemyLootClaimReplicated", DropId=drop_id, Claimed="1"), 3)
        grant_time = datetime.fromisoformat(grant["time"])
        replication_ms = {role: round((datetime.fromisoformat(team(role)[0]["time"]) - grant_time)
                                      .total_seconds() * 1000) for role in roles}
        case.check("each Client sent its contention request before observing the team result",
                   all(case.events(role, "EnemyLootTakeRequested", **identity)[starts[role]]["time"]
                       <= team(role)[0]["time"] for role in roles))

        winner_role = next(role for role, player in players.items() if player == winner)
        def duplicates():
            return server("EnemyLootRejected", **identity, Reason="AlreadyClaimed",
                          RequesterPlayerId=winner, TeamLoot="1")

        before = len(duplicates())
        input_until(winner_role, shared.press("T"), "winner sends five more identical requests",
                    lambda: len(duplicates()) >= before + 5)
        time.sleep(0.4)
        case.check("at least five repeats of the same Round/Drop from the winner produce no extra grant",
                   len(duplicates()) - before >= 5 and len(server("EnemyLootGranted")) == 1)
        case.check("both clients observe one quantity change to 1 within 3 s and one claim for the same drop",
                   all(len(case.events(role, "TeamLootCountReplicated")) == len(team(role)) == 1
                       and 0 <= replication_ms[role] <= 3000
                       and len(case.events(role, "EnemyLootClaimReplicated", DropId=drop_id, Claimed="1")) == 1
                       for role in roles))
        details = {"round": round_id, "dropId": drop_id, "rangeRejection": invalid[0], "grant": grant, "loss": loss,
                   "serverGapMs": server_gap_ms, "outOfRange": len(invalid), "duplicates": len(duplicates()) - before,
                   "replicationMs": replication_ms,
                   "team": {role: team(role)[0] for role in roles}}
    except (RuntimeError, KeyError, IndexError, OSError, StopIteration, ValueError) as exc:
        errors.append(str(exc))
    finally:
        case.finish()

    failures = errors + [item["description"] for item in case.checks if not item["passed"]]
    result = {"status": "FAIL" if failures else "PASS", "count": len(case.checks), "farRole": far_role,
              "serverOutboundLagMs": outbound_lag,
              "players": players, "details": details, "failures": failures, "checks": case.checks}
    (output / "result.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({key: value for key, value in result.items() if key != "checks"}), flush=True)
    return bool(failures)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("stage", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--far", choices=("a", "b"), default="a")
    parser.add_argument("--outbound-lag", type=int, choices=(0, 160), default=160,
                        help="Delay only server output so both Client requests can precede the outcome.")
    args = parser.parse_args()
    sys.exit(run(args.stage.resolve(), args.output.resolve(), args.far, args.outbound_lag))
