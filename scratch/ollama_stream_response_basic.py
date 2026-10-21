
"""
Here we leverage the ollama python api to query a model and generate responses.

We begin by initializing the conversation_state. What this is meant to do is store the sequence of prompts and responses between the 
agent and the user. In this example, we will initialize it with [{"role": "user", "content": "what is 17 * 23"}].

Next we will instantiate a ChatResponse iterator to enable streaming responses from the agent. Call this iterator: stream.

Supppose we have our ChatResponse iterator: stream, then we can repeatedly request elements, one by one from the iterator, to produce an output stream. We can do this using a for loop.

During each iteration of the loop, we can increment a string, call it: content, to maintain a string of the entire response.

Following, the loop, we can append this Interaction to the context state, for example: context_state.extend({"role": "agent":, "content": content})

"""

from collections.abc import Iterator
from ollama import chat, ChatResponse

# initialize conversation context
conversation_context: list[dict[str, str]] = [{"role": "user", "content": "what's 17 * 23"}]

# instantiate a ChatResponse iterator
stream: Iterator[ChatResponse] = chat(model="qwen3:8b-q4_K_M", messages=conversation_context, stream=True, think=False)

# intialize content string
content = ''

# stream responses
for chunk in stream:
    content+=chunk.messages.content

print(content)
