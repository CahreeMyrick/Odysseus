// github_ingestion.cpp
//
// Example:
//   g++ -std=c++20 github_ingestion.cpp -o github_ingestion
//   ./github_ingestion
//
// Dependency:
//   nlohmann/json
//
// macOS:
//   brew install nlohmann-json

#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

using json = nlohmann::json;


// ============================================================
// ENUMS
// ============================================================

enum class ArtifactType {
    File,
    Document,
    Commit,
    Issue,
    PullRequest
};

enum class ClaimType {
    Extracted,
    Derived
};

enum class ClaimStatus {
    Unverified,
    Supported,
    Contradicted,
    Verified
};


// ============================================================
// ENUM SERIALIZATION
// ============================================================

std::string to_string(ArtifactType type)
{
    switch (type) {
        case ArtifactType::File:
            return "file";

        case ArtifactType::Document:
            return "document";

        case ArtifactType::Commit:
            return "commit";

        case ArtifactType::Issue:
            return "issue";

        case ArtifactType::PullRequest:
            return "pull_request";
    }

    return "unknown";
}


std::string to_string(ClaimType type)
{
    switch (type) {
        case ClaimType::Extracted:
            return "extracted";

        case ClaimType::Derived:
            return "derived";
    }

    return "unknown";
}


std::string to_string(ClaimStatus status)
{
    switch (status) {
        case ClaimStatus::Unverified:
            return "unverified";

        case ClaimStatus::Supported:
            return "supported";

        case ClaimStatus::Contradicted:
            return "contradicted";

        case ClaimStatus::Verified:
            return "verified";
    }

    return "unknown";
}


// ============================================================
// COMMON TYPES
// ============================================================

struct Location {
    int start_line = 0;
    int end_line = 0;
};


struct ExtractorInfo {
    std::string name;
    std::string version;
};


struct Provenance {
    std::string source_id;
    std::string artifact_id;

    std::string repository;
    std::string branch;

    std::string commit_sha;
    std::string blob_sha;

    std::string retrieved_at;

    Location location;

    ExtractorInfo extractor;
};


// ============================================================
// SOURCE
// ============================================================

struct Source {
    std::string provider;
    std::string source_type;
    std::string source_id;

    std::string name;
    std::string url;
};


// ============================================================
// SNAPSHOT
// ============================================================

struct Snapshot {
    std::string version;
    std::string branch;
    std::string retrieved_at;
};


// ============================================================
// ARTIFACT
// ============================================================

struct Artifact {
    std::string id;

    ArtifactType type = ArtifactType::File;

    std::string path;
    std::string language;

    std::string content_hash;

    std::size_t size_bytes = 0;

    Provenance provenance;
};


// ============================================================
// CHUNK
// ============================================================

struct Chunk {
    std::string id;

    std::string artifact_id;

    std::string chunk_type;

    std::string language;

    std::string text;

    Location location;

    std::vector<std::string> symbols;

    Provenance provenance;
};


// ============================================================
// ENTITY
// ============================================================

struct Entity {
    std::string id;

    std::string type;

    std::string name;
    std::string qualified_name;

    std::string artifact_id;

    std::string language;

    Location location;

    Provenance provenance;
};


// ============================================================
// RELATIONSHIP
// ============================================================

struct Relationship {
    std::string id;

    std::string subject;

    std::string predicate;

    std::string object;

    double confidence = 1.0;

    Provenance provenance;
};


// ============================================================
// EVIDENCE
// ============================================================

struct Evidence {
    std::string chunk_id;
};


// ============================================================
// CLAIM
// ============================================================

struct Claim {
    std::string id;

    std::string statement;

    ClaimType claim_type = ClaimType::Extracted;

    ClaimStatus status = ClaimStatus::Unverified;

    std::vector<Evidence> evidence;

    double confidence = 0.0;

    Provenance provenance;
};


// ============================================================
// TOP-LEVEL KNOWLEDGE DOCUMENT
// ============================================================

struct KnowledgeDocument {
    Source source;

    Snapshot snapshot;

    std::vector<Artifact> artifacts;

    std::vector<Chunk> chunks;

    std::vector<Entity> entities;

    std::vector<Relationship> relationships;

    std::vector<Claim> claims;
};


// ============================================================
// JSON SERIALIZATION
// ============================================================

void to_json(json& j, const Location& location)
{
    j = {
        {"start_line", location.start_line},
        {"end_line", location.end_line}
    };
}


void to_json(json& j, const ExtractorInfo& extractor)
{
    j = {
        {"name", extractor.name},
        {"version", extractor.version}
    };
}


void to_json(json& j, const Provenance& provenance)
{
    j = {
        {"source_id", provenance.source_id},

        {"artifact_id", provenance.artifact_id},

        {"repository", provenance.repository},

        {"branch", provenance.branch},

        {"commit_sha", provenance.commit_sha},

        {"blob_sha", provenance.blob_sha},

        {"retrieved_at", provenance.retrieved_at},

        {"location", provenance.location},

        {"extractor", provenance.extractor}
    };
}


