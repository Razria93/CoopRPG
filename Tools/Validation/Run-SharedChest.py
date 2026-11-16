"""Exercise last-item contention with a packaged Server and two real Clients."""

import argparse
from collections import Counter
from datetime import datetime
from pathlib import Path
import json
import re
import socket
import subprocess
import time


BUFFER = re.compile(
    r"^\[(?P<timestamp>\d{4}\.\d{2}\.\d{2}-\d{2}\.\d{2}\.\d{2}:\d{3})\]\[ *\d+\]"
    r"LogCoopRPG\w+: (?:Verbose: )?(?P<event>\w+) (?P<fields>.*)$"
)


def read_events(path):
    if not path.exists():
        return []
    events = []
    for line in path.read_text(encoding="utf-8-sig", errors="replace").splitlines():
        match = BUFFER.match(line)
        if match:
            events.append({
                "time": datetime.strptime(match["timestamp"], "%Y.%m.%d-%H.%M.%S:%f").isoformat(timespec="milliseconds"),
                "code": match["event"],
                "fields": dict(re.findall(r"(\w+)=(\S+)", match["fields"])),
            })
    return events


class Case:
    def __init__(self, stage, path):
        self.stage = stage
        self.path = path
        self.macros = path / "Binaries"
        self.macros.mkdir(parents=True)
        self.processes = {}
        self.expected_exits = set()
        self.checks = []

    def start(self, role, args, fps, commands=(), *, server_quit_frame=960, client_driver_end=900):
        server = role == "server"
        target = "Server" if server else "Client"
        exe = self.stage / f"Windows{target}/CoopRPG/Binaries/Win64/CoopRPG{target}.exe"
        if server:
            # Dedicated startup DEFER handles quit without a local player console.
            exec_commands = ("defer " * server_quit_frame) + "quit"
        else:
            gate = self.macros / f"{role}-signal.exec"
            gate.touch()
            macro = self.macros / f"{role}-driver.exec"
            # Keep the pending DEFER queue bounded across startup and world changes.
            scheduled = [*commands, *((frame, f"exec {gate.as_posix()}")
                                      for frame in range(40, client_driver_end, 3))]
            chunk_size = 180 if client_driver_end > 900 else 900
            last_frame = max(frame for frame, _ in scheduled)
            first_frames = range(0, last_frame + 1, chunk_size)
            chunks = [macro if first == 0 else macro.with_stem(f"{macro.stem}-{first}")
                      for first in first_frames]
            for index, first in enumerate(first_frames):
                driver = [("defer " * (frame - first)) + command for frame, command in scheduled
                          if first <= frame < first + chunk_size]
                if index + 1 < len(chunks):
                    driver.append(("defer " * chunk_size) + f"exec {chunks[index + 1].as_posix()}")
                chunks[index].write_text("\n".join(driver) + "\n", encoding="ascii")
            exec_commands = f"exec {macro.as_posix()}"
        flags = [str(exe), *args, "-unattended", "-nosound", "-NoSplash", "-forcelogflush",
                 f"-ABSLOG={(self.path / (role + '.log')).as_posix()}"]
        command = subprocess.list2cmdline(flags) + f' -ExecCmds="t.MaxFPS {fps},{exec_commands}"'
        self.processes[role] = subprocess.Popen(
            command, executable=str(exe), cwd=exe.parent, creationflags=subprocess.CREATE_NO_WINDOW,
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
        )

    def events(self, role, code, **fields):
        return [event for event in read_events(self.path / f"{role}.log")
                if event["code"] == code and all(event["fields"].get(key) == value for key, value in fields.items())]

    def wait(self, description, predicate, timeout=12):
        limit = time.monotonic() + timeout
        while time.monotonic() < limit:
            if predicate():
                return
            exited = [role for role, process in self.processes.items()
                      if process.poll() is not None and (role not in self.expected_exits or process.returncode != 0)]
            if exited:
                raise RuntimeError(f"Early process exit {exited}: {description}")
            time.sleep(0.02)
        raise RuntimeError(f"Timeout: {description}")

    def signal(self, role, commands):
        gate = self.macros / f"{role}-signal.exec"
        temporary = gate.with_suffix(".tmp")
        temporary.write_text(commands + "\n", encoding="ascii")
        for attempt in range(10):
            try:
                temporary.replace(gate)
                return
            except PermissionError:
                if attempt == 9:
                    raise
                time.sleep(0.01)

    def collect_take_calls(self, roles):
        pending = set(roles)
        deadline = time.monotonic() + 6
        while pending and time.monotonic() < deadline:
            for role in list(pending):
                if self.events(role, "ChestTakeRequested"):
                    self.signal(role, "")
                    pending.remove(role)
            time.sleep(0.015)
        if pending:
            raise RuntimeError(f"No key-bound pickup requests: {sorted(pending)}")

    def check(self, description, passed):
        self.checks.append({"description": description, "passed": bool(passed)})

    def finish(self, *, expected_process_count=3):
        forced = []
        clients = [role for role in self.processes if role != "server"]
        for role in (*clients, "server"):
            if role not in self.processes:
                continue
            process = self.processes[role]
            if process.poll() is None:
                try:
                    if role != "server":
                        self.signal(role, "quit")
                    process.wait(timeout=40 if role == "server" else 6)
                except (OSError, subprocess.TimeoutExpired):
                    forced.append(role)
                    process.kill()
                    process.wait(timeout=6)
        (self.path / "processes.json").write_text(json.dumps({
            role: {"pid": p.pid, "exit": p.returncode, "forced": role in forced}
            for role, p in self.processes.items()
        }, indent=2), encoding="utf-8")
        self.check("all started processes exit normally", len(self.processes) == expected_process_count and not forced
                   and all(p.returncode == 0 for p in self.processes.values()))


