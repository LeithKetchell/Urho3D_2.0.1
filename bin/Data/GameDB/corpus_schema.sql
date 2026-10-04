-- Yuki corpus curation schema
-- Every byte traceable. If you can't prove it's clean, it doesn't go in.

PRAGMA journal_mode=WAL;
PRAGMA foreign_keys=ON;

-- ============================================================================
-- Sources — where data comes from
-- ============================================================================
CREATE TABLE IF NOT EXISTS sources (
    id          INTEGER PRIMARY KEY,
    name        TEXT NOT NULL UNIQUE,        -- "Project Gutenberg", "Wikipedia", "arXiv", etc.
    url         TEXT,                         -- base URL
    license_id  INTEGER,                     -- default license for this source
    bulk_method TEXT,                         -- "mirror", "api", "scrape", "manual"
    notes       TEXT,
    added       TEXT NOT NULL DEFAULT (datetime('now')),
    FOREIGN KEY (license_id) REFERENCES licenses(id)
);

-- ============================================================================
-- Licenses — known license types with legal status
-- ============================================================================
CREATE TABLE IF NOT EXISTS licenses (
    id          INTEGER PRIMARY KEY,
    spdx        TEXT NOT NULL UNIQUE,        -- SPDX identifier: "PD", "CC0-1.0", "CC-BY-4.0", etc.
    name        TEXT NOT NULL,               -- human name
    clean       INTEGER NOT NULL DEFAULT 0,  -- 1 = approved for training, 0 = needs review
    requires_attribution INTEGER NOT NULL DEFAULT 0,
    share_alike INTEGER NOT NULL DEFAULT 0,
    notes       TEXT
);

-- Seed known clean licenses
INSERT OR IGNORE INTO licenses (spdx, name, clean, requires_attribution, share_alike) VALUES
    ('PD',          'Public Domain',                    1, 0, 0),
    ('CC0-1.0',     'Creative Commons Zero 1.0',        1, 0, 0),
    ('CC-BY-4.0',   'Creative Commons Attribution 4.0', 1, 1, 0),
    ('CC-BY-SA-4.0','CC Attribution-ShareAlike 4.0',    1, 1, 1),
    ('CC-BY-3.0',   'Creative Commons Attribution 3.0', 1, 1, 0),
    ('CC-BY-SA-3.0','CC Attribution-ShareAlike 3.0',    1, 1, 1),
    ('MIT',         'MIT License',                      1, 1, 0),
    ('Apache-2.0',  'Apache License 2.0',               1, 1, 0),
    ('BSD-2-Clause','BSD 2-Clause',                     1, 1, 0),
    ('BSD-3-Clause','BSD 3-Clause',                     1, 1, 0),
    ('ODbL-1.0',    'Open Data Commons ODbL 1.0',       1, 1, 1),
    ('US-GOV',      'U.S. Government Work',             1, 0, 0),
    ('GFDL-1.3',    'GNU Free Documentation License',   1, 1, 1);

-- Seed known unclean licenses (flagged for rejection)
INSERT OR IGNORE INTO licenses (spdx, name, clean, requires_attribution, share_alike, notes) VALUES
    ('CC-BY-NC-4.0',   'CC Attribution-NonCommercial 4.0',    0, 1, 0, 'NC clause prohibits commercial training'),
    ('CC-BY-NC-SA-4.0','CC Attribution-NC-ShareAlike 4.0',    0, 1, 1, 'NC clause prohibits commercial training'),
    ('CC-BY-ND-4.0',   'CC Attribution-NoDerivatives 4.0',    0, 1, 0, 'ND clause prohibits derivative works'),
    ('UNKNOWN',         'Unknown / Unverified',                0, 0, 0, 'Must not be used until license is verified');

-- ============================================================================
-- Documents — individual works in the corpus
-- ============================================================================
CREATE TABLE IF NOT EXISTS documents (
    id          INTEGER PRIMARY KEY,
    source_id   INTEGER NOT NULL,
    license_id  INTEGER NOT NULL,
    title       TEXT,
    author      TEXT,
    lang        TEXT NOT NULL DEFAULT 'en',  -- ISO 639-1
    pub_year    INTEGER,                     -- original publication year (NULL if unknown)
    url         TEXT,                         -- retrieval URL
    sha256      TEXT NOT NULL UNIQUE,        -- content hash for dedup
    word_count  INTEGER NOT NULL DEFAULT 0,
    byte_size   INTEGER NOT NULL DEFAULT 0,
    status      TEXT NOT NULL DEFAULT 'pending',  -- pending, approved, rejected, quarantined
    quality     REAL,                         -- 0.0-1.0 quality score (NULL = unscored)
    retrieved   TEXT NOT NULL DEFAULT (datetime('now')),
    reviewed    TEXT,                         -- date of human/automated review
    reviewer    TEXT,                         -- who approved/rejected
    reject_reason TEXT,                      -- why rejected (NULL if approved)
    notes       TEXT,
    FOREIGN KEY (source_id)  REFERENCES sources(id),
    FOREIGN KEY (license_id) REFERENCES licenses(id)
);

CREATE INDEX IF NOT EXISTS idx_documents_source   ON documents(source_id);
CREATE INDEX IF NOT EXISTS idx_documents_license  ON documents(license_id);
CREATE INDEX IF NOT EXISTS idx_documents_status   ON documents(status);
CREATE INDEX IF NOT EXISTS idx_documents_lang     ON documents(lang);
CREATE INDEX IF NOT EXISTS idx_documents_sha256   ON documents(sha256);

-- ============================================================================
-- Chunks — training-sized text segments
-- ============================================================================
CREATE TABLE IF NOT EXISTS chunks (
    id          INTEGER PRIMARY KEY,
    document_id INTEGER NOT NULL,
    seq         INTEGER NOT NULL,            -- chunk order within document (0-indexed)
    text        TEXT NOT NULL,
    token_count INTEGER,                     -- estimated token count (NULL = uncounted)
    sha256      TEXT NOT NULL,               -- chunk hash for dedup
    FOREIGN KEY (document_id) REFERENCES documents(id) ON DELETE CASCADE,
    UNIQUE(document_id, seq)
);

CREATE INDEX IF NOT EXISTS idx_chunks_document ON chunks(document_id);
CREATE INDEX IF NOT EXISTS idx_chunks_sha256   ON chunks(sha256);

-- ============================================================================
-- Provenance — audit trail
-- ============================================================================
CREATE TABLE IF NOT EXISTS provenance (
    id          INTEGER PRIMARY KEY,
    document_id INTEGER NOT NULL,
    action      TEXT NOT NULL,               -- "ingested", "approved", "rejected", "quarantined", "re-reviewed"
    actor       TEXT NOT NULL,               -- who did it
    timestamp   TEXT NOT NULL DEFAULT (datetime('now')),
    details     TEXT,                         -- freeform context
    FOREIGN KEY (document_id) REFERENCES documents(id) ON DELETE CASCADE
);

CREATE INDEX IF NOT EXISTS idx_provenance_document ON provenance(document_id);

-- ============================================================================
-- Sources: intentionally NOT seeded.
-- The trust mechanism is preserved (Yuki /trust /untrust /sources), but trust
-- is an operator decision made at runtime — the schema ships zero default
-- sources so nothing is implicitly trusted. Add sources via Yuki's console.
-- ============================================================================
