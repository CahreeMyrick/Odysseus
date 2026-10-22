import json
from pathlib import Path
import shutil
import subprocess
import tempfile

from agent.config import Config
from agent.evidence import EvidenceStore
from agent.graph_store import GraphStore
from agent.repository import Repository
from agent.tools import InspectionTools

from agent.benchmarks.fixtures import FIXTURE, make_graph


class Workspace:
    def __init__(self):
        self.temp = tempfile.TemporaryDirectory(prefix="odysseus-inspection-")
        self.root = Path(self.temp.name)
        self.repo = self.root / "repo"
        shutil.copytree(FIXTURE, self.repo)
        self.git("init", "-q")
        self.git("add", ".")
        self.git("-c", "user.name=Test", "-c", "user.email=test@example.invalid", "commit", "-qm", "fixture")
        self.revision = self.git("rev-parse", "HEAD").strip()
        self.path = self.root / "graph.json"
        self.write_graph(make_graph(self.revision))
        self.config = Config()

    def git(self, *args):
        return subprocess.run(["git", "-C", str(self.repo), *args], check=True, capture_output=True, text=True).stdout

    def write_graph(self, data):
        self.path.write_text(json.dumps(data))

    def tools(self, graph=True, config=None):
        config = config or self.config
        return InspectionTools(Repository(self.repo, config), GraphStore(self.path) if graph else None, EvidenceStore(), config)

    def close(self):
        self.temp.cleanup()
