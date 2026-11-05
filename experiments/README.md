# Historical experiments

This directory preserves research code and recorded runs. It is excluded from the
installable Odysseus package and its default test suite.

- `airline/`: earlier airline-policy comparisons; scores use their original protocols.
- `legacy_agent/`: unfinished inspection-agent prototypes and an earlier write-up.

The maintained airline project is now separate:
[policy-constrained-airline-agent](https://github.com/CahreeMyrick/policy-constrained-airline-agent).
Historical results do not measure the current Odysseus inspection agent.

For legacy notebooks, install optional dependencies with `uv sync --group research`.
Follow each experiment's README; old entry points and dependencies are not covered by CI.
