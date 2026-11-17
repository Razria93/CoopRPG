"""Verify weapon and recovery actions on a packaged server and two clients."""

import argparse
import importlib.util
import json
from pathlib import Path
import socket
import sys


sys.dont_write_bytecode = True
spec = importlib.util.spec_from_file_location("shared_chest", Path(__file__).with_name("Run-SharedChest.py"))
shared = importlib.util.module_from_spec(spec)
spec.loader.exec_module(shared)


def trial(stage, path):
    case = shared.Case(stage, path)
    roles = ("a", "b")
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        sock.bind(("127.0.0.1", 0))
        port = sock.getsockname()[1]

    players = {}
    round_id = ""
    errors = []

    def server(role, code, **fields):
        return case.events("server", code, RequesterPlayerId=players[role], **fields)

    def own(role, code, **fields):
        return case.events(role, code, LocalPlayerId=players[role], **fields)

    def input_until(role, key, label, predicate):
        case.signal(role, shared.press(key))
        try:
            case.wait(label, predicate)
        finally:
            case.signal(role, "")

    def health(role, actor_prefix, before, after):
        return [event for event in case.events(role, "HealthReplicated", Before=before, After=after)
                if event["fields"]["Actor"].startswith(actor_prefix)]

    try:
        case.start("server", [f"-port={port}", "-MULTIHOME=127.0.0.1", "-CoopCombatTest"], 20)
        case.wait("server listening", lambda: (path / "server.log").exists() and f"listening on port {port}" in
                  (path / "server.log").read_text(encoding="utf-8", errors="replace"), 55)
        for role in roles:
            case.start(role, [f"127.0.0.1:{port}", "-RenderOffscreen", "-windowed", "-ResX=960", "-ResY=540",
                              "-CoopCombatTest"], 30,
                       [(110, "Input.+key R"), (111, "Input.-key R"),
                        (120, "Input.+key W"), (153, "Input.-key W")])
        case.wait("both players ready", lambda: case.events("server", "LootRoundPhase", Phase="Playing"), 55)
        round_id = case.events("server", "LootRoundPhase", Phase="Playing")[0]["fields"]["Round"]
        for role in roles:
            input_until(role, "E", f"{role} opened chest",
                        lambda: case.events(role, "ChestViewReplicated", Open="1", Stock="1"))
            players[role] = case.events(role, "ChestViewReplicated", Open="1")[0]["fields"]["LocalPlayerId"]
        opened = case.events("server", "ChestOpened", Round=round_id)
        case.check("two distinct players opened the same chest",
                   len(set(players.values())) == 2 and len(opened) == 2
                   and len({e["fields"]["Chest"] for e in opened}) == 1)

        for role in roles:
            input_until(role, "LeftMouseButton", f"{role} unarmed attack rejected",
                        lambda: server(role, "AttackRejected", Reason="Unarmed", Stamina="100.00", Cooldown="0"))
        case.check("both unarmed attacks rejected before damage, cost, and cooldown",
                   not any(case.events("server", code) for code in ("AttackStarted", "AttackApproved"))
                   and not case.events("server", "CooldownChanged", Count="1")
                   and not any(case.events(role, "StaminaReplicated") for role in roles))

        for role, transition in zip(roles, ("2->1", "1->0")):
            input_until(role, "G", f"{role} weapon grant", lambda: server(role, "ChestWeaponGranted", Stock=transition))
            for observer in roles:
                local_role = "AutonomousProxy" if observer == role else "SimulatedProxy"
                case.wait(f"{observer} sees {role} equipped", lambda: case.events(
                    observer, "MeleeWeaponReplicated", Equipped="1", EquippedPlayerId=players[role], LocalRole=local_role))
            if role == "a":
                input_until(role, "G", "second weapon refused for a",
                            lambda: server("a", "ChestRejected", Action="TakeWeapon", Reason="AlreadyEquipped"))
                case.check("duplicate equip leaves one copy for the other player",
                           len(case.events("server", "ChestWeaponGranted")) == 1)
        case.check("exactly one weapon granted per player",
                   len(case.events("server", "ChestWeaponGranted")) == 2
                   and all(len(server(role, "ChestWeaponGranted", Round=round_id, Equipped="1")) == 1 for role in roles))
        for role in roles:
            case.wait(f"{role} sees no weapons left", lambda: case.events(role, "WeaponStockReplicated", Stock="0"))
            case.check(f"{role} sees own and remote ownership with empty stock",
                       len(case.events(role, "MeleeWeaponReplicated", Equipped="1")) == 2)

        for role, before, after in (("a", "100.00", "90.00"), ("b", "90.00", "80.00")):
            input_until(role, "LeftMouseButton", f"{role} armed attack starts", lambda: server(role, "AttackStarted"))
            case.wait(f"{role} armed hit", lambda: server(role, "AttackApproved", HP=f"{before}->{after}"))
            for observer in roles:
                case.wait(f"{observer} receives target damage from {role}",
                          lambda: health(observer, "CoopRPGAttackTarget", before, after))
            case.check(f"{role} armed attack hits exactly once and both clients receive damage",
                       len(server(role, "AttackApproved")) == 1)

        input_until("a", "F", "a receives recovery item", lambda: own("a", "RecoveryCountReplicated", Count="1"))
        for role in roles:
            case.wait(f"{role} sees no recovery items left", lambda: case.events(role, "ChestStockReplicated", Stock="0"))
        input_until("a", "H", "item use at max HP rejected",
                    lambda: server("a", "RecoveryUseRejected", Reason="FullHealth", Recovery="1"))
        case.check("max HP rejection preserves the item", not server("a", "RecoveryUsed")
                   and not own("a", "RecoveryCountReplicated", Count="0"))

        case.signal("a", "Coop.Test.Vitals 80 80")
        try:
            case.wait("server applies injury fixture", lambda: case.events("server", "TestVitals", AttackerHP="80.00"))
        finally:
            case.signal("a", "")
        for role in roles:
            case.wait(f"{role} sees injured player", lambda: health(role, "CoopRPGCharacter", "100.00", "80.00"))

        input_until("a", "H", "healing used", lambda: server("a", "RecoveryUsed", HP="80.00->100.00", Recovery="1->0"))
        case.wait("owner receives consumed inventory", lambda: own("a", "RecoveryCountReplicated", Count="0"))
        for role in roles:
            case.wait(f"{role} sees restored player HP", lambda: health(role, "CoopRPGCharacter", "80.00", "100.00"))
        case.check("both clients receive the heal but only owner receives inventory or consumption",
                   len(server("a", "RecoveryUsed")) == 1 and not case.events("b", "RecoveryCountReplicated")
                   and [e["fields"]["Count"] for e in own("a", "RecoveryCountReplicated")] == ["1", "0"])

        case.signal("a", "Coop.Test.Vitals 80 60")
        try:
            for role in roles:
                case.wait(f"{role} sees new injury", lambda: health(role, "CoopRPGCharacter", "100.00", "60.00"))
        finally:
            case.signal("a", "")
        empty_before = len(server("a", "RecoveryUseRejected", Reason="NoRecovery", Recovery="0"))
        input_until("a", "H", "a tries again while injured, with no item", lambda: len(server(
            "a", "RecoveryUseRejected", Reason="NoRecovery", Recovery="0")) > empty_before)
        input_until("b", "H", "other player still has no item", lambda: server(
            "b", "RecoveryUseRejected", Reason="NoRecovery", Recovery="0"))
        case.check("empty inventory cannot heal the injured player or another player",
                   len(case.events("server", "RecoveryUsed")) == 1 and
                   not any(health(role, "CoopRPGCharacter", "60.00", "90.00") for role in roles))
    except (RuntimeError, KeyError, IndexError, OSError) as exc:
        errors.append(str(exc))
    finally:
        case.finish()

    failures = errors + [check["description"] for check in case.checks if not check["passed"]]
    result = {"status": "FAIL" if failures else "PASS", "count": len(case.checks), "failures": failures,
              "players": players, "round": round_id, "checks": case.checks}
    (path / "result.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({key: value for key, value in result.items() if key != "checks"}), flush=True)
    return bool(failures)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("stage", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    sys.exit(trial(args.stage.resolve(), args.output.resolve()))
