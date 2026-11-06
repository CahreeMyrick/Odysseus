### DEV-0001 

- **Author:** Cahree
- **Timestamp:** 2026-10-22 6:30 AM
- **Type:** CHANGE
- **Status:** COMPLETED
- **Components:** Ingestion
- **Git Commit:** N/A

Details: Changed `AdapterFactory` to `SourceFactory` everywere as the new name gives more context to what it represents. 

### DEV-0002

- **Author:** Cahree
- **Timestamp:** 2026-10-22 6:47 AM
- **Type:** CHANGE
- **Status:** COMPLETED
- **Components:** Ingestion

Details:

Changed `IngestionApplication` to `Ingestion` everywhere as the name is simpler, and renamed files application hpp & cpp to ingestion hpp & cpp. In addition,
I changed the name of the `build_application` method in the `IngestionApplication` class to `initialize_service`.

### DEV-0003

- **Author:** Cahree
- **Timestamp:** 2026-11-06 2:32 AM
- **Type:** CHANGE
- **Status:** IN-PROGRESS
- **Components:** Agent Orchestration

Details:

Refractoring / Redesingning `__main__.py` to be simpler and more sensible.

### DEV-0004

- **Author:** Cahree
- **Timestamp:** 2026-11-06 2:00 PM
- **Type:** CHANGE
- **Status:** IDEA
- **Components:** N/A

Details:

We should use architectural and low-level design patterns to build our system components. We should
use Robert C Martins *Clean Architecture* and Eric/Elisabeth Freemans *Head First Design Patterns*.
This way:

1. We can have a clear and static model of the system.
2. We can reduce architectural drift as the system evolves.
3. We can maxmize collaborative *bandwidth* and *fidelity* both with **each other** and with **AI coding agents** by having a common model of the system
and supporting resources that we can refer.
