#!/usr/bin/env python3
"""Check the local dependency graph and donor scope assignments."""

import json
from pathlib import Path
import sys


def check(graph, scope):
    tasks = graph["tasks"]
    by_id = {task["id"]: task for task in tasks}
    if len(tasks) != len(by_id):
        raise ValueError("duplicate task ID")
    required = {f"B{i:02}" for i in range(35)}
    required.update({"BASELINE", "P01", "P02", "P03", "RELEASE"})
    if not required <= by_id.keys():
        raise ValueError(f"missing tasks: {sorted(required - by_id.keys())}")

    for task in tasks:
        deps = task["depends_on"]
        if len(deps) != len(set(deps)):
            raise ValueError(f"duplicate prerequisites for {task['id']}")
        missing = set(deps) - by_id.keys()
        if missing:
            raise ValueError(f"unknown prerequisites for {task['id']}: {sorted(missing)}")

    ancestors = {}
    visiting = set()

    def visit(task_id):
        if task_id in visiting:
            raise ValueError(f"dependency cycle at {task_id}")
        if task_id not in ancestors:
            visiting.add(task_id)
            parents = set(by_id[task_id]["depends_on"])
            result = set(parents)
            for parent in parents:
                result.update(visit(parent))
            visiting.remove(task_id)
            ancestors[task_id] = result
        return ancestors[task_id]

    for task_id in by_id:
        visit(task_id)
        if task_id.startswith("P") or task_id == "RELEASE":
            if "BASELINE" not in ancestors[task_id]:
                raise ValueError(f"{task_id} must follow BASELINE")
    baseline = {f"B{i:02}" for i in range(35)}
    if not baseline <= set(by_id["BASELINE"]["depends_on"]):
        raise ValueError("BASELINE must depend directly on every task B00 through B34")

    targets = scope["functional_targets"]
    target_ids = [target["id"] for target in targets]
    if len(target_ids) != len(set(target_ids)):
        raise ValueError("duplicate functional target ID")
    if set(target_ids) != {f"T{i:02}" for i in range(1, 24)}:
        raise ValueError("functional target map must cover exactly T01 through T23")
    for target in targets:
        for field in ("implementation_tasks", "qualification_tasks"):
            owners = target[field]
            if not owners or len(owners) != len(set(owners)) or set(owners) - by_id.keys():
                raise ValueError(f"invalid {field} for {target['id']}")
        if set(target["implementation_tasks"]) - baseline:
            raise ValueError(f"{target['id']} implementation must belong to baseline tasks")

    directories = scope["source_directories"]
    paths = [entry["path"] for entry in directories]
    if len(paths) != len(set(paths)):
        raise ValueError("duplicate source directory")
    for entry in directories:
        owners = entry["owner_tasks"]
        if not owners or len(owners) != len(set(owners)) or set(owners) - baseline:
            raise ValueError(f"invalid owner tasks for {entry['path']}")
    if graph["ledger_plan_revision"] != scope["ledger_plan_revision"]:
        raise ValueError("graph and source map refer to different ledger revisions")
    return len(tasks), len(targets), len(directories)


def main():
    root = Path(__file__).resolve().parents[1]
    try:
        graph = json.loads((root / "docs/dependencies.json").read_text())
        scope = json.loads((root / "docs/source-map.json").read_text())
        tasks, targets, directories = check(graph, scope)
    except (OSError, ValueError, KeyError, TypeError) as error:
        print(f"plan check failed: {error}", file=sys.stderr)
        return 1
    print(f"plan check passed: {tasks} tasks, {targets} targets, {directories} source directories")
    return 0


if __name__ == "__main__":
    sys.exit(main())
