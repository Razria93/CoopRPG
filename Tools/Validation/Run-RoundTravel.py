"""Verify packaged round progression, same-map retry, and terminal results."""

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

ROLES = ("a", "b")
FIRST_MAP = "L_Movement"
NEXT_MAP = "L_Movement_Round2"
FOREST_MAP = "L_Round_01"
SCENARIOS = ("success", "death", "timeout", "disconnect")
ARRIVAL = { "Phase": "Waiting", "Ready": "0", "Open": "0", "HP": "100.00",
    "Stamina": "100.00", "Cooldown": "0", "Equipped": "0", "Recovery": "0",
    "ChestRecovery": "1", "ChestWeapons": "2", "EnemyHP": "100.00", "TeamLoot": "0",
    "Score": "0", "Clock": "0", "Extraction": "0",
}


def trial(stage, output, scenario, start_map):
    case = shared.Case(stage, output)
    if scenario in ("death", "timeout"):
        destination = start_map
    elif scenario == "success" and start_map == FIRST_MAP:
        destination = NEXT_MAP
    else:
        destination = None
    forest = start_map == FOREST_MAP
    arrival_defaults = {**ARRIVAL, "Map": destination}
    evidence = {}
    error = None
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        sock.bind(("127.0.0.1", 0))
        port = sock.getsockname()[1]

    def server(code, **fields):
        return case.events("server", code, **fields)

    def command(role, text, description, predicate, timeout=12):
        case.signal(role, text)
        try:
            case.wait(description, predicate, timeout)
        finally:
            case.signal(role, "")

    def snapshot(role, label, round_id, local_expected=None):
        fields = {"Label": label, "Round": round_id}
        def client_snapshots():
            return [event for event in case.events(role, "TestRoundSnapshot", **fields)
                    if all(event["fields"].get(key) == value for key, value in (local_expected or {}).items())]

        command(role, f"Coop.Test.RoundSnapshot {label}", f"{role} snapshot {label}",
                lambda: server("TestRoundSnapshot", **fields) and client_snapshots())
        return {"server": server("TestRoundSnapshot", **fields)[0]["fields"],
                role: client_snapshots()[0]["fields"]}

    def open_chest(role, player, opens=1):
        command(role, shared.press("E"), f"{role} looks inside the chest",
                lambda: len(server("ChestOpened", RequesterPlayerId=player)) >= opens)

    def capture_hud(role, label):
        picture = output / f"{role}-{label}.png"
        command(role, f"shot showui -nosuffix filename={picture.as_posix()}", f"{role} captures {label}",
                lambda: picture.is_file() and picture.stat().st_size > 0)

    try:
        case.start("server", [f"/Game/Maps/{start_map}?EnemyCombat", f"-port={port}",
                              "-MULTIHOME=127.0.0.1", "-CoopCombatTest"], 25, server_quit_frame=1500)
        case.wait("dedicated server listening", lambda: (output / "server.log").exists()
                  and f"listening on port {port}" in (output / "server.log").read_text(encoding="utf-8", errors="replace"), 55)
        for role in ROLES:
            case.start(role, [f"127.0.0.1:{port}", "-RenderOffscreen", "-windowed", "-ResX=960", "-ResY=540",
                              "-CoopCombatTest"], 30,
                       [(110, "Input.+key R"), (111, "Input.-key R"),
                        (120, "Input.+key W"), (153, "Input.-key W")], client_driver_end=1650)
        case.wait("old round starts with two players", lambda: server("LootRoundPhase", Phase="Playing"), 55)
        old_id = server("LootRoundPhase", Phase="Playing")[0]["fields"]["Round"]
        old = {"Round": old_id}
        case.wait("both input logs flush their ready and owner IDs", lambda: all(
            any("LocalPlayerId" in row["fields"] for row in case.events(role, "LootReadyRequested", **old))
            for role in ROLES))
        time.sleep(1.75)  # let the initial W pulse settle at the chest before the first E
        players = {role: case.events(role, "LootReadyRequested", **old)[0]["fields"]["LocalPlayerId"] for role in ROLES}
        evidence["old_round"] = old_id
        for role in ROLES:
            open_chest(role, players[role])
            command(role, shared.press("G"), f"{role} equips a weapon",
                    lambda: server("ChestWeaponGranted", RequesterPlayerId=players[role]))
        command("a", shared.press("F"), "A takes the old round's medicine",
                lambda: server("ChestGranted", RequesterPlayerId=players["a"]))
        dirty_expectations = {"Map": start_map, "Phase": "Playing", "Equipped": "1", "Recovery": "1",
                              "ChestRecovery": "0", "ChestWeapons": "0", "Open": "1"}
        dirty = snapshot("a", "BeforeTravel", old_id, dirty_expectations)
        evidence["before"] = dirty
        case.check("old round has spent stock, an equipped weapon, and an open chest", all(
            row[key] == value for row in dirty.values() for key, value in dirty_expectations.items()))
        for role in ROLES:
            command(role, shared.press("Q"), f"{role} closes the old chest",
                    lambda: case.events(role, "ChestViewReplicated", Open="0"))

        if scenario in ("success", "death"):
            vitals = ("10", "70") if scenario == "success" else ("100", "10")
            command("a", "Coop.Test.Vitals " + " ".join(vitals), "server sets up reproducible health values",
                    lambda: server("TestVitals", HP=f"{vitals[0]}.00", AttackerHP=f"{vitals[1]}.00"))
            if forest:
                layout = "shieldDown" if scenario == "success" else "counter44"
                command("a", f"Coop.Test.MeleeLayout {layout}", "server prepares the shield encounter",
                        lambda: any(row["fields"]["Layout"].lower() == layout.lower()
                                    for row in server("MeleeTestLayout")))
            else:
                case.signal("a", "Input.+key W")
                try:
                    case.wait("enemy begins a windup targeted at player A",
                              lambda: server("EnemyAttackStarted", TargetPlayerId=players["a"]))
                finally:
                    case.signal("a", "Input.-key W")
            if scenario == "success":
                command("a", "Input.-key W\n" + shared.press("LeftMouseButton"), "A's attack kills the enemy",
                        lambda: server("EnemyLootSpawned", **old))
                drop = server("EnemyLootSpawned", **old)[0]["fields"]["DropId"]
                case.wait("both clients receive the old loot", lambda: all(
                    case.events(role, "EnemyLootReplicated", DropId=drop) for role in ROLES))
                command("a", "Coop.Test.LootPosition near", "A moves to loot range",
                        lambda: server("TestEnemyLootPosition", DropId=drop, Position="Near"))
                command("a", shared.press("T"), "server grants the old team's loot",
                        lambda: server("EnemyLootGranted", **old, DropId=drop, TeamLoot="0->1"))
                for role in ROLES:
                    command(role, "Input.-key W\nCoop.Test.ExtractionPosition inside", f"{role} enters extraction",
                            lambda: server("TestExtractionPosition", Inside="1", RequesterPlayerId=players[role]))
            else:
                case.wait("real enemy hit deals lethal damage", lambda: server(
                    "EnemyAttackHit", TargetPlayerId=players["a"], HP="10.00->0.00"))
        elif scenario == "timeout":
            command("a", "Coop.Test.RoundEnd timeout", "server sets a near timeout",
                    lambda: server("TestRoundEndPrepared", **old, Scenario="timeout"))
        else:
            case.expected_exits.add("b")
            case.signal("b", "quit")
            case.wait("B exits cleanly", lambda: case.processes["b"].poll() is not None, 10)
            case.check("B closes its real client connection", case.processes["b"].returncode == 0)

        result_event, reason, score = {
            "success": ("ExtractionResult", "Extracted", "1"), "death": ("RoundResult", "Death", "0"),
            "timeout": ("RoundResult", "Timeout", "0"),
            "disconnect": ("RoundResult", "Disconnected", "0"),
        }[scenario]
        case.wait("server finalizes the old round", lambda: server(result_event, **old))
        result, = server(result_event, **old)
        evidence["old_result"] = result
        case.check("the old round ends for the intended reason and score",
                   result["fields"]["Reason"] == reason and result["fields"]["Score"] == score)
        remaining = ("a",) if scenario == "disconnect" else ROLES
        for role in remaining:
            case.wait(f"{role} draws the round result", lambda: case.events(role, "RoundResultDisplayed", **old))
            capture_hud(role, "result")
            case.check(f"{role} sees the same score and reason exactly once",
                       len(case.events(role, result_event + "Replicated", **old, Score=score, Reason=reason)) == 1
                       and len(case.events(role, "RoundResultDisplayed", **old)) == 1)

        if destination:
            case.wait("server starts one transition to the expected map after the result",
                      lambda: server("RoundTravelStarted", TargetMap=f"/Game/Maps/{destination}", **old), 18)
        else:
            if scenario != "disconnect":
                case.wait("both players acknowledge the terminal success", lambda: len(server(
                    "RoundResultAcknowledged", **old)) == 2)
            time.sleep(5.0)
            case.check("the server holds the terminal result beyond the travel delay",
                       not server("RoundTravelStarted") and len(server("LootRoundCreated")) == 1
                       and len(server(result_event, **old)) == 1)
        trace = shared.read_events(output / "server.log")
        acks = server("RoundResultAcknowledged", **old)
        travel = server("RoundTravelStarted", **old)
        evidence["travel"] = {"acknowledgements": acks, "start": travel}
        if scenario != "disconnect":
            case.check("the same two owners acknowledge the result in order",
                       len(acks) == 2 and {row["fields"]["PlayerId"] for row in acks} == set(players.values())
                       and [row["fields"]["Count"] for row in acks] == ["1", "2"]
                       and trace.index(result) < trace.index(acks[0]) < trace.index(acks[1]))
        if destination:
            case.check("precisely one travel starts after both acknowledgements",
                       len(travel) == 1 and trace.index(acks[1]) < trace.index(travel[0]))
            def new_rounds():
                return [row for row in server("LootRoundCreated", Map=destination) if row["fields"]["Round"] != old_id]

            case.wait("the same server creates a new round on the expected map", new_rounds, 23)
            new_id = new_rounds()[0]["fields"]["Round"]
            new = {"Round": new_id}
            evidence["new_round"] = new_id
            case.check("the new world creates exactly one new RoundId", old_id != new_id and len(new_rounds()) == 1)
            arrivals = {}
            for role in ROLES:
                welcome = f"Welcomed by server (Level: /Game/Maps/{destination},"
                expected_count = 2 if destination == start_map else 1
                case.wait(f"{role} reconnects to the destination map", lambda:
                          (output / f"{role}.log").read_text(encoding="utf-8", errors="replace").count(welcome)
                          >= expected_count, 23)
                arrivals[role] = snapshot(role, f"Arrival{role}", new_id, arrival_defaults)
                capture_hud(role, "arrival")
                case.check(f"{role} and its authority observe all new-world defaults", all(
                    row.get(key) == value for row in arrivals[role].values() for key, value in arrival_defaults.items()))
            evidence["arrival"] = arrivals
            new_players = {role: arrivals[role][role]["LocalPlayerId"] for role in ROLES}
            case.check("both remote peers correspond to distinct server-owned Pawns", len(set(new_players.values())) == 2 and all(
                arrivals[role]["server"]["RequesterPlayerId"] == new_players[role] for role in ROLES))

            movements = {}
            for role in ROLES:
                case.signal(role, "Input.+key W\n" + "defer " * 33 + "Input.-key W")
                time.sleep(0.20)
                case.signal(role, "")
                time.sleep(1.30)
                moved = snapshot(role, f"Moved{role}", new_id)
                movements[role] = moved
                case.check(f"{role} can move its new Pawn, verified locally and on the server", all(
                    abs(float(row["X"]) - float(arrivals[role][source]["X"])) > 100 for source, row in moved.items()))
            evidence["movement"] = movements
            command("a", shared.press("R"), "A readies alone in the new round",
                    lambda: server("LootPlayerReady", **new, RequesterPlayerId=new_players["a"]))
            one = {role: snapshot(role, f"OnceReady{role}", new_id, {"Ready": "1" if role == "a" else "0"})
                   for role in ROLES}
            evidence["one_ready"] = one
            case.check("one ready player leaves the round waiting and the clock stopped", all(
                row["Ready"] == ("1" if role == "a" else "0") and row["Phase"] == "Waiting" and row["Clock"] == "0"
                for role, snapshots in one.items() for row in snapshots.values()) and not server("RoundClockStarted", **new))
            command("b", shared.press("R"), "B readies and starts the new round",
                    lambda: server("LootRoundPhase", **new, Phase="Playing"))
            clock, = server("RoundClockStarted", **new)
            evidence["new_clock"] = clock
            case.check("the next round starts its own 300-second deadline", clock["fields"]["Duration"] == "300"
                       and abs(float(clock["fields"]["Deadline"]) - float(clock["fields"]["Time"]) - 300) <= .01)
            command("a", shared.press("E"), "A opens the destination chest", lambda: server("ChestOpened", **new))
            before = snapshot("a", "BeforeReplay", new_id)
            old_request = {"RequesterPlayerId": new_players["a"], "Action": "Take", "Reason": "WrongRound", "Round": old_id}
            command("a", f"Coop.Test.StalePickup {old_id}", "the server rejects five replayed old-round requests",
                    lambda: len(server("ChestRejected", **old_request)) >= 5)
            after = snapshot("a", "AfterReplay", new_id)
            evidence["replay"] = {"before": before, "after": after, "rejections": server("ChestRejected", **old_request)}
            case.check("old requests change neither inventory nor the new stock", all(
                before[source]["Recovery"] == after[source]["Recovery"] == "0"
                and before[source]["ChestRecovery"] == after[source]["ChestRecovery"] == "1" for source in after)
                and not server("ChestGranted", **new))
            command("a", shared.press("F"), "a fresh request is accepted in the new round", lambda: server("ChestGranted", **new))
            fresh = snapshot("a", "AfterFresh", new_id, {"Recovery": "1", "ChestRecovery": "0"})
            evidence["fresh"] = fresh
            case.check("the new sequence starts at 1, fresh request grants once and stock reaches zero", all(
                fields["Recovery"] == "1" and fields["ChestRecovery"] == "0" for fields in fresh.values())
                and len(server("ChestGranted", **new, Request="1")) == 1 and not server("RoundTravelFailed"))
    except (IndexError, KeyError, OSError, RuntimeError, ValueError) as exc:
        error = str(exc)
    finally:
        case.finish()
    result = {"scenario": scenario, "map": start_map, "checks": case.checks, "evidence": evidence, "error": error}
    result["passed"] = not error and all(check["passed"] for check in case.checks)
    (output / "result.json").write_text(json.dumps(result, indent=2), encoding="utf-8")
    return result



def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--stage", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--map", choices=(FIRST_MAP, FOREST_MAP), default=FIRST_MAP)
    parser.add_argument("--scenario", choices=SCENARIOS, action="append")
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=False)
    scenarios = args.scenario or SCENARIOS
    results = []
    for name in scenarios:
        result = trial(args.stage.resolve(), args.output.resolve() / name, name, args.map)
        results.append(result)
        failed = [check["description"] for check in result["checks"] if not check["passed"]]
        print(result["scenario"], "PASS" if result["passed"] else "FAIL", failed, result["error"], flush=True)
    (args.output / "summary.json").write_text(json.dumps(results, indent=2), encoding="utf-8")
    return 0 if all(result["passed"] for result in results) else 1


if __name__ == "__main__":
    sys.exit(main())
