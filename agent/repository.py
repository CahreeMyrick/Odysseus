"""Bounded, read-only access to a checkout. No model-supplied shell commands."""
import hashlib
import os
import subprocess
from pathlib import Path

from .config import Config


EXCLUDED = {".git", ".venv", "venv", "node_modules", "build", "__pycache__", ".pytest_cache"}


class Repository:
    def __init__(self, root: Path, config: Config):
        self.root = Path(root).resolve(strict=True)
        if not self.root.is_dir():
            raise ValueError("Repository root must be a directory")
        self.config = config

    def _git(self, *args: str) -> bytes | None:
        try:
            process = subprocess.run(["git", "-C", str(self.root), *args], capture_output=True,
                                     timeout=10, check=False)
        except (OSError, subprocess.TimeoutExpired):
            return None
        return process.stdout if process.returncode == 0 else None

    def head(self) -> str | None:
        value = self._git("rev-parse", "HEAD")
        return value.decode().strip() if value else None

    def status(self, graph_revision: str | None = None) -> dict:
        head = self.head()
        changes = self._git("status", "--porcelain", "--untracked-files=normal")
        return {"root": str(self.root), "head": head,
                "dirty": bool(changes) if changes is not None else None,
                "graph_revision": graph_revision,
                "revision_matches": head == graph_revision if head and graph_revision else None}

    def resolve(self, path: str) -> Path:
        if not isinstance(path, str) or not path or "\0" in path:
            raise ValueError("Expected a repository-relative path")
        relative = Path(path)
        if relative.is_absolute() or ".." in relative.parts or ".git" in relative.parts:
            raise ValueError("Path must stay inside the repository and outside .git")
        resolved = (self.root / relative).resolve(strict=True)
        if not resolved.is_relative_to(self.root) or ".git" in resolved.relative_to(self.root).parts:
            raise ValueError("Path escapes the repository")
        if not resolved.is_file():
            raise ValueError("Path is not a regular file")
        return resolved

    def read(self, path: str) -> tuple[bytes, str]:
        resolved = self.resolve(path)
        with resolved.open("rb") as stream:
            raw = stream.read(self.config.max_file_bytes + 1)
        if len(raw) > self.config.max_file_bytes:
            raise ValueError("File exceeds source-read byte limit")
        if b"\0" in raw:
            raise ValueError("Binary file is not supported")
        return raw, raw.decode("utf-8")

    def symbol_source_matches(self, path: str, raw: bytes, revision: str) -> bool:
        # A graph commit must be an actual Git object, not an arbitrary rev expression.
        if not revision or any(c not in "0123456789abcdefABCDEF" for c in revision) or len(revision) not in (40, 64):
            return False
        if self.head() != revision:
            return False
        original = self._git("show", f"{revision}:{path}")
        return original is not None and original == raw

    def source_record(self, path: str, raw: bytes, start: int, end: int, content: str) -> dict:
        return {"kind": "source", "path": path, "revision": self.head(),
                "snapshot": "working_tree", "sha256": hashlib.sha256(raw).hexdigest(),
                "start_line": start, "end_line": end, "text": content}

    def search(self, query: str, path_filter: str, limit: int) -> dict:
        matches, skipped = [], 0
        scanned, total_bytes = 0, 0
        truncated = False
        for directory, directories, filenames in os.walk(self.root, followlinks=False):
            directories[:] = sorted(d for d in directories if d not in EXCLUDED and
                                    not (Path(directory) / d).is_symlink())
            for filename in sorted(filenames):
                path = (Path(directory) / filename).relative_to(self.root).as_posix()
                if path_filter and path_filter not in path:
                    continue
                if scanned >= self.config.max_search_files or total_bytes >= self.config.max_search_bytes:
                    truncated = True
                    break
                scanned += 1
                try:
                    raw, content = self.read(path)
                except (OSError, ValueError):
                    skipped += 1
                    continue
                if total_bytes + len(raw) > self.config.max_search_bytes:
                    truncated = True
                    break
                total_bytes += len(raw)
                for line, value in enumerate(content.splitlines(), 1):
                    if query.casefold() in value.casefold():
                        if len(matches) >= limit:
                            truncated = True
                            break
                        # Cap single-line results; full source can be requested separately.
                        excerpt = value[:1000]
                        matches.append(self.source_record(path, raw, line, line, excerpt) |
                                       {"excerpt_truncated": len(value) > 1000})
                if truncated:
                    break
            if truncated:
                break
        return {"matches": matches, "truncated": truncated, "skipped_files": skipped,
                "scanned_files": scanned, "scanned_bytes": total_bytes}
