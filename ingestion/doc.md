## Ingestion Service

### Objective of this Document

The objective of this document is to serve as a technical reference and guide for understanding the architecture, design, and implementation of the Odysseus Ingestion Service.

The goal is to develop a mental model of the system that is both precise, comprehensive, and extensible

### Summary 

The **Odysseus Ingestion Service** is responsible for acquiring, processing, and persisting information from external data sources into the Odysseus knowledge infrastructure.

Its primary purpose is to transform heterogeneous data sources into structured, searchable, and interconnected representations that can support downstream information retrieval, knowledge discovery, and reasoning.

#### Motivating Example
Suppose Alice is a software engineer at Amazon. Her team maintains several GitHub repositories containing application code, documentation, configuration files, and other technical artifacts.

Alice wants to integrate Odysseus into her team's development workflow so that engineers and intelligent agents can explore their codebase, retrieve relevant information, understand dependencies, and reason about the team's software systems.

To accomplish this, Alice must first make the team's knowledge accessible to Odysseus.

She initiates an ingestion request specifying a GitHub repository as the data source:

```
odysseus ingest github https://github.com/example/team-repository
```

The ingestion service is then responsible for:

**Acquisition**: Connecting to the specified source and retrieving its artifacts, such as source files, documentation, and configuration files.

**Processing**: Transforming each acquired artifact into a structured representation that preserves information relevant to downstream applications.

**Indexing**: Producing representations that enable efficient search, retrieval, and navigation, such as text indexes, vector embeddings, and knowledge graph relationships.

**Persistence**: Storing the resulting representations in the appropriate storage systems.

Once ingestion is complete, the team's information becomes available to downstream Odysseus components.    


### End-to-End Example

#### Compiling

Requires CMake 3.18+, a C/C++ compiler, nlohmann-json 3.11+, PostgreSQL client
headers, and libcurl development headers. On Ubuntu:

```bash
sudo apt-get install build-essential cmake nlohmann-json3-dev libpq-dev libcurl4-openssl-dev
```

The initial configuration downloads pinned, SHA-256-verified Tree-sitter runtime
and C/C++ grammar sources from GitHub, so it requires network access.

Run these commands from the repository root:

```bash
# Configure the build
cmake -S ingestion -B build/ingestion -DBUILD_TESTING=ON
```

```bash
# Compile the project
cmake --build build/ingestion
```

Run the test suite after building:

```bash
ctest --test-dir build/ingestion --output-on-failure
```

Database integration tests skip unless `ODYSSEUS_TEST_DATABASE_URL` and
`ODYSSEUS_GITHUB_TEST_DATABASE_URL`, respectively, point to isolated empty test
databases. The latter test also requires access to GitHub.

### Command Line Interface (CLI)

The user communicates with the system striclty through the CLI.

There are currently **two-modes of interaction**:

1. Ingesting data into **PostgreSQL**

```bash
odysseus ingest github <repository-uri> [options]
```

2. Indexing data into **Qdrant** (Vector Database) and (or) **Neo4j** (Graph Database)

```bash
odyseus ingest-index vector|graph|all
```

#### Ingesting data into PostgreSQL

When a user runs:

```bash
odysseus ingest github https://github.com/company/repo --postgres
```

The following sequence of events occur:

First, the shell finds the `odysseus` executable `(src/main.cpp)` and starts it as a new process. The operating system
then begins executing at the programs `main()` function

`src/main.cpp` is responsibe for **orchestrating** the following sequence of events:

It does not *own* the logic or state of any of these events, it simply executes them.

1. Parse cli args
2. Load configuration
3. Build the Ingestion module
4. Execute Ingestion

More precisely,

when a user runs the service:
```bash
./build/ingestion/odysseus ingest github https://github.com/company/repo --postgres
```

The executable `ingestion/src/main.cpp` accepts:

```c++
int argc = 5;
char * argv[] = ["./build/ingestion/odysseus", "ingest", "github", "https://github.com/company/repo, "--postgres"]
```

as parameters to the main function:

```c++
int main(int argc, char*argv[]);
```


**First Event:**  Parse cli arguments

```c++
const auto options = parse_cli(argc, argv);
```

This gives us an object of type `CliOptions` which we will use for downstream events.


**Second Event:** Using the `options` data model, **load the configuration** to be used by the ```Ingestion```

```c++
const auto config = load_config(options);
```


**Third Event:** Using the ``config``, build the application.

```c++
auto ingestion_service = initialize_service(config, std::cout);
```


**Fourth Event:** Execute Ingestion through the ```Ingestion.ingest``` method

```c++
const auto result = ingestion_service.ingest(options.source)
```
<br>

#### What happens inside of `Ingestion.ingest(SourceRequest source)`?

The function accepts the following data model as input:

```c++
struct SourceRequest {
    SourceType type = SourceType::Unknown,
    std::string uri;
    json options = json::object();
};
```

Here `SourceType` refers to the following sources:

```c++
# Currently only Github is being explored
enum class SourceType {
    Github,
    LocalFile,
    Database,
    Unknown
};
```

`initialize_service()` constructs a `SourceFactory` and registers the GitHub adapter creator.
It also registers placeholders for `LocalFile` and `Database` that throw a source-specific
"not implemented yet" error when selected. The CLI accepts `github`, `local-file`, and
`database`; only GitHub currently performs ingestion.
The creator captures a shared pointer to the GitHub client, keeping it alive for the
lifetime of the factory. The factory and storage are then moved into `Ingestion`.

```c++
SourceFactory source_factory;
source_factory.register_source(SourceType::GitHub, [client](const SourceRequest& source) {
    return std::make_unique<GitHubAdapter>(*client, source);
});
```

Each call to `Ingestion::ingest()` builds a pipeline using the stored dependencies:

```c++
IngestionPipeline pipeline(source_factory_, processor_, *storage_);
return pipeline.ingest(request);
```

The pipeline asks the factory for a fresh `SourceAdapter` for each request.
`Ingestion` therefore does not need GitHub-specific logic, and repeated ingestion
calls each get their own adapter session.

#### What happens inside of `IngestionPipeline.ingest(SourceRequest request)`?

1. Create the adapter
2. Instantiate ContentClassifier
3. Run processing loop

#### What happens inside of `processor_.process(*artifact)`?

#### What happens inside of `storage_.write(processor_.process(*artifact))`?

<br>

### Indexing Data into Qdrant & Neo4j

### Exporting models locally

```bash
./build/ingestion-debug/odysseus ingest github https://github.com/OWNER/REPO --local ./exports/run-1
```

Add `--ref BRANCH_OR_COMMIT` to select a revision. `--local` and `--postgres`
are mutually exclusive. The destination must not already exist; parent directories
are created automatically. No database server is needed for local output.

Each supported artifact becomes a pretty-printed JSON file (`1.json`, `2.json`,
etc.) containing `artifact_id`, `model_type`, and `model`. The model includes
file metadata, repository provenance, and—for code—functions, classes, calls,
scopes, parser versions, and diagnostics. Repository paths are stored in
`model.file.path`, not used as output filenames. Code exports contain extracted
structure, not a copy of the original source text; document models include text.
If ingestion fails midway, files already written remain in the output directory.
Use a new directory for another run.
