"""Explicit markers for methods exposed to the language model."""
from typing import Callable, TypeVar


F = TypeVar("F", bound=Callable)


def llm_tool(*, requires_graph: bool = False) -> Callable[[F], F]:
    """Mark an LLM tool without wrapping its signature or docstring."""
    def decorate(function: F) -> F:
        setattr(function, "__llm_tool_requires_graph__", requires_graph)
        return function
    return decorate


def available_tools(instance, *, has_graph: bool) -> dict[str, Callable]:
    """Expose only explicitly marked methods, including inherited methods."""
    methods = {}
    for cls in reversed(type(instance).__mro__):
        methods.update(vars(cls))
    return {
        name: getattr(instance, name)
        for name, method in methods.items()
        if hasattr(method, "__llm_tool_requires_graph__")
        and (has_graph or not method.__llm_tool_requires_graph__)
    }
