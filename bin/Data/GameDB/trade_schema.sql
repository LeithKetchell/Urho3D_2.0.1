-- Trade system schema — Phase 1: Direct barter + Phase 4: Trade Post
-- Pure barter. No currency. No abstraction.

-- Active trade sessions (runtime, not persisted across restarts)
CREATE TABLE IF NOT EXISTS trade_sessions (
    id          INTEGER PRIMARY KEY,
    player_a    INTEGER NOT NULL,
    player_b    INTEGER NOT NULL,
    locked_a    INTEGER DEFAULT 0,
    locked_b    INTEGER DEFAULT 0,
    created     REAL NOT NULL           -- elapsed time at creation
);

-- Items offered in an active trade session
CREATE TABLE IF NOT EXISTS trade_offers_active (
    session_id  INTEGER NOT NULL REFERENCES trade_sessions(id) ON DELETE CASCADE,
    player_id   INTEGER NOT NULL,       -- which player is offering this
    item_id     INTEGER NOT NULL REFERENCES items(id),
    quantity    INTEGER DEFAULT 1,
    PRIMARY KEY (session_id, player_id, item_id)
);

-- Trade Post passive offers (persisted, async trade)
CREATE TABLE IF NOT EXISTS trade_offers (
    id          INTEGER PRIMARY KEY,
    owner_id    INTEGER NOT NULL,
    post_id     INTEGER NOT NULL,       -- placed_buildings id
    offer_item  INTEGER NOT NULL REFERENCES items(id),
    offer_qty   INTEGER DEFAULT 1,
    want_item   INTEGER NOT NULL REFERENCES items(id),
    want_qty    INTEGER DEFAULT 1,
    created_day INTEGER NOT NULL
);

-- Trade history log (server-side, for dispute resolution)
CREATE TABLE IF NOT EXISTS trade_history (
    id          INTEGER PRIMARY KEY,
    player_a    INTEGER NOT NULL,
    player_b    INTEGER NOT NULL,
    completed   REAL NOT NULL,          -- elapsed time at completion
    items_a     TEXT NOT NULL,           -- JSON: [{item_id, qty}, ...]
    items_b     TEXT NOT NULL            -- JSON: [{item_id, qty}, ...]
);
