import pathlib,re,json,datetime,sys,hashlib
root=pathlib.Path(sys.argv[1]); failures=[]; checks=[]
verification={'script':str(pathlib.Path(__file__).resolve()),'sha256':hashlib.sha256(pathlib.Path(__file__).read_bytes()).hexdigest(),'arguments':sys.argv[1:]}
def check(name,condition,detail=None):
    checks.append({'name':name,'passed':bool(condition),'detail':detail})
    if not condition: failures.append(name)
def load(role):return (root/f'{role}.log').read_text(encoding='utf-8',errors='replace').splitlines()
lines={r:load(r) for r in ['server','a','b']}
def events(role,pattern):
    result=[]
    for n,line in enumerate(lines[role]):
        m=re.search(pattern,line)
        if m:
            d=m.groupdict();d.update(line=line,index=n)
            ts=re.search(r'^\[(\d{4}\.\d{2}\.\d{2}-\d{2}\.\d{2}\.\d{2}:\d{3})\]',line)
            if ts:d['time']=datetime.datetime.strptime(ts[1],'%Y.%m.%d-%H.%M.%S:%f').timestamp()
            result.append(d)
    return result
meta=json.loads((root/'processes.json').read_text(encoding='utf-8-sig'))
check('three independent processes',len({p['pid'] for p in meta['processes'].values()})==3)
check('normal exits',all(p['exit']==0 and not p['forced'] for p in meta['processes'].values()),meta['processes'])
check('two joins',len(events('server','Join succeeded'))==2)
for role in lines:
    check(role+' no fatal/ensure',not any(re.search(r'Fatal error:|Assertion failed:|Ensure condition failed:|LogWindows: Error:',s) for s in lines[role]))
