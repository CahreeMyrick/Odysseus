"""Small, explicit budgets for an inspection session."""
from dataclasses import dataclass


@dataclass(frozen=True)
class Config:
    model: str = "qwen3:4b-instruct-16k"
    max_tool_rounds: int = 8
    max_calls_per_round: int = 8
    max_result_chars: int = 24000
    max_source_lines: int = 200
    max_file_bytes: int = 2 * 1024 * 1024
    max_search_files: int = 5000
    max_search_bytes: int = 10 * 1024 * 1024

    def __post_init__(self):
        if not self.model.strip():
            raise ValueError("Model must not be empty")
        for name, value in vars(self).items():
            if name != "model" and (type(value) is not int or value < 1):
                raise ValueError(f"{name} must be a positive integer")
