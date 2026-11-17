"""Exercise admission, reserved seats during map travel, and connection notices in packages."""

import argparse
from contextlib import ExitStack
import importlib.util
import json
from pathlib import Path
import socket
import subprocess
import sys
from threading import Event, Thread
import time


sys.dont_write_bytecode = True
spec = importlib.util.spec_from_file_location('shared_chest', Path(__file__).with_name('Run-SharedChest.py'))
shared = importlib.util.module_from_spec(spec)
spec.loader.exec_module(shared)

PEERS = ('a', 'b')
MAP = '/Game/Maps/L_Movement'
NEXT_MAP = '/Game/Maps/L_Movement_Round2'
CLIENT_FLAGS = ('-RenderOffscreen', '-windowed', '-ResX=960', '-ResY=540')


class Relay:
    """Allow the test to hold returning peers without teaching production code about the test."""

    def __init__(self, port):
        self.target = ('127.0.0.1', port)
        self.socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.socket.bind(('127.0.0.1', 0))
        self.socket.settimeout(.1)
        self.port = self.socket.getsockname()[1]
        self.done = Event()
        self.paused = Event()
        self.thread = Thread(target=self.forward)

    def __enter__(self):
        self.thread.start()
        return self

    def __exit__(self, exc_type, exc, tb):
        self.done.set()
        self.thread.join()
        self.socket.close()

    def forward(self):
        client = None
        while not self.done.is_set():
            try:
                packet, source = self.socket.recvfrom(65535)
            except TimeoutError:
                continue
            except ConnectionResetError:
                # On Windows a datagram sent while the server switches sockets can report ICMP port unreachable.
                continue
            if source != self.target:
                client = source
            destination = client if source == self.target else self.target
            if destination and not self.paused.is_set():
                self.socket.sendto(packet, destination)


