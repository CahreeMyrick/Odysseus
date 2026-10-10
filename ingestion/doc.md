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

We build the system with the following commands:

```bash
# Configure the build
cmake -S Ingestion -B build/ingestion
```

```bash
# Compile the project
cmake --build build/ingestion
```

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
3. Build the IngestionApplication module
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


**Second Event:** Using the `options` data model, **load the configuration** to be used by the ```IngestionApplication```

```c++
const auto config = load_config(options);
```


**Third Event:** Using the ``config``, build the application.

```c++
auto application = build_application(config, std::cout);
```


**Fourth Event:** Execute Ingestion through the ```IngestionApplication.ingest``` method

```c++
const auto result = application.ingest(options.source)
```
<br>

#### What happens inside of `IngestionApplication.ingest(SourceRequest source)`?

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

When the flow of control reaches `IngestionResult IngestionApplication.ingest(SourceRequest& request)`, the following sequence of 
events ocurr:

1. Instantiate an `AdapterFactory`
2. Register the adapter 
3. Instantiate an `IngestionPipeline`
4. Run the `IngestionPipline`

**1. Instantiate an `AdapterFactory`**

The role of the `AdapterFactory` is to help is leverage both the **Factory Design Pattern** and the **Adapter Design Pattern** for
to dynamically instantiate the appropriate source adapter, while providing the ingestion pipeline with a common interface for interacting
with different data sources, independent of their underlying implementations.

For example:

Suppose a user requests that Odysseus ingest a Github repo. The `AdapterFactory` examines the `SourceType` specified in the `SourceRequest`
and instantiates a `GithubAdapter`, which implements the common `SourceAdapter` interface.

The **Factory Design Pattern** encapsulates the creation of the appropriate adapter, while the **Adapter Design Pattern** allows Github
specific operations to be exposed through the standardized interface expected by the `IngestionPipeline`

As a result, the ingestion pipeline can acquire artifacts from Github, local files, or other supported sources without needing to know
how each source is acessed or how its underlying APIs operate.

**2. Register the adapter**
```c++
Adapter factory
factory.register_source(SourceType::Github, [this](const SourcRequest& source) {return std::make_unique<GithubAdapter>(*client_, source)});
```

3. Instantiate `IngestionPipeline`

```c++
// NOTE: processor_ and storage_ are member variables of the IngestionApplication class
// NOTE: we dereference storage_ because it is a pointer to ...
IngestionPipline pipeline(factory, processor_, *storage_);
```

4. Run the ingestion pipeline

```c++
pipeline.ingest(request);
```

#### What happens inside of `IngestionPipeline.ingest(SourceRequest request)`?

1. Create the adapter
2. Instantiate ContentClassifier
3. Run processing loop

#### What happens inside of `processor_.process(*artifact)`?

#### What happens inside of `storage_.write(processor_.process(*artifact))`?

<br>

### Indexing Data into Qdrant & Neo4j
