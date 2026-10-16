from pydantic import BaseModel
import argparse
import json
import sys
from urllib.error import HTTPError, URLError
from urllib.request import Request, urlopen


class AgentResponse(BaseModel):
    answer: dict

class Agent:
    def __init__(self, model, host: str = "http://localhost:11434", system: str = "You are a helpful assistant. Answer clearly and precisley."):
        self.model = model
        self.host = host
        self.system = system
        self.messages = {}
        self.clear()

    def clear(self) -> None:
        self.messages = [{"role": "system", "content": self.system}]

    def ask(self, prompt: str) -> AgentResponse:

        # --- add prompt to context --- #
        messages = self.messages + [{"role": "user", "content": prompt}]

        # --- build request --- #
        request = Request(
            f"{self.host}/api/chat",
            data=json.dumps({"model": self.model, "messages": messages,
                             "stream": False}).encode("utf-8"),
            headers={"Content-Type": "application/json"},
            method="POST",
        )
        try:
            with urlopen(request, timeout=300) as response:
                result = json.load(response)
        except HTTPError as exc:
            detail = exc.read().decode("utf-8", errors="replace")
            raise RuntimeError(f"Ollama returned HTTP {exc.code}: {detail}") from exc

        return AgentResponse(answer=result) 
        

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--model", required=True, help="Exact model name from ollama list")
    parser.add_argument("--host", default="http://localhost:11434")
    parser.add_argument("--system", default="You are a helpful assistant. Answer clearly and precisely.")
    parser.add_argument("--prompt", "-p", help="Ask one question and exit")
    args = parser.parse_args()
    
    agent = Agent(args.model, args.host, args.system)
    prompt = args.prompt

    response = agent.ask(prompt)
    
    print(response.answer)
    
if __name__ == "__main__":
    main()
