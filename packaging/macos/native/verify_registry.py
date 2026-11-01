"""Compare production registry operations with/without move-to-front; no FPS inference."""
import json
import re
import subprocess
import sys

cases = {}
for front in ("0", "1"):
    p = subprocess.run([sys.argv[1], front], capture_output=True, text=True, timeout=30)
    if p.returncode:
        raise RuntimeError(f"Registry mode {front} failed ({p.returncode}):\n{p.stdout}\n{p.stderr}")
    assert "PASS: retained thread handles" in p.stdout
    snapshots = re.findall(
        r"Registry lookup: semaphore front=\d calls=(\d+) hits=(\d+) nodes=(\d+) search_ns=\d+", p.stdout
    )
    assert len(snapshots) == 3
    before, after = [tuple(map(int, row)) for row in snapshots[:2]]
    assert after[0] - before[0] == after[1] - before[1] == 2
    if front == "1":
        assert after[2] - before[2] == 2, "Read-only snapshot reordered the semaphore registry"
    cases[front] = {
        name: dict(zip(("calls", "hits", "nodes", "search_ns"), map(int, values)))
        for name, _mode, *values in re.findall(
            r"Registry lookup: (\w+) front=(\d) calls=(\d+) hits=(\d+) nodes=(\d+) search_ns=(\d+)", p.stdout
        )
    }
assert cases["0"].keys() == cases["1"].keys() == {"rwlock", "semaphore", "thread"}
for name in cases["0"]:
    old, new = cases["0"][name], cases["1"][name]
    assert old["calls"] == new["calls"] and old["hits"] == new["hits"], name
    assert new["nodes"] < old["nodes"] / 4, (name, old, new)
print(json.dumps(cases, indent=2))
