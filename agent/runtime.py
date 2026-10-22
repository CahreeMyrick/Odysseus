"""Provider boundary and bounded tool-calling conversation loop."""
from dataclasses import dataclass
from time import perf_counter
from typing import Protocol

from .config import Config
from .prompts import SYSTEM_PROMPT
from .schemas import encode, result
from .tools import InspectionTools
from .tracing import ToolEvent


class ModelClient(Protocol):
    def complete(self, model: str, messages: list[dict], tools: list) -> dict: ...


class OllamaClient:
    def __init__(self):
        from ollama import Client
        self.client = Client(timeout=120)

    def complete(self, model: str, messages: list[dict], tools: list) -> dict:
        from ollama import ResponseError
        from httpx import HTTPError
        try:
            response = self.client.chat(model=model, messages=messages, tools=tools, think=False,
                                        options={"num_predict": 2048})
        except (ResponseError, HTTPError, ConnectionError) as exc:
            raise RuntimeError(f"Ollama request failed: {exc}") from exc
        return {"message": response.message.model_dump(exclude_none=True),
                "usage": {"input_tokens": response.prompt_eval_count,
                          "output_tokens": response.eval_count}}


@dataclass
class InspectionAgent:
    client: ModelClient
    tools: InspectionTools
    config: Config

    def __post_init__(self):
        self.history = [{"role": "system", "content": SYSTEM_PROMPT}]
        self.delivered: set[str] = set()

    def ask(self, question: str) -> dict:
        if not isinstance(question, str) or not question.strip():
            raise ValueError("Question must not be empty")
        started = perf_counter()
        self.history.append({"role": "user", "content": question})
        events, usage = [], []
        answer = ""
        completion = "tool_limit"
        for _ in range(self.config.max_tool_rounds):
            response = self.client.complete(self.config.model, self.history, list(self.tools.available().values()))
            message = response["message"]
            self.history.append(message)
            usage.append(response.get("usage", {}))
            calls = message.get("tool_calls") or []
            if not calls:
                answer = message.get("content") or "No answer returned."
                completion = "answered"
                break
            for index, call in enumerate(calls):
                function = call.get("function", {})
                name = function.get("name", "")
                arguments = function.get("arguments", {})
                tool_started = perf_counter()
                if index >= self.config.max_calls_per_round:
                    output = result("error", data={"message": "Per-round tool-call budget exceeded"})
                else:
                    output = self.tools.execute(name, arguments)
                self.delivered.update(output["evidence_ids"])
                events.append(ToolEvent(name, arguments, output, (perf_counter() - tool_started) * 1000))
                tool_message = {"role": "tool", "tool_name": name, "content": encode(output)}
                if call.get("id"):
                    tool_message["tool_call_id"] = call["id"]
                self.history.append(tool_message)
        if completion != "answered":
            answer = "Inspection stopped at the tool-round limit. No final answer was produced. Narrow the question or increase the budget."
            self.history.append({"role": "assistant", "content": answer})
        audit = self.tools.evidence.audit(answer, self.delivered)
        return {"question": question, "answer": answer, "completion": completion,
                "citation_audit": audit,
                "references": self.tools.evidence.references([eid for eid in audit["citations"] if eid in self.delivered]),
                "evidence": {eid: self.tools.evidence.records[eid] for eid in sorted(self.delivered)},
                "tool_events": [event.to_dict() for event in events], "usage": usage,
                "elapsed_ms": (perf_counter() - started) * 1000,
                "mode": "graph" if self.tools.graph else "source-only", "model": self.config.model}