def press(key):
    return f"Input.+key {key}\ndefer Input.-key {key}"


def trial(stage, path, lead, invalid_first, port):
    case = Case(stage, path)
    winner = None
    gap = None
    failure = None
    try:
        case.start("server", [f"-port={port}", "-MULTIHOME=127.0.0.1"], 30)
        case.wait("dedicated server listening", lambda: (path / "server.log").exists()
                  and f"listening on port {port}" in (path / "server.log").read_text(encoding="utf-8", errors="replace"), 55)
        for role in ("a", "b"):
            commands = []
            if invalid_first:
                commands += [(68, "Input.+key E"), (69, "Input.-key E")]
            commands += [(110, "Input.+key R"), (111, "Input.-key R"),
                         (120, "Input.+key W"), (153, "Input.-key W")]
            case.start(role, [f"127.0.0.1:{port}", "-RenderOffscreen", "-windowed", "-ResX=960", "-ResY=540"], 30, commands)
        case.wait("both players ready", lambda: case.events("server", "LootRoundPhase", Phase="Playing"), 55)
        for role in ("a", "b"):
            case.signal(role, press("E"))
        for role in ("a", "b"):
            case.wait(role + " opened stock 1", lambda: case.events(role, "ChestViewReplicated", Open="1", Stock="1"))
            case.signal(role, "")
        opened = case.events("server", "ChestOpened")
        ids = {role: case.events(role, "ChestViewReplicated", Open="1")[0]["fields"].get("LocalPlayerId")
               for role in ("a", "b")}
        case.check("two owner views for the same chest, while stock is 1", len(opened) == 2
                   and len({e["fields"]["Chest"] for e in opened}) == 1
                   and {e["fields"]["RequesterPlayerId"] for e in opened} == set(ids.values())
                   and {e["fields"]["Stock"] for e in opened} == {"1"})

        if invalid_first:
            mover = lead
            other = "b" if mover == "a" else "a"
            case.check("opening before Playing is rejected", bool(case.events("server", "ChestRejected",
                                                                                Action="Open", Reason="NotPlaying")))
            case.signal(mover, "Input.+key S\n" + ("defer " * 33) + "Input.-key S")
            time.sleep(0.16)
            case.signal(mover, "")
            case.wait("server closes the out-of-range view", lambda: case.events(
                "server", "ChestClosed", Reason="OutOfRange", RequesterPlayerId=ids[mover]))
            case.wait("closed view reaches its owner", lambda: case.events(mover, "ChestViewReplicated", Open="0"))
            case.check("the other view stays open", not case.events(other, "ChestViewReplicated", Open="0"))
            def range_rejections():
                return case.events("server", "ChestRejected", Action="Open", Reason="OutOfRange",
                                   RequesterPlayerId=ids[mover])

            earlier_rejections = len(range_rejections())
            case.signal(mover, press("E"))
            case.wait("opening from outside the range is denied after auto-close",
                      lambda: len(range_rejections()) > earlier_rejections)
            case.signal(mover, "")
            case.signal(other, press("G"))
            case.wait("other player can take a weapon", lambda: case.events(
                "server", "ChestWeaponGranted", Stock="2->1", RequesterPlayerId=ids[other]))
            case.signal(other, "")
            case.signal(other, press("F"))
            case.collect_take_calls((other,))
            case.wait("other player can collect the last recovery item", lambda: case.events(
                "server", "ChestGranted", RequesterPlayerId=ids[other]))
            case.wait("updated weapon and recovery stocks reach the mover", lambda: (
                case.events(mover, "WeaponStockReplicated", Stock="1")
                and case.events(mover, "ChestStockReplicated", Stock="0")))
            case.signal(mover, "Input.+key W\n" + ("defer " * 33) + "Input.-key W")
            time.sleep(0.16)
            case.signal(mover, "")
            time.sleep(1.65)
            case.check("returning does not reopen the view", len(case.events(mover, "ChestViewReplicated", Open="1")) == 1)
            case.signal(mover, press("E"))
            case.wait("explicit reopen shows both updated stocks", lambda: (
                case.events("server", "ChestOpened", RequesterPlayerId=ids[mover], Stock="0", WeaponStock="1")
                and case.events(mover, "ChestViewReplicated", Open="1", Stock="0", WeaponStock="1")))
            case.signal(mover, "")
            case.signal(mover, press("F"))
            case.collect_take_calls((mover,))
        else:
            first = lead or "a"
            other = "b" if first == "a" else "a"
            case.signal(first, press("F"))
            if lead:
                time.sleep(0.10)
            case.signal(other, press("F"))
            case.collect_take_calls(("a", "b"))

        case.wait("one grant and a rejected claimant",
                  lambda: bool(case.events("server", "ChestGranted"))
                  and bool(case.events("server", "ChestRejected", Action="Take")))
        for role in ("a", "b"):
            case.wait(role + " sees stock zero", lambda: case.events(role, "ChestStockReplicated", Stock="0"))

        grants = case.events("server", "ChestGranted")
        winner = next(role for role, player in ids.items() if player == grants[0]["fields"]["RequesterPlayerId"])
        loser = "b" if winner == "a" else "a"
        reason = "Depleted"
        case.wait("winner sees personal item count 1", lambda: case.events(winner, "RecoveryCountReplicated", Count="1"))
        case.wait("loser denied", lambda: case.events("server", "ChestRejected", Action="Take", Reason=reason,
                                                    RequesterPlayerId=ids[loser]))
        denied = case.events("server", "ChestRejected", Action="Take", Reason=reason, RequesterPlayerId=ids[loser])
        case.check("exactly one grant; stock 1->0 and personal count 1",
                   len(grants) == 1 and grants[0]["fields"].get("Stock") == "1->0"
                   and grants[0]["fields"].get("Recovery") == "1")
        case.check("winner only receives inventory change", len(case.events(winner, "RecoveryCountReplicated", Count="1")) == 1
                   and not case.events(loser, "RecoveryCountReplicated"))
        case.check("both clients receive stock zero", all(len(case.events(role, "ChestStockReplicated", Stock="0")) == 1
                                                         for role in ("a", "b")))
        for role in ("a", "b"):
            calls = case.events(role, "ChestTakeRequested")
            case.check(role + " requested through actual controller input", bool(calls)
                       and calls[0]["fields"].get("LocalPlayerId") == ids[role]
                       and calls[0]["fields"].get("Round") == grants[0]["fields"].get("Round"))

        first_outcome = datetime.fromisoformat(grants[0]["time"])
        second_outcome = datetime.fromisoformat(denied[0]["time"])
        gap = round((second_outcome - first_outcome).total_seconds() * 1000)
        if invalid_first:
            case.check("only the mover was automatically closed and the other player got the item",
                       winner == other and len(case.events("server", "ChestClosed", Reason="OutOfRange")) == 1
                       and not case.events(other, "ChestViewReplicated", Open="0"))
        else:
            case.check("the two real requests compete within 250 ms", 0 <= gap <= 250)

        if invalid_first:
            # Reattempts exercise the same depletion check after the result has replicated.
            case.signal(other, press("F"))
            case.wait("later request is rejected as depleted", lambda: case.events("server", "ChestRejected",
                         Action="Take", Reason="Depleted", RequesterPlayerId=ids[other]))
            case.signal(other, "")
            case.check("repeat requests do not grant again", len(case.events("server", "ChestGranted")) == 1)
            case.signal(mover, press("Q"))
            case.wait("explicit close reaches the server and owner", lambda: (
                case.events("server", "ChestClosed", Reason="Manual", RequesterPlayerId=ids[mover])
                and len(case.events(mover, "ChestViewReplicated", Open="0")) == 2))
            case.signal(mover, "")
            case.check("the other player did not close during the test", not case.events(other, "ChestViewReplicated", Open="0"))
    except (RuntimeError, OSError, StopIteration, IndexError, KeyError) as exc:
        failure = str(exc)
    finally:
        case.finish()
        failures = [check["description"] for check in case.checks if not check["passed"]]
        if failure:
            failures.append(failure)
        result = {"status": "FAIL" if failures else "PASS", "winner": winner, "lead": lead,
                  "invalidFirst": invalid_first, "serverOutcomeGapMs": gap,
                  "failures": failures, "checks": case.checks}
        (path / "result.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
        print(path.name, result["status"], "winner", winner, "gap_ms", gap, failures, flush=True)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("run_root", type=Path)
    parser.add_argument("attempt")
    parser.add_argument("--single", action="store_true")
    options = parser.parse_args()
    stage = (options.run_root / "Stage").resolve()
    out = (options.run_root / options.attempt).resolve()
    out.mkdir()
    cases = [("", False)] if options.single else [
        ("a", False), ("b", False), ("a", False), ("b", False), ("", False), ("a", True), ("b", True),
    ]
    results = []
    for i, (lead, invalid_first) in enumerate(cases, 1):
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
            sock.bind(("127.0.0.1", 0))
            port = sock.getsockname()[1]
        results.append(trial(stage, out / f"trial-{i:02}", lead, invalid_first, port))
        if results[-1]["status"] != "PASS":
            break
    winners = Counter(item["winner"] for item in results if not item["invalidFirst"])
    passed = (len(results) == len(cases) and all(item["status"] == "PASS" for item in results)
              and (options.single or set(winners) == {"a", "b"}))
    (out / "result.json").write_text(json.dumps({"status": "PASS" if passed else "FAIL",
        "winners": dict(winners), "trials": results}, indent=2) + "\n", encoding="utf-8")
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
