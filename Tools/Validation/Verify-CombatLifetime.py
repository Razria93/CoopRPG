"""Verify V02 rejection boundaries and V03 lifetime against packaged-process logs."""
import hashlib
import json
from pathlib import Path
import re
import sys

run = Path(sys.argv[1])
require_telemetry = '--require-telemetry' in sys.argv[2:]
raw_logs = {source: (run / f'{source}.log').read_bytes() for source in ('server', 'a', 'b')}
checks = []

def check(name, condition, detail=None):
    checks.append({'name': name, 'passed': bool(condition), 'detail': detail})

def read_events(raw):
    events = []
    for i, line in enumerate(raw.decode('utf-8-sig', errors='replace').splitlines(), 1):
        outcome = re.search(r'LogCoopRPG\w+: (?:Verbose: )?(\w+) ', line)
        if outcome:
            fields = dict(re.findall(r'(\w+)=(\S+)', line))
            events.append(dict(code=outcome[1], fields=fields, line=i, raw=line))
    return events

events = {src: read_events(raw) for src, raw in raw_logs.items()}
server = events['server']
meta = json.loads((run / 'processes.json').read_text(encoding='utf-8-sig'))
check('three separate packaged processes', len({p['pid'] for p in meta['processes'].values()}) == 3)
check('all processes exited normally', all(x['exit'] == 0 and not x['forced'] for x in meta['processes'].values()))
for source, raw in raw_logs.items():
    check(source + ' no fatal/ensure or unrecognized console command',
         not re.search(rb'Fatal error:|Assertion failed:|Ensure condition failed:|LogWindows: Error:|Command not recognized', raw))

def filter_events(dataset, code, predicate=lambda _: True):
    return [e for e in dataset if e['code'] == code and predicate(e['fields'])]

def snapshot(source, label):
    found = filter_events(events[source], 'CombatSnapshot', lambda values: values.get('Label') == label)
    check(f'{source} {label} snapshot exists exactly once', len(found) == 1, [e['line'] for e in found])
    return found[0] if len(found) == 1 else {'line': -1, 'fields': {}}

def get_values(event):
    fields = event['fields']
    return tuple(fields.get(k) for k in ('HP', 'Stamina', 'Cooldown'))

def expect_values(event, hp, stamina, cooldown):
    return get_values(event) == (f'{hp:.2f}', f'{stamina:.2f}', str(cooldown))

class Requester:
    def __init__(self, source):
        self.source = source
        ids = {e['fields'].get('LocalPlayerId') for e in filter_events(events[source], 'AttackInput')}
        check(source + ' has one measured PlayerId', len(ids) == 1 and None not in ids, sorted(str(x) for x in ids))
        self.id = next(iter(ids)) if len(ids) == 1 else None
        self.actor = snapshot('server', source + '-FarBefore')['fields'].get('Actor')

    def block(self, name):
        labels = [f'{self.source}-{name}{suffix}' for suffix in ('Before', 'After')]
        before, after = [snapshot('server', label) for label in labels]
        local_before, local_after = [snapshot(self.source, label) for label in labels]
        check(f'{self.source} {name} snapshots in order', 0 < before['line'] < after['line'])
        window = [e for e in server if before['line'] < e['line'] < after['line']]
        actor_events = [e for e in window if any(e['fields'].get(field) == self.actor for field in ('Actor', 'Attacker'))]
        return before, after, local_before, local_after, actor_events

    def cancel_rpc(self, name, lines, outcome):
        before = snapshot(self.source, f'{self.source}-{name}Before')
        after = snapshot(self.source, f'{self.source}-{name}After')
        calls = filter_events(events[self.source], 'CancelRPCCalled',
                              lambda f: f.get('LocalPlayerId') == self.id and f.get('NetMode') == 'Client')
        calls = [e for e in calls if before['line'] < e['line'] < after['line']]
        receipts = filter_events(lines, 'CancelRPCReceived')
        check(f'{self.source} {name} matching cancel call and server receipt',
              bool(calls) and len(calls) == len(receipts)
              and all(e['fields'].get('RequesterPlayerId') == self.id
                      and e['fields'].get('NetMode') == 'DedicatedServer' for e in receipts))
        check(f'{self.source} {name} server receipt precedes cancel result',
              bool(receipts) and bool(outcome) and receipts[0]['line'] < outcome[0]['line']
              and outcome[0]['fields'].get('RequesterPlayerId') == self.id)
        return receipts

    def outcome(self, name, lines, kind, amount, reason=None):
        found = filter_events(lines, kind, lambda f: reason is None or f.get('Reason') == reason)
        check(f'{self.source} {name} {kind} x{amount}', len(found) == amount, [e['line'] for e in found])
        if found and kind.startswith('Attack'):
            check(f'{self.source} {name} server requester matches', bool(self.id)
                  and all(e['fields'].get('RequesterPlayerId') == self.id for e in found))
        return found

