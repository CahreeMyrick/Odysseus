"""Read-only inspection CLI: odysseus-inspect --help."""

import argparse
import json
import sys
from pathlib import Path

from .config import Config
from .evaluation import evaluate, validate_tasks
from .evidence import EvidenceStore
from .graph_store import GraphStore
from .repository import Repository
from .runtime import InspectionAgent, OllamaClient
from .tools import InspectionTools
from .tracing import save_report
from .chat import chat

def parser() -> argparse.ArgumentParser:
    cli = argparse.ArgumentParser(description="Read-only codebase inspection agent")
    cli.add_argument("--repo", type=Path, required=True, help="Local checkout root")
    cli.add_argument("--graph", type=Path, help="Odysseus graph.json export")
    cli.add_argument("--revision", help="Exact commit recorded in graph (required if ambiguous)")
    cli.add_argument("--source-uri", help="Exact repository URI recorded in graph")
    cli.add_argument("--model", default=Config().model)
    cli.add_argument("--max-tool-rounds", type=int, default=Config().max_tool_rounds)
    cli.add_argument(
        "--source-only", action="store_true", help="Disable all graph tools for a baseline"
    )
    mode = cli.add_mutually_exclusive_group()
    mode.add_argument("--question", help="Ask once; otherwise enter interactive mode")
    mode.add_argument("--benchmark", type=Path, help="Run fixed inspection tasks")
    cli.add_argument(
        "--compare", action="store_true", help="Run benchmark in source-only and graph modes"
    )
    cli.add_argument(
        "--trace", type=Path, help="Save full JSON trace to a new file; may contain source code"
    )
    cli.add_argument(
        "--inspect-only",
        action="store_true",
        help="Validate inputs and print status without an LLM",
    )
    return cli


def main(argv: list[str] | None = None) -> int:
    cli = parser()
    args = cli.parse_args(argv)
    config = Config(model=args.model, max_tool_rounds=args.max_tool_rounds)
    repository = Repository(args.repo, config)
    graph = GraphStore(args.graph, args.revision, args.source_uri)
    client = OllamaClient()
    tools = InspectionTools(repository, graph, config)
    agent = InspectionAgent(client, tools, config)
  
    run_chat(agent)
    responses = [] 
    while True:
        question = input(">>> ").strip()
        if question in {"quit", "exit"}:
            break
        if not question:
            continue
        
        response = agent.ask(question)
        responses.append(response)
        cli.show_response(response)


if __name__ == "__main__":
    main()