void to_json(json& j, const Source& source)
{
    j = {
        {"provider", source.provider},

        {"source_type", source.source_type},

        {"source_id", source.source_id},

        {"name", source.name},

        {"url", source.url}
    };
}


void to_json(json& j, const Snapshot& snapshot)
{
    j = {
        {"version", snapshot.version},

        {"branch", snapshot.branch},

        {"retrieved_at", snapshot.retrieved_at}
    };
}


void to_json(json& j, const Artifact& artifact)
{
    j = {
        {"id", artifact.id},

        {"type", to_string(artifact.type)},

        {"path", artifact.path},

        {"language", artifact.language},

        {"content_hash", artifact.content_hash},

        {
            "metadata",
            {
                {"size_bytes", artifact.size_bytes}
            }
        },

        {"provenance", artifact.provenance}
    };
}


void to_json(json& j, const Chunk& chunk)
{
    j = {
        {"id", chunk.id},

        {"artifact_id", chunk.artifact_id},

        {"chunk_type", chunk.chunk_type},

        {"language", chunk.language},

        {"text", chunk.text},

        {"location", chunk.location},

        {"symbols", chunk.symbols},

        {"provenance", chunk.provenance}
    };
}


void to_json(json& j, const Entity& entity)
{
    j = {
        {"id", entity.id},

        {"type", entity.type},

        {"name", entity.name},

        {"qualified_name", entity.qualified_name},

        {"artifact_id", entity.artifact_id},

        {"language", entity.language},

        {"location", entity.location},

        {"provenance", entity.provenance}
    };
}


void to_json(json& j, const Relationship& relationship)
{
    j = {
        {"id", relationship.id},

        {"subject", relationship.subject},

        {"predicate", relationship.predicate},

        {"object", relationship.object},

        {"confidence", relationship.confidence},

        {"provenance", relationship.provenance}
    };
}


void to_json(json& j, const Evidence& evidence)
{
    j = {
        {"chunk_id", evidence.chunk_id}
    };
}


void to_json(json& j, const Claim& claim)
{
    j = {
        {"id", claim.id},

        {"statement", claim.statement},

        {"claim_type", to_string(claim.claim_type)},

        {"status", to_string(claim.status)},

        {"evidence", claim.evidence},

        {"confidence", claim.confidence},

        {"provenance", claim.provenance}
    };
}


void to_json(json& j, const KnowledgeDocument& document)
{
    j = {
        {"source", document.source},

        {"snapshot", document.snapshot},

        {"artifacts", document.artifacts},

        {"chunks", document.chunks},

        {"entities", document.entities},

        {"relationships", document.relationships},

        {"claims", document.claims}
    };
}


// ============================================================
// EXAMPLE GITHUB MODEL
//
// Later this would be populated using the GitHub API.
// ============================================================

struct GitHubFile {
    std::string path;

    std::string sha;

    std::string content;

    std::string language;

    std::size_t size_bytes = 0;
};


// ============================================================
// NORMALIZATION
// ============================================================

Artifact normalize_artifact(
    const GitHubFile& file,
    const std::string& repository,
    const std::string& branch,
    const std::string& commit_sha
)
{
    Artifact artifact;

    artifact.id =
        "artifact:" + repository + ":" + file.path;

    artifact.type = ArtifactType::File;

    artifact.path = file.path;

    artifact.language = file.language;

    artifact.content_hash = file.sha;

    artifact.size_bytes = file.size_bytes;

    artifact.provenance.source_id =
        "github:" + repository;

    artifact.provenance.artifact_id =
        artifact.id;

    artifact.provenance.repository =
        repository;

    artifact.provenance.branch =
        branch;

    artifact.provenance.commit_sha =
        commit_sha;

    artifact.provenance.blob_sha =
        file.sha;

    artifact.provenance.retrieved_at =
        "2026-10-02T13:00:00Z";

    artifact.provenance.extractor = {
        "odysseus-github-ingestor",
        "0.1.0"
    };

    return artifact;
}


// ============================================================
// EXAMPLE CHUNKING
//
// Temporary implementation.
//
// Later replace this with:
//     AST-aware chunking
//     Tree-sitter
//     Clang tooling
//     semantic document chunking
// ============================================================

Chunk create_example_chunk(
    const Artifact& artifact,
    const GitHubFile& file
)
{
    Chunk chunk;

    chunk.id =
        "chunk:" + artifact.id + ":0";

    chunk.artifact_id =
        artifact.id;

    chunk.chunk_type =
        "file";

    chunk.language =
        file.language;

    chunk.text =
        file.content;

    chunk.location = {
        1,
        1
    };

    chunk.provenance =
        artifact.provenance;

    chunk.provenance.location =
        chunk.location;

    return chunk;
}


// ============================================================
// OUTPUT
// ============================================================