def trial(stage, output, scenario):
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as reserved:
        reserved.bind(('127.0.0.1', 0))
        port = reserved.getsockname()[1]
    case = shared.Case(stage, output)
    expected_count = {'admission': 5, 'travel': 4, 'shutdown': 3}[scenario]
    failure = None

    def log_contains(role, fragment):
        log = output / f'{role}.log'
        return log.is_file() and fragment in log.read_text(encoding='utf-8', errors='replace')

    def server(code, **fields):
        return case.events('server', code, **fields)

    def command(role, text, message, predicate, timeout=12):
        case.signal(role, text)
        try:
            case.wait(message, predicate, timeout)
        finally:
            case.signal(role, '')

    def capture(role, name):
        shot = output / f'{name}.png'
        command(role, f'shot showui -nosuffix filename={shot.as_posix()}', f'{role} saved {name}',
                lambda: shot.is_file() and shot.stat().st_size > 1000)

    def close(role):
        case.expected_exits.add(role)
        case.signal(role, 'quit')
        case.processes[role].wait(timeout=12)
        case.check(f'{role} exits normally', case.processes[role].returncode == 0)

    def ready(role, round_id):
        previous = len(server('LootPlayerReady', Round=round_id))
        command(role, shared.press('R'), f'{role} readies',
                lambda: len(server('LootPlayerReady', Round=round_id)) > previous)

    def reject(role, reason):
        case.wait(f'{role} draws notice {reason}', lambda: case.events(role, 'ConnectionNoticeDisplayed', Reason=reason), 25)
        case.check(f'{role} sees a server-side pre-login rejection for {reason} without a welcome',
                   len(server('RoundJoinRejected', Reason=reason, Stage='PreLogin')) == 1
                   and len(case.events(role, 'ConnectionNoticeDisplayed', Reason=reason)) == 1
                   and not log_contains(role, 'Welcomed by server'))
        capture(role, reason)

    with ExitStack() as stack:
        try:
            relays = {role: stack.enter_context(Relay(port)) for role in PEERS} if scenario == 'travel' else {}
            server_frames = {'admission': 1500, 'travel': 1500, 'shutdown': 700}[scenario]
            case.start('server', [MAP + '?EnemyCombat', f'-port={port}', '-MULTIHOME=127.0.0.1', '-CoopCombatTest'],
                       30, server_quit_frame=server_frames)
            case.wait('server listens', lambda: log_contains('server', f'listening on port {port}'), 55)
            for role in PEERS:
                endpoint = relays[role].port if role in relays else port
                case.start(role, [f'127.0.0.1:{endpoint}', *CLIENT_FLAGS, '-CoopCombatTest'],
                           30, client_driver_end=1450)
            case.wait('both peers enter the first map', lambda: all(log_contains(role, f'Welcomed by server (Level: {MAP},') for role in PEERS)
                      and len(server('RoundPlayerAdmitted', Map='L_Movement')) == 2, 55)
            old_id = server('LootRoundCreated', Map='L_Movement')[0]['fields']['Round']
            case.check('normal entry admits exactly two players',
                       [item['fields']['Players'] for item in server('RoundPlayerAdmitted')] == ['1', '2'])

            if scenario == 'admission':
                case.start('c-full', [f'127.0.0.1:{port}', *CLIENT_FLAGS], 30, client_driver_end=1450)
                reject('c-full', 'RoundFull')
                close('c-full')

            for role in PEERS:
                ready(role, old_id)
            case.wait('both players start the round', lambda: server('LootRoundPhase', Round=old_id, Phase='Playing'))

            if scenario == 'admission':
                case.start('c-playing', [f'127.0.0.1:{port}', *CLIENT_FLAGS], 30, client_driver_end=1450)
                reject('c-playing', 'RoundInProgress')
                close('c-playing')
                case.check('both denied attempts create no third player and leave the current round intact',
                           len(server('RoundPlayerAdmitted')) == 2 and not server('RoundResult')
                           and not any(case.events(role, 'ConnectionNoticeDisplayed') for role in PEERS))
            elif scenario == 'shutdown':
                case.expected_exits.add('server')
                case.processes['server'].wait(timeout=55)
                case.check('a dedicated server shutdown is graceful', case.processes['server'].returncode == 0)
                case.wait('both original clients render connection-ended notices', lambda: all(
                    case.events(role, 'ConnectionNoticeDisplayed', Reason='ConnectionEnded') for role in PEERS), 55)
                for role in PEERS:
                    capture(role, f'{role}-shutdown')
                case.check('each client renders one notice and remains open after server shutdown', all(
                    len(case.events(role, 'ConnectionNoticeDisplayed')) == 1 and case.processes[role].poll() is None
                    for role in PEERS))
            else:
                case.start('c', [MAP, *CLIENT_FLAGS], 30, client_driver_end=1450)
                case.wait('C finishes standby startup', lambda: log_contains('c', f'Load map complete {MAP}'), 45)
                command('a', 'Coop.Test.RoundEnd timeout', 'server sets up a near deadline',
                        lambda: server('TestRoundEndPrepared', Round=old_id, Scenario='timeout'))
                case.wait('both original peers start actual network travel', lambda: all(
                    log_contains(role, f'{NEXT_MAP}?') for role in PEERS))
                for relay in relays.values():
                    relay.paused.set()
                case.wait('new server world loaded before C tries', lambda: server('LootRoundCreated', Map='L_Movement_Round2'))
                new_id = server('LootRoundCreated', Map='L_Movement_Round2')[0]['fields']['Round']
                command('c', f'open 127.0.0.1:{port}', 'C starts a real connection while seats are reserved',
                        lambda: log_contains('c', f'LogNet: Browse: 127.0.0.1:{port}'))
                reject('c', 'RoundTravelReserved')
                case.check('C is denied before either original peer takes a reserved place',
                           not server('RoundTravelSeatJoined') and old_id != new_id
                           and len(server('RoundTravelSeatsReserved', Map='L_Movement_Round2', Count='2')) == 1)
                for relay in relays.values():
                    relay.paused.clear()
                case.wait('both peers reconnect to the destination', lambda: all(log_contains(role, f'Welcomed by server (Level: {NEXT_MAP},')
                          for role in PEERS) and len(server('RoundTravelSeatJoined', Map='L_Movement_Round2')) == 2, 25)
                case.check('only the original peers consume the two seats',
                           [row['fields']['Remaining'] for row in server('RoundTravelSeatJoined')] == ['1', '0']
                           and len(server('RoundPlayerAdmitted', Map='L_Movement_Round2')) == 2)
                ready('a', new_id)
                case.check('one ready peer keeps the new clock stopped', not server('RoundClockStarted', Round=new_id))
                ready('b', new_id)
                case.wait('the destination starts its own round', lambda: server('RoundClockStarted', Round=new_id, Duration='300'))
                case.check('successful map travel shows no connection-ended notice to either original peer', all(
                    not case.events(role, 'ConnectionNoticeQueued') for role in PEERS))
        except (IndexError, KeyError, OSError, RuntimeError, ValueError, subprocess.TimeoutExpired) as exc:
            failure = str(exc)
        finally:
            for relay in relays.values():
                relay.paused.clear()
            case.finish(expected_process_count=expected_count)

    result = {'scenario': scenario, 'checks': case.checks, 'error': failure,
              'passed': not failure and all(item['passed'] for item in case.checks)}
    (output / 'result.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print(scenario, 'PASS' if result['passed'] else 'FAIL', len(case.checks), failure,
          [item['description'] for item in case.checks if not item['passed']], flush=True)
    return result