snapshot=r'CombatSnapshot Label=(?P<label>\S+) Actor=(?P<actor>\S+) HP=(?P<hp>[\d.]+) Stamina=(?P<stamina>[\d.]+) Cooldown=(?P<cooldown>\d+) Remaining=(?P<remaining>[-\d.]+) Time=(?P<worldtime>[\d.]+)'
snaps={r:events(r,snapshot) for r in lines}
if not meta['serverProbeEnabled']:
    check('server refuses two test preparations',len(events('server','TestPrepareRejected Actor='))==2)
    check('no approval in disabled test',not events('server','AttackApproved Attacker='))
    for role in ['a','b']:
        found=[s for s in snaps[role] if s['label']==role+'-Disabled']
        check(role+' HP and stamina unchanged',len(found)==1 and float(found[0]['hp'])==100 and float(found[0]['stamina'])==100,found)
    result={'status':'PASS' if not failures else 'FAIL','verification':verification,'checks':len(checks),'failures':failures,'results':checks}
    (root/'disabled-result.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
    print(json.dumps({k:v for k,v in result.items() if k!='results'},ensure_ascii=False))
    sys.exit(bool(failures))
approvals=events('server',r'AttackApproved Attacker=(?P<actor>\S+) Target=(?P<target>\S+) Distance=(?P<distance>[\d.]+) Range=250.00 HP=(?P<before>[\d.]+)->(?P<after>[\d.]+) Stamina=(?P<costbefore>[\d.]+)->(?P<costafter>[\d.]+)')
rejections=events('server',r'AttackRejected Attacker=(?P<actor>\S+) Reason=(?P<reason>\S+)')
rolemap={};delays=[]
expected={'CostBefore':(100,9,0),'CostAfter':(100,9,0),'Boundary':(90,0,0),'CooldownDuring':(90,90,1),'CooldownExpired':(90,90,0),'Reuse':(80,80,1),'NormalThree':(70,70,0),'Independent':(60,80,0)}
for role in ['a','b']:
    ready=[s for s in snaps['server'] if s['label']==role+'-Ready']
    local=[s for s in snaps[role] if s['label']==role+'-Ready']
    check(role+' identity snapshots',len(ready)==len(local)==1)
    if not ready or not local: continue
    actor=ready[0]['actor'];owner=local[0]['actor'];rolemap[role]={'server':actor,'local':owner}
    for name,values in expected.items():
        for source in ['server',role]:
            match=[s for s in snaps[source] if s['label']==f'{role}-{name}']
            ok=len(match)==1 and (float(match[0]['hp']),float(match[0]['stamina']),int(match[0]['cooldown']))==values
            check(f'{source} {role}-{name}',ok,match)
    for reason,wanted in [('Cost',5),('Cooldown',7)]:
        found=[e for e in rejections if e['actor']==actor and e['reason']==reason]
        check(f'{role} server rejects {reason}',len(found)==wanted,len(found))
    cost_before=[s for s in snaps['server'] if s['label']==f'{role}-CostBefore']
    cost_after=[s for s in snaps['server'] if s['label']==f'{role}-CostAfter']
    if cost_before and cost_after:
        check(role+' no cost-stage approval',not any(cost_before[0]['index']<e['index']<cost_after[0]['index'] for e in approvals))
    own=[a for a in approvals if a['actor']==actor]
    check(role+' eight approvals',len(own)==8,len(own))
    check(role+' costs only on approval',all(float(a['costbefore'])-float(a['costafter'])==10 and float(a['before'])-float(a['after'])==10 and float(a['distance'])<=250 for a in own))
    check(role+' ten input presses',len(events(role,'AttackInput Actor='))==10)
    dispatch=events(role,r'AttackDispatch Actor=\S+ Submitted=(?P<submitted>[01])')
    check(role+' eight dispatches and two local blocks',sum(d['submitted']=='1' for d in dispatch)==8 and sum(d['submitted']=='0' for d in dispatch)==2)
    check(role+' local cost block',len(events(role,r'AttackBlocked Attacker=\S+ Reason=Cost '))==1)
    check(role+' local cooldown block',len(events(role,r'AttackBlocked Attacker=\S+ Reason=Cooldown '))==1)
    check(role+' twelve explicit server probes',len(events(role,'TestActivationRequest Actor='))==12)
    check(role+' twenty outgoing GAS requests',len(events(role,r'::CallServerTryActivateAbility '))==20)
    stamina=events(role,r'StaminaReplicated Actor=(?P<actor>\S+) Before=(?P<before>[\d.]+) After=(?P<after>[\d.]+)')
    check(role+' stamina replication owner only',all(e['actor']==owner for e in stamina if e['before']!=e['after']))
    for a in own:
        match=[e for e in stamina if e['before']==a['costbefore'] and e['after']==a['costafter'] and 0<=e['time']-a['time']<=3]
        check(f'{role} cost replicated at {a["index"]}',bool(match))
    health=events(role,r'HealthReplicated Actor=(?P<actor>\S+) Before=(?P<before>[\d.]+) After=(?P<after>[\d.]+)')
    for a in approvals:
        match=[e for e in health if e['before']==a['before'] and e['after']==a['after'] and 0<=e['time']-a['time']<=3]
        check(f'{role} HP replicated at {a["index"]}',bool(match))
        if match:delays.append(min(e['time']-a['time'] for e in match))
changes=events('server',r'CooldownChanged Actor=(?P<actor>\S+) Count=(?P<count>\d+) Time=(?P<worldtime>[\d.]+)')
starts={};durations=[];independent=[]
for e in changes:
    if e['count']=='1':starts[e['actor']]=e
    elif e['actor'] in starts:
        begin=starts.pop(e['actor']);duration=float(e['worldtime'])-float(begin['worldtime']);durations.append(duration)
        for a in approvals:
            if a['actor']!=e['actor'] and begin['time']<a['time']<e['time']:independent.append({'cooldownOwner':e['actor'],'approvedOther':a['actor'],'approvalLine':a['index']})
check('sixteen cooldown lifetimes',len(durations)==16,len(durations))
check('no early expiry or rejection refresh',all(1.99<=d<=2.25 for d in durations),durations)
check('other player can approve during cooldown',len(independent)>=2,independent)
result={'status':'PASS' if not failures else 'FAIL','verification':verification,'checks':len(checks),'failures':failures,'roleMap':rolemap,'approvals':len(approvals),'rejections':len(rejections),'cooldownSeconds':durations,'maxHealthReplicationDelayMs':round(max(delays,default=0)*1000,2),'results':checks}
(root/'boundary-result.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps({k:v for k,v in result.items() if k!='results'},ensure_ascii=False,indent=2))
sys.exit(bool(failures))
