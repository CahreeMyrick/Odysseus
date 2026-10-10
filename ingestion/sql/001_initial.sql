CREATE SCHEMA IF NOT EXISTS odysseus_ingestion;

CREATE TABLE IF NOT EXISTS odysseus_ingestion.artifacts (
    id TEXT PRIMARY KEY,
    source_uri TEXT NOT NULL,
    revision TEXT NOT NULL,
    path TEXT NOT NULL,
    content TEXT NOT NULL,
    metadata JSONB NOT NULL,
    updated_at TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP,
    UNIQUE (source_uri, revision, path)
);

CREATE TABLE IF NOT EXISTS odysseus_ingestion.models (
    artifact_id TEXT PRIMARY KEY REFERENCES odysseus_ingestion.artifacts(id),
    model_type TEXT NOT NULL CHECK (model_type IN ('code', 'document')),
    model JSONB NOT NULL,
    updated_at TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP
);
