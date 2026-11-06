from dataclasses import dataclass
from time import perf_counter
from typing import Protocol

from .config import Config
from .prompts import SYSTEM_PROMPT
from .schemas import encode, result
from .tools import init_tools
from .tracing import ToolEvent

from httpx import HTTPError
from ollama import ResponseError

def chat(tools, ):

