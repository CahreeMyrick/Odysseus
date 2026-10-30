from collections.abc import Sequence
from cli import parse_args
from runtime import initialize, RuntimeConfig
from chat import chat

def main(argv: Sequence[str] | None = None) -> None:
    options = parse_args(argv)
    config = RuntimeConfig(
            model=options.model,
            database_path=options.database_path,
            knowledge_graph_path=options.knowledge_graph_path
    )

    with initialize(config) as runtime:
        chat(runtime=runtime)

if __name__ == "__main__":
    main()