requesters = [Requester(source) for source in ('a','b')]
check('distinct player IDs and distinct server Pawns', len({r.id for r in requesters}) == 2 and len({r.actor for r in requesters}) == 2)
for requester in requesters:
    for name, reason, hp in (
        ('Far','OutOfRange',100), ('DeadTarget','InvalidTarget',0), ('DeadAttacker','AttackerDead',100)
    ):
        before, after, local_before, local_after, lines = requester.block(name)
        rejects = requester.outcome(name, lines, 'AttackRejected', 5, reason)
        check(f'{requester.source} {name} no damage, cost, or cooldown',
              all(expect_values(e,hp,100,0) for e in (before,after,local_before,local_after))
              and not any(e['code'] in ('AttackApproved','AttackStarted','CooldownChanged') for e in lines))
        if name == 'Far':
            check(f'{requester.source} distance measured above advertised range', len(rejects)==5 and
                  all(float(e['fields']['Distance']) > float(e['fields']['Range']) for e in rejects))
        else:
            setup = [e for e in filter_events(server, 'TestVitals', lambda f: f.get('Actor') == requester.actor)
                     if e['line'] < before['line']]
            expected_health = '0.00' if name == 'DeadAttacker' else '100.00'
            check(f'{requester.source} {name} authority logged the expected attacker and target health',
                  bool(setup) and setup[-1]['fields'].get('AttackerHP') == expected_health
                  and setup[-1]['fields'].get('HP') == f'{hp:.2f}')
    for i in range(1,6):
        name = f'Cancel{i}-'
        before, after, local_before, local_after, lines = requester.block(name)
        started = requester.outcome(name, lines, 'AttackStarted', 1)
        cancelled = requester.outcome(name, lines, 'AttackCancelled', 1)
        if require_telemetry:
            receipts = requester.cancel_rpc(name, lines, cancelled)
            check(f'{requester.source} cancel {i} cost is logged before receipt',
                  bool(started) and bool(receipts) and started[0]['line'] < receipts[0]['line']
                  and started[0]['fields'].get('Stamina') == f'{110-i*10:.2f}->{100-i*10:.2f}')
        check(f'{requester.source} cancel {i} no hit; committed cost and cooldown stay active',
              all(expect_values(e,100,110-i*10,0) for e in (before,local_before))
              and all(expect_values(e,100,100-i*10,1) for e in (after,local_after))
              and all(e['fields'].get('Damage')=='0' and e['fields'].get('Committed')=='1' for e in cancelled)
              and not any(e['code'] in ('AttackApproved','AttackMissed') for e in lines))
    before, after, local_before, local_after, lines = requester.block('Recovery')
    hits = requester.outcome('Recovery', lines, 'AttackApproved', 1)
    ignored = filter_events(lines, 'CancelIgnored', lambda f: f.get('Reason') == 'NoActiveAttack')
    if require_telemetry:
        receipts = requester.cancel_rpc('Recovery', lines, ignored)
        started = filter_events(lines, 'AttackStarted')
        check(f'{requester.source} recovery separates cost and damage before late cancel',
              bool(started) and bool(hits) and bool(receipts)
              and started[0]['line'] < hits[0]['line'] < receipts[0]['line']
              and started[0]['fields'].get('Stamina') == '50.00->40.00'
              and hits[0]['fields'].get('HP') == '100.00->90.00')
    check(f'{requester.source} recovery after cooldown and late cancel leaves hit intact',
          bool(ignored) and all(expect_values(e,100,50,0) for e in (before,local_before))
          and all(expect_values(e,90,40,1) for e in (after,local_after)))
    for i in range(1,6):
        name = f'Duplicate{i}-'
        before, after, local_before, local_after, lines = requester.block(name)
        duplicated = requester.outcome(name, lines, 'TestDuplicateConfirmation', 1)
        requester.outcome(name, lines, 'AttackApproved', 1)
        check(f'{requester.source} duplicate {i} one damage and one charge',
              all(e['fields'].get('Attempts')=='2' for e in duplicated)
              and len(filter_events(lines,'CooldownChanged',lambda f:f.get('Count')=='1')) == 1
              and all(expect_values(e,100,100,0) for e in (before,local_before))
              and all(expect_values(e,90,90,1) for e in (after,local_after)))
    before, after, local_before, local_after, lines = requester.block('Miss')
    misses = requester.outcome('Miss', lines, 'AttackMissed', 1, 'InvalidTarget')
    check(f'{requester.source} target dies during windup; committed cost and cooldown retained',
          all(expect_values(e,100,100,0) for e in (before,local_before))
          and all(expect_values(e,0,90,1) for e in (after,local_after))
          and all(e['fields'].get('Damage')=='0' and e['fields'].get('Committed')=='1' for e in misses)
          and not any(e['code']=='AttackApproved' for e in lines))

for src in ('a','b'):
    target = filter_events(events[src], 'HealthReplicated', lambda f: f.get('Actor','').startswith('CoopRPGAttackTarget_'))
    check(src + ' replicated both players’ twelve successful hits',
          sum(e['fields'].get('Before')=='100.00' and e['fields'].get('After')=='90.00' for e in target) == 12)
for code, count in [('AttackStarted',24),('AttackApproved',12),('AttackCancelled',10),('AttackMissed',2),('AttackRejected',30)]:
    check(f'exact total {code}', len(filter_events(server,code))==count)

if require_telemetry:
    commits = filter_events(server, 'AttackStarted')
    hits = filter_events(server, 'AttackApproved')
    check('all commits isolate stamina; all hit events isolate target HP',
          all('Stamina' in e['fields'] and 'HP' not in e['fields'] for e in commits)
          and all('HP' in e['fields'] and 'Stamina' not in e['fields'] for e in hits))

failed = [c for c in checks if not c['passed']]
result = dict(status='FAIL' if failed else 'PASS', count=len(checks), failures=failed,
              playerIds={r.source:r.id for r in requesters}, checks=checks,
              sourceSHA256={k:hashlib.sha256(v).hexdigest() for k,v in raw_logs.items()})
(run/'lifetime-result.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n', encoding='utf-8')
print(json.dumps({k:v for k,v in result.items() if k!='checks'},ensure_ascii=False,indent=2))
sys.exit(bool(failed))
