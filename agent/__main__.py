"""Run from the repository root: python -m agent --help."""
import argparse
import json
from pathlib import Path
import sys

from .config import Config
from .evidence import EvidenceStore
from .evaluation import evaluate, validate_tasks
from .graph_store import GraphStore
from .repository import Repository
from .runtime import InspectionAgent, OllamaClient
from .tools import InspectionTools
from .tracing import save_report


def parser() -> argparse.ArgumentParser:
    cli = argparse.ArgumentParser(description="Read-only codebase inspection agent")
    cli.add_argument("--repo", type=Path, required=True, help="Local checkout root")
    cli.add_argument("--graph", type=Path, help="Odysseus graph.json export")
    cli.add_argument("--revision", help="Exact commit recorded in graph (required if ambiguous)")
    cli.add_argument("--source-uri", help="Exact repository URI recorded in graph")
    cli.add_argument("--model", default=Config().model)
    cli.add_argument("--max-tool-rounds", type=int, default=Config().max_tool_rounds)
    cli.add_argument("--source-only", action="store_true", help="Disable all graph tools for a baseline")
    mode = cli.add_mutually_exclusive_group()
    mode.add_argument("--question", help="Ask once; otherwise enter interactive mode")
    mode.add_argument("--benchmark", type=Path, help="Run fixed inspection tasks")
    cli.add_argument("--compare", action="store_true", help="Run benchmark in source-only and graph modes")
    cli.add_argument("--trace", type=Path, help="Save full JSON trace to a new file; may contain source code")
    cli.add_argument("--inspect-only", action="store_true", help="Validate inputs and print status without an LLM")
    return cli


def main(argv: list[str] | None = None) -> int:
    cli = parser()
    args = cli.parse_args(argv)
    if not args.source_only and not args.graph:
        cli.error("--graph is required unless --source-only is selected")
    if args.compare and (not args.benchmark or not args.graph or args.source_only):
        cli.error("--compare requires --benchmark and --graph, without --source-only")
    if args.trace and args.trace.exists():
        cli.error("Trace output already exists; choose a new path")
    try:
        config = Config(model=args.model, max_tool_rounds=args.max_tool_rounds)
        repository = Repository(args.repo, config)
        graph = None if args.source_only else GraphStore(args.graph, args.revision, args.source_uri)
        if args.inspect_only:
            print(json.dumps(InspectionTools(repository, graph, EvidenceStore(), config).repository_status(), indent=2))
            return 0
        tasks = validate_tasks(json.loads(args.benchmark.read_text())) if args.benchmark else None
        client = OllamaClient()
        def new_agent(use_graph: bool) -> InspectionAgent:
            return InspectionAgent(client, InspectionTools(repository, graph if use_graph else None, EvidenceStore(), config), config)
        reports = []
        if tasks is not None:
            modes = [False, True] if args.compare else [not args.source_only]
            for task in tasks:
                for use_graph in modes:
                    report = new_agent(use_graph).ask(task["question"])
                    report["evaluation"] = evaluate(task, report)
                    reports.append(report)
                    print(json.dumps(report["evaluation"], indent=2))
        else:
            agent = new_agent(not args.source_only)
            while True:
                if args.question is not None:
                    question = args.question
                else:
                    try:
                        question = input("inspect> ").strip()
                    except EOFError:
                        break
                    if question in {"quit", "exit"}:
                        break
                    if not question:
                        continue
                report = agent.ask(question)
                reports.append(report)
                print(report["answer"])
                for reference in report["references"]:
                    print(reference)
                audit = report["citation_audit"]
                if audit["invalid_citations"]:
                    print("Citation check: unknown or undelivered evidence IDs: " + ", ".join(audit["invalid_citations"]), file=sys.stderr)
                elif not audit["has_citations"]:
                    print("Citation check: no source citations; answer has not been evidence-validated.", file=sys.stderr)
                if args.question is not None:
                    break
        if args.trace:
            save_report(args.trace, {"schema_version": 1, "reports": reports})
        return 0
    except (ValueError, TypeError, KeyError, OSError, RuntimeError) as exc:
        print(f"Inspection failed: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
