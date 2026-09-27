# B31 progress and arena source judgment

The explicit Jev comparison used the original B31 goal and criterion from plan
revision 6. Model `jev-1.13.0` selected **incomplete** with probability/confidence
0.99 and criterion support 0.03. The adjacent acceptance JSON preserves the
response, including its 17,149 input tokens.

The packet includes eight complete committed files: native progress persistence,
arena progression, the shared Q3 info helper and bounded review documentation.
It expressly identifies missing application consumers, arena archive/round
integration and lobby/ranking flows. It neither closes B31 nor establishes a
whole-application audit. No engine code was built or run.

Reproduce the exact submitted packet from its adjacent evidence manifest:

```python
import hashlib, json, subprocess
from pathlib import Path
m = json.loads(Path("docs/audit/B31-progress-arena-evidence.json").read_text())
report = m["prefix"]
for row in m["files"]:
    content = subprocess.check_output(["git", "show", m["commit"] + ":" + row["path"]])
    assert hashlib.sha256(content).hexdigest() == row["sha256"]
    report += m["separator"].format(path=row["path"]) + content.decode()
assert len(report) == m["characters"]
assert hashlib.sha256(report.encode()).hexdigest() == m["sha256"]
Path("/tmp/qa-B31-progress-arena-report.txt").write_text(report)
```

This direct comparison supplements the ledger. Automatic client hook delivery
remains unverified.
