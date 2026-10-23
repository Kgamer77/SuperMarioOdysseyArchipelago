import json, collections, sys

path = sys.argv[1]
start = sys.argv[2]
target = sys.argv[3]

d = json.load(open(path, encoding="utf-8"))
nodes = {}
edges = []
def walk(o):
    if isinstance(o, dict):
        if o.get("node_id") and "node_type" in o:
            nodes[o["node_id"]] = o
        if "from_node_id" in o and "to_node_id" in o and ("edge_id" in o or "edge_type" in o):
            edges.append(o)
        for v in o.values():
            walk(v)
    elif isinstance(o, list):
        for v in o:
            walk(v)
walk(d)
print("nodes:", len(nodes), "edges:", len(edges))

print(f"\n=== edges touching {target} ===")
for e in edges:
    f, t = e["from_node_id"], e["to_node_id"]
    if f == target or t == target:
        arrow = "->"
        print(f"  {f} {arrow} {t} | req={e.get('requirement_expr')!r} bidir={e.get('bidirectional')} type={e.get('edge_type')} src={e.get('source')}")

adj = collections.defaultdict(list)
for e in edges:
    f, t = e["from_node_id"], e["to_node_id"]
    req = (e.get("requirement_expr") or "").strip()
    bidir = str(e.get("bidirectional", "false")).lower() == "true"
    adj[f].append((t, req, e.get("edge_id")))
    if bidir:
        adj[t].append((f, req, e.get("edge_id")))

def bfs(free_only=True):
    seen = {start: None}
    q = collections.deque([start])
    while q:
        cur = q.popleft()
        for (nxt, req, eid) in adj[cur]:
            if nxt in seen: continue
            if free_only and req.lower() not in ("free", ""): continue
            seen[nxt] = (cur, req, eid)
            q.append(nxt)
    return seen

seen = bfs(True)
print(f"\n=== {target} FREE-reachable from {start}? ===", target in seen)
if target in seen:
    path_=[]; cur=target
    while cur is not None:
        info=seen[cur]
        if info is None: path_.append((cur,None,None)); break
        path_.append((cur,info[1],info[2])); cur=info[0]
    for n,req,eid in reversed(path_):
        print(f"  {n}   [{eid}: {req}]")

print("\n=== node detail for target ===")
n = nodes.get(target, {})
for k in ("node_id","node_type","source","display_name","scenario_indices","entrance_id","side"):
    print(f"  {k}: {n.get(k)}")
