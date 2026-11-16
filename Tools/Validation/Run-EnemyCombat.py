"""Verify enemy pursuit, melee, and recovery in the packaged two-player scenario."""

import argparse
from collections import Counter
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


def run(stage, output, target_role, verify_health_subject):
    case = shared.Case(stage, output)
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        sock.bind(("127.0.0.1", 0))
        port = sock.getsockname()[1]

    roles = ("a", "b")
    observing_role = "b" if target_role == "a" else "a"
    players = {}
    round_id = ""
    errors = []
    observed = {}

    def server(code, **fields):
        return case.events("server", code, **fields)

    def input_until(role, commands, label, predicate, release=""):
        case.signal(role, commands)
        try:
            case.wait(label, predicate)
        finally:
            case.signal(role, release)

    def press(role, key, label, predicate):
        input_until(role, shared.press(key), label, predicate)

    def player_health(role, **fields):
        return [event for event in case.events(role, "HealthReplicated", **fields)
                if event["fields"]["Actor"].startswith("CoopRPGCharacter_")]

    try:
        map_url = "/Game/Maps/L_Movement?EnemyCombat"
        case.start("server", [map_url, f"-port={port}", "-MULTIHOME=127.0.0.1"], 20)
        case.wait("dedicated server listening", lambda: (output / "server.log").exists()
                  and f"listening on port {port}" in (output / "server.log").read_text(encoding="utf-8", errors="replace"), 55)
        for role in roles:
            inputs = [(110, "Input.+key R"), (111, "Input.-key R")]
            if role == target_role:
                inputs += [(120, "Input.+key W"), (153, "Input.-key W")]
            case.start(role, [f"127.0.0.1:{port}", "-RenderOffscreen", "-windowed", "-ResX=960", "-ResY=540"], 30, inputs)
        case.wait("both ready and the round is playing", lambda: server("LootRoundPhase", Phase="Playing"), 55)
        round_id = server("LootRoundPhase", Phase="Playing")[0]["fields"]["Round"]
        for role in roles:
            calls = case.events(role, "LootReadyRequested", Round=round_id, NetMode="Client")
            players[role] = calls[0]["fields"]["LocalPlayerId"]
        case.check("two ready players on separate client processes",
                   len(set(players.values())) == 2 and len(server("LootPlayerReady", Round=round_id)) == 2)

        player_id = players[target_role]
        victim = {"TargetPlayerId": player_id}
        press(target_role, "E", "target opens the chest", lambda: case.events(target_role, "ChestViewReplicated", Open="1"))
        press(target_role, "F", "target has one recovery item", lambda: case.events(target_role, "RecoveryCountReplicated", Count="1"))
        press(target_role, "G", "target equips the weapon", lambda: server("ChestWeaponGranted", RequesterPlayerId=player_id))
        case.check("chest can be looted before enemy detection", not server("EnemyDetected") and not server("EnemyAttackHit"))

        input_until(target_role, "Input.+key W", "server detects approaching player",
                    lambda: server("EnemyDetected", **victim), release="Input.-key W")
        detected = server("EnemyDetected", **victim)[0]
        case.wait("server pursues the detected player", lambda: server("EnemyChaseStarted", **victim))
        chase = server("EnemyChaseStarted", **victim)[0]
        case.wait("enemy winds up the first attack", lambda: server("EnemyAttackStarted", **victim))
        first_windup = server("EnemyAttackStarted", **victim)[0]
        case.check("enemy closes from detection distance into melee range",
                   135 < float(chase["fields"]["Distance"]) <= 360
                   and 0 < float(first_windup["fields"]["Distance"]) <= 135
                   and detected["time"] <= chase["time"] < first_windup["time"])
        input_until(target_role, "Input.-key W\nInput.+key S", "retreat before contact results in a range miss",
                    lambda: server("EnemyAttackMissed", Reason="OutOfRange", **victim), release="Input.-key S")
        miss = server("EnemyAttackMissed", Reason="OutOfRange", **victim)[0]
        case.check("miss leaves both players at original health", float(miss["fields"]["Distance"]) > 135
                   and not server("EnemyAttackHit") and not any(player_health(role) for role in roles))
        for role in roles:
            case.wait(f"{role} observes enemy movement", lambda: case.events(
                role, "EnemyMovementObserved", NetMode="Client", LocalRole="SimulatedProxy"))
        case.check("both clients observe at least 40 cm of replicated enemy movement", all(float(
            case.events(role, "EnemyMovementObserved")[0]["fields"]["Distance"]) >= 40 for role in roles))

        case.signal(target_role, "Input.-key S\nInput.+key W")
        try:
            # Re-enter the detection area, then wait while the enemy recovers from its miss.
            time.sleep(0.55)
        finally:
            case.signal(target_role, "Input.-key W")
        case.wait("enemy winds up again in range", lambda: len(server("EnemyAttackStarted", **victim)) >= 2)
        case.wait("enemy applies the damage Gameplay Effect", lambda: server("EnemyAttackHit", HP="100.00->90.00", **victim))
        hit = server("EnemyAttackHit", HP="100.00->90.00", **victim)[0]
        case.check("only the intended target is damaged in range", len(server("EnemyAttackHit")) == 1
                   and float(hit["fields"]["Distance"]) <= 135 and hit["fields"]["NetMode"] == "DedicatedServer")
        for role in roles:
            local_role = "AutonomousProxy" if role == target_role else "SimulatedProxy"
            case.wait(f"{role} receives the victim's damage", lambda: player_health(
                role, Before="100.00", After="90.00", LocalRole=local_role))

        press(target_role, "H", "injured player uses one recovery item", lambda: server("RecoveryUsed",
              HP="90.00->100.00", Recovery="1->0", RequesterPlayerId=player_id))
        case.wait("owner receives the private count", lambda: case.events(target_role, "RecoveryCountReplicated", Count="0"))
        for role in roles:
            local_role = "AutonomousProxy" if role == target_role else "SimulatedProxy"
            case.wait(f"{role} receives the victim's recovery", lambda: player_health(
                role, Before="90.00", After="100.00", LocalRole=local_role))
        case.check("both see only the intended player's damage and recovery; count is owner-only",
                   all(len(player_health(role)) == 2 for role in roles)
                   and [e["fields"]["Count"] for e in case.events(target_role, "RecoveryCountReplicated")] == ["1", "0"]
                   and not case.events(observing_role, "RecoveryCountReplicated") and len(server("RecoveryUsed")) == 1)

        press(target_role, "LeftMouseButton", "player commits an attack on the moving enemy",
              lambda: any(e["fields"]["Target"].startswith("CoopRPGEnemy_") for e in server("AttackStarted")))
        case.wait("the server accepts the armed player's attack", lambda: any(
            e["fields"]["Target"].startswith("CoopRPGEnemy_") for e in server("AttackApproved", HP="100.00->90.00")))
        for role in roles:
            case.wait(f"{role} observes enemy damage", lambda: any(e["fields"]["Actor"].startswith("CoopRPGEnemy_")
                      for e in case.events(role, "HealthReplicated", Before="100.00", After="90.00")))
        case.check("armed player damages the enemy and both clients receive enemy HP",
                   len(server("AttackApproved", RequesterPlayerId=player_id, HP="100.00->90.00")) == 1)
        if verify_health_subject:
            for role in roles:
                player_rows = player_health(role)
                enemy_rows = [e for e in case.events(role, "HealthReplicated")
                              if e["fields"]["Actor"].startswith("CoopRPGEnemy_")]
                case.check(f"{role} raw HP rows identify the real owner even when remote",
                           len(player_rows) == 2 and all(e["fields"].get("SubjectType") == "Player"
                           and e["fields"].get("SubjectPlayerId") == player_id for e in player_rows))
                case.check(f"{role} raw enemy HP row identifies the enemy",
                           len(enemy_rows) == 1 and enemy_rows[0]["fields"].get("SubjectType") == "Enemy"
                           and "SubjectPlayerId" not in enemy_rows[0]["fields"])
        case.check("normal inputs and enemy AI supply all damage and healing", not server("TestVitals"))
        observed = {label: event["fields"] for label, event in (("detect", detected), ("chase", chase),
                    ("firstWindup", first_windup), ("miss", miss), ("hit", hit))}
    except (RuntimeError, KeyError, IndexError, OSError, ValueError) as exc:
        errors.append(str(exc))
    finally:
        case.finish()

    failures = errors + [check["description"] for check in case.checks if not check["passed"]]
    result = {"status": "FAIL" if failures else "PASS", "count": len(case.checks), "failures": failures,
              "players": players, "round": round_id, "observed": observed, "checks": case.checks,
              "serverEventCounts": dict(Counter(event["code"] for event in shared.read_events(output / "server.log")
                                                if event["code"].startswith("Enemy")))}
    (output / "result.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({key: value for key, value in result.items() if key != "checks"}), flush=True)
    return bool(failures)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("stage", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--target", choices=("a", "b"), default="a")
    parser.add_argument("--verify-health-subject", action="store_true")
    args = parser.parse_args()
    sys.exit(run(args.stage.resolve(), args.output.resolve(), args.target, args.verify_health_subject))
