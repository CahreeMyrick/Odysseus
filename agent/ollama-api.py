from collections.abc import Iterator
from ollama import ChatResponse, chat


# intialize the conversation context
messages: list[dict[str, str]]  = [{"role": "user", "content": "What is 17 × 23?"}]

# instantiate a ChatResponse Iterator
stream: Iterator[ChatResponse] = chat(model="qwen3:8b-q4_K_M", messages=messages, stream=True, think=False)

# stream responses
content = ""
for chunk in stream:
    if chunk.message.content:
        content += chunk.message.content
        print(chunk.message.content, end="", flush=True)

# add complete response to the conversation context
messages.append({
    "role": "assistant",
    "content": content,
})

print(messages)