bool write_json(
    const KnowledgeDocument& document,
    const std::string& filename
)
{
    std::ofstream out(filename);

    if (!out) {
        std::cerr
            << "Error: could not open output file: "
            << filename
            << '\n';

        return false;
    }

    json j = document;

    out << j.dump(4);

    return true;
}


// ============================================================
// MAIN
// ============================================================

int main()
{
    // --------------------------------------------------------
    // Example repository metadata
    // --------------------------------------------------------

    const std::string repository =
        "cahree/odysseus";

    const std::string branch =
        "main";

    const std::string commit_sha =
        "abc123";


    // --------------------------------------------------------
    // Example GitHub file
    //
    // Eventually fetched from GitHub.
    // --------------------------------------------------------

    GitHubFile github_file{
        .path = "src/index.cpp",

        .sha = "f82ad123",

        .content =
R"(#include <iostream>

class InvertedIndex {
public:
    void add_document(const std::string& document)
    {
        std::cout << "Indexing document\n";
    }
};
)",

        .language = "cpp",

        .size_bytes = 180
    };


    // --------------------------------------------------------
    // Create canonical Odysseus document
    // --------------------------------------------------------

    KnowledgeDocument knowledge;


    // --------------------------------------------------------
    // Source
    // --------------------------------------------------------

    knowledge.source = {
        .provider = "github",

        .source_type = "repository",

        .source_id = "github:" + repository,

        .name = repository,

        .url =
            "https://github.com/" + repository
    };


    // --------------------------------------------------------
    // Snapshot
    // --------------------------------------------------------

    knowledge.snapshot = {
        .version = commit_sha,

        .branch = branch,

        .retrieved_at =
            "2026-10-02T13:00:00Z"
    };


    // --------------------------------------------------------
    // Normalize GitHub file
    // --------------------------------------------------------

    Artifact artifact =
        normalize_artifact(
            github_file,
            repository,
            branch,
            commit_sha
        );

    knowledge.artifacts.push_back(
        artifact
    );


    // --------------------------------------------------------
    // Chunk file
    // --------------------------------------------------------

    Chunk chunk =
        create_example_chunk(
            artifact,
            github_file
        );

    knowledge.chunks.push_back(
        chunk
    );


    // --------------------------------------------------------
    // Example entity
    //
    // Eventually extracted using Tree-sitter / Clang.
    // --------------------------------------------------------

    Entity inverted_index;

    inverted_index.id =
        "entity:InvertedIndex";

    inverted_index.type =
        "class";

    inverted_index.name =
        "InvertedIndex";

    inverted_index.qualified_name =
        "InvertedIndex";

    inverted_index.artifact_id =
        artifact.id;

    inverted_index.language =
        "cpp";

    inverted_index.location = {
        3,
        9
    };

    inverted_index.provenance =
        artifact.provenance;

    inverted_index.provenance.location =
        inverted_index.location;

    knowledge.entities.push_back(
        inverted_index
    );


    // --------------------------------------------------------
    // Example method entity
    // --------------------------------------------------------

    Entity add_document;

    add_document.id =
        "entity:InvertedIndex::add_document";

    add_document.type =
        "method";

    add_document.name =
        "add_document";

    add_document.qualified_name =
        "InvertedIndex::add_document";

    add_document.artifact_id =
        artifact.id;

    add_document.language =
        "cpp";

    add_document.location = {
        5,
        8
    };

    add_document.provenance =
        artifact.provenance;

    add_document.provenance.location =
        add_document.location;

    knowledge.entities.push_back(
        add_document
    );


    // --------------------------------------------------------
    // Relationship
    // --------------------------------------------------------

    Relationship relationship;

    relationship.id =
        "relationship:1";

    relationship.subject =
        inverted_index.id;

    relationship.predicate =
        "defines";

    relationship.object =
        add_document.id;

    relationship.confidence =
        1.0;

    relationship.provenance =
        artifact.provenance;

    knowledge.relationships.push_back(
        relationship
    );


    // --------------------------------------------------------
    // Claim
    // --------------------------------------------------------

    Claim claim;

    claim.id =
        "claim:1";

    claim.statement =
        "InvertedIndex defines the add_document method.";

    claim.claim_type =
        ClaimType::Extracted;

    claim.status =
        ClaimStatus::Supported;

    claim.evidence.push_back(
        Evidence{
            .chunk_id = chunk.id
        }
    );

    claim.confidence =
        1.0;

    claim.provenance =
        artifact.provenance;

    knowledge.claims.push_back(
        claim
    );


    // --------------------------------------------------------
    // Write final normalized representation
    // --------------------------------------------------------

    const std::string output_file =
        "odysseus_knowledge.json";

    if (!write_json(
            knowledge,
            output_file
        )) {
        return 1;
    }


    std::cout
        << "Knowledge representation written to "
        << output_file
        << '\n';


    return 0;
}

# g++ -std=c++20 github_ingestion.cpp -o github_ingestion
# ./github_ingestion

# it produces:
# odysseus_knowledge.json

