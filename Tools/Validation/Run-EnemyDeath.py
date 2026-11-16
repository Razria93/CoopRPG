"""Verify one server death and one replicated loot Actor for overlapping attacks."""

import argparse
import importlib.util
import json
from pathlib import Path
import re
import socket
import sys
import time


sys.dont_write_bytecode = True
spec = importlib.util.spec_from_file_location("shared_chest", Path(__file__).with_name("Run-SharedChest.py"))
shared = importlib.util.module_from_spec(spec)
spec.loader.exec_module(shared)


def run(stage, output, lead):
    case = shared.Case(stage, output)
    roles = ("a", "b")
    other = "b" if lead == "a" else "a"
    players = {}
    details = {}
    errors = []
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        sock.bind(("127.0.0.1", 0))
        port = sock.getsockname()[1]

    def server(code, **fields):
        return case.events("server", code, **fields)

    def input_until(role, commands, label, predicate, release=""):
        case.signal(role, commands)
        try:
            case.wait(label, predicate)
        finally:
            case.signal(role, release)

    try:
        case.start("server", ["/Game/Maps/L_Movement?EnemyCombat", f"-port={port}", "-MULTIHOME=127.0.0.1",
                              "-CoopCombatTest"], 20)
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
        case.check("two ready and armed players", len(set(players.values())) == len(server("ChestWeaponGranted")) == 2)

        input_until(lead, "Coop.Test.Vitals 10 100", "server sets the enemy to 10 HP",
                    lambda: server("TestVitals", HP="10.00", AttackerHP="100.00"))
        for role in roles:
            case.wait(f"{role} observes initial enemy HP 10", lambda: case.events(
                role, "HealthReplicated", SubjectType="Enemy", After="10.00"))

        for role in roles:
            case.signal(role, "Input.+key W")
        try:
            case.wait("enemy detects both approaching clients' first valid target", lambda: server("EnemyDetected"))
        finally:
            for role in roles:
                case.signal(role, "Input.-key W")
        case.wait("enemy begins a pending attack", lambda: server("EnemyAttackStarted"))
        input_until(lead, shared.press("LeftMouseButton"), f"{lead} starts a lethal hit",
                    lambda: server("AttackStarted", RequesterPlayerId=players[lead]))
        input_until(other, shared.press("LeftMouseButton"), f"{other} also starts a lethal hit",
                    lambda: server("AttackStarted", RequesterPlayerId=players[other]))
        case.wait("one death and one server drop", lambda: server("EnemyDeathConfirmed") and server("EnemyLootSpawned"))
        case.wait("later queued attack fails at hit time", lambda: server(
            "AttackMissed", RequesterPlayerId=players[other], Reason="InvalidTarget", Damage="0", Committed="1"))

        death = server("EnemyDeathConfirmed")[0]
        loot = server("EnemyLootSpawned")[0]
        drop_id = loot["fields"]["DropId"]
        for role in roles:
            case.wait(f"{role} receives death and the same drop ID", lambda: case.events(
                role, "EnemyDeathReplicated", LocalRole="SimulatedProxy") and case.events(
                role, "EnemyLootReplicated", LocalRole="SimulatedProxy", DropId=drop_id))
            case.wait(f"{role} receives final health", lambda: case.events(
                role, "HealthReplicated", SubjectType="Enemy", Before="10.00", After="0.00"))

        # Exceed both the normal 0.9 s windup and the 2.5 s recovery, then try an attack on the dead enemy.
        time.sleep(3.7)
        input_until(lead, shared.press("LeftMouseButton"), "new attack against dead enemy rejected",
                    lambda: server("AttackRejected", RequesterPlayerId=players[lead], Reason="InvalidTarget"))
        trace = shared.read_events(output / "server.log")
        death_index = next(index for index, event in enumerate(trace) if event["code"] == "EnemyDeathConfirmed")
        started = [(index, event) for index, event in enumerate(trace) if event["code"] == "AttackStarted"]
        case.check("both abilities committed before the first lethal resolution",
                   [event["fields"]["RequesterPlayerId"] for _, event in started] == [players[lead], players[other]]
                   and started[-1][0] < death_index)
        hits = server("AttackApproved")
        case.check("first hit wins and later committed hit has zero damage",
                   len(hits) == 1 and hits[0]["fields"].get("HP") == "10.00->0.00"
                   and hits[0]["fields"].get("RequesterPlayerId") == players[lead]
                   and len(server("AttackMissed", RequesterPlayerId=players[other], Damage="0")) == 1)
        case.check("server confirms one death and spawns one drop for that enemy and round",
                   len(server("EnemyDeathConfirmed")) == len(server("EnemyLootSpawned")) == 1
                   and death["fields"]["Round"] == loot["fields"]["Round"] == round_id
                   and death["fields"]["Actor"] == loot["fields"]["Enemy"]
                   and bool(re.fullmatch(r"[0-9A-Fa-f]{32}", drop_id)))
        case.check("death clears a real pending enemy strike and prevents further strikes",
                   death["fields"].get("PendingAttackDiscarded") == "1" and any(
                       event["code"] == "EnemyAttackStarted" for event in trace[:death_index])
                   and not any(event["code"] in ("EnemyAttackStarted", "EnemyAttackHit", "EnemyChaseStarted")
                               for event in trace[death_index + 1:]))
        case.check("each client sees one death and exactly the same loot Actor and ID",
                   all(len(case.events(role, "EnemyDeathReplicated")) ==
                       len(case.events(role, "EnemyLootReplicated")) == 1
                       and case.events(role, "EnemyLootReplicated")[0]["fields"]["DropId"] == drop_id for role in roles))
        details = {"round": round_id, "death": death, "loot": loot, "winner": hits[0]["fields"]["RequesterPlayerId"],
                   "committed": [event["time"] for _, event in started], "lateRequestRejected": True}
    except (RuntimeError, KeyError, IndexError, OSError, StopIteration) as exc:
        errors.append(str(exc))
    finally:
        case.finish()

    failures = errors + [item["description"] for item in case.checks if not item["passed"]]
    result = {"status": "FAIL" if failures else "PASS", "count": len(case.checks), "lead": lead,
              "players": players, "details": details, "failures": failures, "checks": case.checks}
    (output / "result.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({key: value for key, value in result.items() if key != "checks"}), flush=True)
    return bool(failures)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("stage", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--lead", choices=("a", "b"), default="a")
    args = parser.parse_args()
    sys.exit(run(args.stage.resolve(), args.output.resolve(), args.lead))
