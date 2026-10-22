"""Create an isolated, committed synthetic fixture for the example benchmark."""
import argparse
import json
from pathlib import Path
import shutil
import subprocess

from .fixtures import FIXTURE, make_graph


def prepare(output: Path) -> None:
    output.mkdir(parents=True, exist_ok=False)
    repo = output / "repo"
    shutil.copytree(FIXTURE, repo)
    def git(*args):
        return subprocess.run(["git", "-C", str(repo), *args], capture_output=True, text=True, check=True).stdout
    git("init", "-q")
    git("add", ".")
    git("-c", "user.name=Inspection Fixture", "-c", "user.email=fixture@example.invalid", "commit", "-qm", "inspection fixture")
    revision = git("rev-parse", "HEAD").strip()
    (output / "graph.json").write_text(json.dumps(make_graph(revision), indent=2) + "\n")
    print(f"Repository: {repo}\nGraph: {output / 'graph.json'}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True, help="A new directory")
    args = parser.parse_args()
    try:
        prepare(args.output)
    except (OSError, subprocess.CalledProcessError) as exc:
        parser.exit(1, f"Fixture preparation failed: {exc}\n")


if __name__ == "__main__":
    main()