def initial_timeout_trial(stage, output):
    case = shared.Case(stage, output)
    role = 'c-timeout'
    failure = None
    # A bound UDP socket silently drops packets and cannot accidentally admit the client.
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sink:
        sink.bind(('127.0.0.1', 0))
        port = sink.getsockname()[1]
        try:
            case.start(role, [f'127.0.0.1:{port}', *CLIENT_FLAGS,
                       '-ini:Engine:[/Script/OnlineSubsystemUtils.IpNetDriver]:InitialConnectTimeout=3.0'],
                       30, client_driver_end=1450)
            case.wait('client renders the notice after an initial timeout',
                      lambda: case.events(role, 'ConnectionNoticeDisplayed'), 45)
            queued = case.events(role, 'ConnectionNoticeQueued')
            displayed = case.events(role, 'ConnectionNoticeDisplayed')
            log = (output / f'{role}.log').read_text(encoding='utf-8', errors='replace')
            case.check('a pending connection times out without receiving a welcome',
                       len(queued) == 1 and queued[0]['fields'].get('Failure') == 'ConnectionTimeout'
                       and 'Welcomed by server' not in log)
            case.check('the initial timeout queues and displays ConnectionFailed',
                       [event['fields'].get('Reason') for event in queued] == ['ConnectionFailed']
                       and [event['fields'].get('Reason') for event in displayed] == ['ConnectionFailed'])
        except (OSError, RuntimeError, subprocess.TimeoutExpired) as exc:
            failure = str(exc)
        finally:
            case.finish(expected_process_count=1)

    result = {'scenario': 'initial-timeout', 'checks': case.checks, 'error': failure,
              'passed': not failure and all(item['passed'] for item in case.checks)}
    (output / 'result.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print(result['scenario'], 'PASS' if result['passed'] else 'FAIL', len(case.checks), failure,
          [item['description'] for item in case.checks if not item['passed']], flush=True)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--stage', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    scenarios = ('admission', 'travel', 'shutdown', 'initial-timeout')
    parser.add_argument('--scenario', choices=scenarios, action='append')
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=False)
    stage, output = args.stage.resolve(), args.output.resolve()
    results = [initial_timeout_trial(stage, output / scenario) if scenario == "initial-timeout"
               else trial(stage, output / scenario, scenario) for scenario in (args.scenario or scenarios)]
    (args.output / 'summary.json').write_text(json.dumps(results, indent=2) + '\n', encoding='utf-8')
    return 0 if all(result['passed'] for result in results) else 1


if __name__ == '__main__':
    sys.exit(main())
