import json, collections

d = json.load(open("logic/graph-editor/saves/kingdoms/cap_kingdom.json", encoding="utf-8"))

# Find the edges list and nodes list
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

# Build adjacency. Treat requirement 'free' as traversable; everything else as "needs X".
adj = collections.defaultdict(list)
for e in edges:
    f, t = e["from_node_id"], e["to_node_id"]
    req = (e.get("requirement_expr") or "").strip()
    bidir = str(e.get("bidirectional", "false")).lower() == "true"
    adj[f].append((t, req, e.get("edge_id")))
    if bidir:
        adj[t].append((f, req, e.get("edge_id")))

# BFS from the home start node, traversing ONLY free edges, to see if obj381 is
# reachable with no abilities at all. Print the path.
start = "CapWorldHomeStage::start"
target = "CapWorldHomeStage::checkpoint::obj381"

def bfs(free_only=True):
    seen = {start: None}
    q = collections.deque([start])
    while q:
        cur = q.popleft()
        for (nxt, req, eid) in adj[cur]:
            if nxt in seen:
                continue
            if free_only and req.lower() not in ("free", ""):
                continue
            seen[nxt] = (cur, req, eid)
            q.append(nxt)
    return seen

seen = bfs(free_only=True)
print("\n=== obj381 reachable via FREE-only edges from", start, "? ===", target in seen)
if target in seen:
    # reconstruct
    path = []
    cur = target
    while cur is not None:
        info = seen[cur]
        if info is None:
            path.append((cur, None, None)); break
        path.append((cur, info[1], info[2]))
        cur = info[0]
    for n, req, eid in reversed(path):
        print(f"  {n}   [{eid}: {req}]")

# Also list what is reachable free, restricted to CapWorldHomeStage overworld nodes
print("\n=== FREE-reachable CapWorldHomeStage nodes ===")
for n in sorted(seen):
    if n.startswith("CapWorldHomeStage::"):
        print("  ", n)
