"""Exercise V06 round-end reasons, conflict ordering, and post-result rules."""

import argparse
from concurrent.futures import ThreadPoolExecutor
import importlib.util
import json
from pathlib import Path
import socket
import subprocess
import sys


sys.dont_write_bytecode = True
spec = importlib.util.spec_from_file_location("shared_chest", Path(__file__).with_name("Run-SharedChest.py"))
shared = importlib.util.module_from_spec(spec)
spec.loader.exec_module(shared)

EXPECTATIONS = {
    "death": ("Death", "Failed", ("Death",)),
    "timeout": ("Timeout", "Failed", ("Timeout",)),
    "disconnect": ("Disconnected", "Aborted", ("Disconnect",)),
    "death-timeout": ("Death", "Failed", ("Death", "Timeout")),
    "timeout-extraction": ("Timeout", "Failed", ("Timeout", "Extraction")),
    "disconnect-death": ("Disconnected", "Aborted", ("Disconnect", "Death")),
}


def run(stage, output, scenario):
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
        case.wait("round starts with two players", lambda: server("LootRoundPhase", Phase="Playing"), 55)
        round_id = server("LootRoundPhase", Phase="Playing")[0]["fields"]["Round"]
        key = {"Round": round_id}
        clock, = server("RoundClockStarted", **key)
        case.check("production deadline is 300 server-clock seconds from start",
                   clock["fields"]["Duration"] == "300"
                   and abs(float(clock["fields"]["Deadline"]) - float(clock["fields"]["Time"]) - 300) <= 0.001)
        for role in roles:
            players[role] = case.events(role, "LootReadyRequested", **key)[0]["fields"]["LocalPlayerId"]
            input_until(role, shared.press("E"), f"{role} opens the chest",
                        lambda: case.events(role, "ChestViewReplicated", Open="1"))
            input_until(role, shared.press("G"), f"{role} equips a weapon",
                        lambda: server("ChestWeaponGranted", RequesterPlayerId=players[role]))
            input_until(role, shared.press("Q"), f"{role} closes the chest",
                        lambda: case.events(role, "ChestViewReplicated", Open="0"))
        case.check("both real clients enter, ready and receive their weapons",
                   len(set(players.values())) == len(server("ChestWeaponGranted")) == 2)

        def prepare(role, action):
            input_until(role, "Coop.Test.RoundEnd " + action, f"server prepares {action}",
                        lambda: server("TestRoundEndPrepared", **key, Scenario=action))

        def approach_enemy():
            input_until("a", "Input.+key W", "enemy begins a windup targeted at player a",
                        lambda: server("EnemyAttackStarted", TargetPlayerId=players["a"]), "Input.-key W")

        if scenario == "death":
            input_until("a", "Coop.Test.Vitals 100 10", "test prepares A's HP 10",
                        lambda: server("TestVitals", HP="100.00", AttackerHP="10.00"))
            approach_enemy()
            case.wait("real enemy strike applies lethal GAS damage",
                      lambda: server("EnemyAttackHit", TargetPlayerId=players["a"], HP="10.00->0.00"))
        elif scenario == "timeout":
            prepare("a", "timeout")
        elif scenario == "death-timeout":
            approach_enemy()
            input_until("a", "Input.-key W\n" + shared.press("LeftMouseButton"), "A commits a pending attack",
                        lambda: server("AttackStarted", RequesterPlayerId=players["a"]))
            prepare("b", "death-timeout")
            case.wait("the live player's queued hit misses after the result",
                      lambda: server("AttackMissed", RequesterPlayerId=players["a"], Reason="NotPlaying", Damage="0"))
        elif scenario == "timeout-extraction":
            input_until("a", "Coop.Test.Vitals 10 100", "enemy HP is prepared for one hit",
                        lambda: server("TestVitals", HP="10.00", AttackerHP="100.00"))
            approach_enemy()
            input_until("a", "Input.-key W\n" + shared.press("LeftMouseButton"), "enemy dies and spawns the drop",
                        lambda: server("EnemyLootSpawned", **key))
            drop = server("EnemyLootSpawned", **key)[0]["fields"]["DropId"]
            case.wait("A receives the drop", lambda: case.events("a", "EnemyLootReplicated", DropId=drop))
            input_until("a", "Coop.Test.LootPosition near", "A is within pickup range",
                        lambda: server("TestEnemyLootPosition", DropId=drop, Position="Near",
                                       RequesterPlayerId=players["a"]))
            input_until("a", shared.press("T"), "real client pickup raises the team count",
                        lambda: server("EnemyLootGranted", **key, DropId=drop, TeamLoot="0->1"))
            for role in roles:
                input_until(role, "Input.-key W\nCoop.Test.ExtractionPosition inside", f"{role} is in the zone",
                            lambda: server("TestExtractionPosition", Inside="1", RequesterPlayerId=players[role]))
            case.wait("an actual three-second hold is underway", lambda: server("ExtractionStarted", **key))
            prepare("a", "timeout-extraction")
            assert (server("ExtractionStarted", **key)[0]["fields"]["Deadline"] ==
                    server("TestRoundEndPrepared", **key, Scenario="timeout-extraction")[0]["fields"]["Deadline"])
        else:
            if scenario == "disconnect-death":
                prepare("a", "death-on-logout")
            case.expected_exits.add("b")
            case.signal("b", "quit")
            case.processes["b"].wait(timeout=10)
            case.check("the second real client closes its connection", case.processes["b"].returncode == 0)
            if scenario == "disconnect-death":
                case.wait("the real logout also sets the remaining player's HP to zero in the same callback",
                          lambda: server("TestPeerLogoutDeath", **key))

        reason, outcome, flags = EXPECTATIONS[scenario]
        expected = {**key, "Reason": reason, "Outcome": outcome, "Score": "0"}
        case.wait("server finalizes exactly the expected result", lambda: server("RoundResult", **expected))
        decision = server("RoundResult", **expected)[0]
        actual_flags = tuple(name for name in ("Disconnect", "Death", "Timeout", "Extraction")
                             if decision["fields"][name] == "1")
        case.check("the server samples exactly the expected conditions in one adjudication",
                   actual_flags == flags and int(decision["fields"]["Frame"]) > 0)
        if scenario in ("timeout", "timeout-extraction"):
            action = "timeout-extraction" if scenario == "timeout-extraction" else "timeout"
            deadline = float(server("TestRoundEndPrepared", **key, Scenario=action)[0]["fields"]["Deadline"])
            case.check("timeout is decided when the server clock reaches its deadline",
                       0.0 <= float(decision["fields"]["Time"]) - deadline < 0.20)

        remaining = ("a",) if "disconnect" in scenario else roles
        for role in remaining:
            case.wait(f"{role} receives {outcome}, {reason}, 0 points",
                      lambda: case.events(role, "RoundResultReplicated", **expected), 3)
            path = output / f"{role}-result.png"
            input_until(role, f"shot showui -nosuffix filename={path.as_posix()}", f"{role} saves result HUD",
                        lambda: path.is_file() and path.stat().st_size > 0)
        case.check("each connected client replicates the same result once",
                   all(len(case.events(role, "RoundResultReplicated", **expected)) == 1 for role in remaining))

        def check_late(role, command, event, **filters):
            identity = {"RequesterPlayerId": players[role], **filters}
            input_until(role, command, f"{role} receives server rejection for a late request",
                        lambda: server(event, **identity))

        for role in remaining:
            check_late(role, shared.press("E"), "ChestRejected", Action="Open", Reason="NotPlaying")
        if scenario == "timeout-extraction":
            check_late("a", shared.press("T"), "EnemyLootRejected", **key, Reason="NotPlaying", TeamLoot="1")
        if scenario in ("death", "timeout", "disconnect"):
            living = "b" if scenario == "death" else "a"
            check_late(living, "Coop.Test.Request", "AttackRejected", Reason="NotPlaying")

        trace = shared.read_events(output / "server.log")
        final_index = trace.index(decision)
        after_result = trace[final_index + 1:]
        case.check("one final result holds through late inputs without extra hits or loot",
                   len(server("RoundResult", **key)) == 1 and not server("ExtractionResult", **key)
                   and not any(event["code"] in ("AttackApproved", "EnemyAttackHit", "EnemyLootGranted",
                                                 "ChestGranted", "ChestWeaponGranted") for event in after_result))
        details = {"round": round_id, "clock": clock, "decision": decision,
                   "clients": {role: case.events(role, "RoundResultReplicated", **expected)[0] for role in remaining}}
    except (AssertionError, RuntimeError, KeyError, IndexError, OSError, ValueError, subprocess.TimeoutExpired) as exc:
        errors.append(str(exc) or type(exc).__name__)
    finally:
        case.finish()

    failures = errors + [item["description"] for item in case.checks if not item["passed"]]
    result = {"status": "FAIL" if failures else "PASS", "scenario": scenario, "count": len(case.checks),
              "players": players, "details": details, "failures": failures, "checks": case.checks}
    (output / "result.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"scenario": scenario, "status": result["status"],
                      "checks": result["count"], "failures": failures}), flush=True)
    return bool(failures)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("stage", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--scenario", choices=EXPECTATIONS, action="append")
    args = parser.parse_args()
    stage = args.stage.resolve()
    output = args.output.resolve()
    scenarios = args.scenario or list(EXPECTATIONS)
    with ThreadPoolExecutor(max_workers=2) as pool:
        failures = list(pool.map(lambda scenario: run(stage, output / scenario, scenario), scenarios))
    sys.exit(any(failures))
