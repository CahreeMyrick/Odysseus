CREATE SEQUENCE IF NOT EXISTS odysseus_ingestion.indexing_claim_tokens;
CREATE TABLE IF NOT EXISTS odysseus_ingestion.indexing_tasks (
    id BIGINT GENERATED ALWAYS AS IDENTITY PRIMARY KEY,
    artifact_id TEXT NOT NULL REFERENCES odysseus_ingestion.artifacts(id),
    target TEXT NOT NULL CHECK (target IN ('vector', 'graph')),
    payload JSONB NOT NULL,
    generation BIGINT NOT NULL DEFAULT 1,
    status TEXT NOT NULL DEFAULT 'pending' CHECK (status IN ('pending','running','completed','failed')),
    attempts INTEGER NOT NULL DEFAULT 0,
    claim_token BIGINT,
    lease_until TIMESTAMPTZ,
    available_at TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP,
    last_error TEXT,
    updated_at TIMESTAMPTZ NOT NULL DEFAULT CURRENT_TIMESTAMP,
    UNIQUE (artifact_id, target)
);
CREATE INDEX IF NOT EXISTS indexing_tasks_ready
    ON odysseus_ingestion.indexing_tasks (target, status, available_at);
