-- Workboard SQL Schema v7
-- Collective task tracker with owner tracking.
-- v7: removed death_notes + dictionary tables (knowledge lives in yuki_memory.db).
-- v5: owner + started_at columns on tasks.

PRAGMA journal_mode=WAL;
PRAGMA synchronous=NORMAL;

CREATE TABLE IF NOT EXISTS tasks (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    section     TEXT NOT NULL CHECK(section IN
                ('planned','open','in_progress','done')),
    task_name   TEXT NOT NULL,
    priority    INTEGER DEFAULT 0,
    outcome     TEXT DEFAULT '',
    learned     TEXT DEFAULT '',
    blocked_by  TEXT DEFAULT '',
    summary     TEXT DEFAULT '',
    owner       TEXT DEFAULT '',
    started_at  TEXT DEFAULT '',
    sort_order  INTEGER DEFAULT 0,
    created_at  TEXT DEFAULT (datetime('now')),
    updated_at  TEXT DEFAULT (datetime('now'))
);

CREATE UNIQUE INDEX IF NOT EXISTS idx_tasks_name ON tasks(task_name);
CREATE INDEX IF NOT EXISTS idx_tasks_section ON tasks(section);

CREATE TABLE IF NOT EXISTS workboard_meta (
    key   TEXT PRIMARY KEY,
    value TEXT
);
INSERT OR REPLACE INTO workboard_meta VALUES ('schema_version', '6');

-- ============================================================================
-- Rules engine — enforcement lives here, not in markdown
-- ============================================================================

CREATE TABLE IF NOT EXISTS rules (
    id          INTEGER PRIMARY KEY,
    category    TEXT NOT NULL,       -- build, git, process, workflow, ipc, asset, code
    name        TEXT NOT NULL UNIQUE,
    description TEXT NOT NULL,
    enforcement TEXT NOT NULL,       -- hook, manager, advisory
    pattern     TEXT,               -- regex matched against command/file
    action      TEXT NOT NULL,       -- block, warn, log
    message     TEXT,               -- feedback when triggered
    active      INTEGER DEFAULT 1
);

CREATE TABLE IF NOT EXISTS violations (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    rule_id     INTEGER REFERENCES rules(id),
    role        TEXT,
    timestamp   TEXT DEFAULT (datetime('now')),
    context     TEXT,
    outcome     TEXT                -- blocked, warned, logged
);

CREATE INDEX IF NOT EXISTS idx_violations_rule ON violations(rule_id);
CREATE INDEX IF NOT EXISTS idx_violations_role ON violations(role);

-- ── Seed rules ──

-- Build
INSERT OR IGNORE INTO rules (category, name, description, enforcement, pattern, action, message) VALUES
('build', 'no_raw_make', 'Never run make directly. Use safe_build.sh.', 'hook', '^make\b', 'block', 'BLOCKED: Use safe_build.sh, not raw make. Raw make caused deadlocks.'),
('build', 'no_duplicate_builds', 'One build per target, no retries.', 'advisory', NULL, 'warn', 'One build per target. Exit 144 = queue full, ask Leith.');

-- Git
INSERT OR IGNORE INTO rules (category, name, description, enforcement, pattern, action, message) VALUES
('git', 'no_commit_without_ask', 'Never commit without asking first.', 'hook', 'git\s+commit\b', 'block', 'BLOCKED: Do not commit without asking Leith first.'),
('git', 'no_git_push', 'Do not push without explicit go-ahead.', 'hook', 'git\s+push\b', 'block', 'BLOCKED: Do not push without Leith''s go-ahead.'),
('git', 'no_force_push', 'Never force-push to main/master.', 'hook', 'git\s+push\s+.*--force', 'block', 'BLOCKED: Force push forbidden.'),
('git', 'no_destructive_git', 'No reset --hard, checkout ., clean -f.', 'hook', 'git\s+(reset\s+--hard|checkout\s+\.|clean\s+-f)', 'block', 'BLOCKED: Destructive git ops require permission.'),
('git', 'no_archive_upload', 'Never add archives to git.', 'hook', 'git\s+add\s+.*\.(zip|tar|gz|7z|rar)\b', 'block', 'BLOCKED: No archive files in git.');

-- Process
INSERT OR IGNORE INTO rules (category, name, description, enforcement, pattern, action, message) VALUES
('process', 'no_kill_processes', 'Never kill shared processes without permission.', 'hook', '(kill|pkill|killall)\s', 'block', 'BLOCKED: Do not kill processes without permission.'),
('process', 'no_launch_game', 'Never launch game binaries from coder.', 'hook', '\./60_TerrainNode|\./[0-9]+_', 'block', 'BLOCKED: Only Leith launches game binaries.'),
('process', 'no_timed_launches', 'Never launch GUI apps with timeout.', 'hook', 'timeout\s+.*\./|timeout\s+\d+\s+\./', 'block', 'BLOCKED: No timed GUI launches.'),
('process', 'no_pid_management', 'Do not touch PID files or /proc/.', 'hook', '(cat|rm|echo).*instances/.*\.pid|/proc/[0-9]', 'warn', 'WARNING: PID management is handled by hooks, not you.');

-- Workflow
INSERT OR IGNORE INTO rules (category, name, description, enforcement, pattern, action, message) VALUES
('workflow', 'workboard_before_work', 'Update workboard BEFORE starting any task.', 'advisory', NULL, 'warn', 'Update workboard before starting work.'),
('workflow', 'delete_pipeline_cache', 'Delete pipeline cache after shader edits.', 'hook', '\.glsl$', 'warn', 'Shader edited. Delete ~/.local/share/urho3d/pipeline_cache.bin after build.'),
('workflow', 'bump_claudette_version', 'Increment CLAUDETTE_VERSION before rebuild.', 'advisory', NULL, 'warn', 'Bump CLAUDETTE_VERSION before rebuilding. Odd=dev, even=RC.'),
('workflow', 'report_to_workboard', 'When done, report to workboard. It tells the others.', 'manager', NULL, 'block', 'BLOCKED: Update the workboard when work is complete. The workboard notifies other coders.');

-- Assets
INSERT OR IGNORE INTO rules (category, name, description, enforcement, pattern, action, message) VALUES
('asset', 'no_touch_assets', 'Never touch game assets without permission.', 'hook', 'git\s+checkout\s+.*bin/Data|git\s+checkout\s+.*\.(mdl|ani|png)\b', 'block', 'BLOCKED: Do not touch game assets without permission.'),
('asset', 'staging_delete_on_integrate', 'Delete from staging after promoting.', 'advisory', NULL, 'warn', 'Staging is transit. Delete source after promoting.');

-- Code
INSERT OR IGNORE INTO rules (category, name, description, enforcement, pattern, action, message) VALUES
('code', 'no_platform_api', 'Use Urho3D APIs, not raw platform calls.', 'advisory', NULL, 'warn', 'Use Urho3D APIs. If none exists, STOP and ask Leith.');

-- Coordination
INSERT OR IGNORE INTO rules (category, name, description, enforcement, pattern, action, message) VALUES
('workflow', 'no_execute_without_permission', 'Do not execute without permission from Leith.', 'advisory', NULL, 'block', 'BLOCKED: Do not execute without Leith''s permission.'),
('workflow', 'no_assume_peer_roles', 'Do not assume jobs from peers. Your role is your role.', 'advisory', NULL, 'block', 'BLOCKED: Do not take another coder''s role or assignment.'),
('workflow', 'coordinate_with_peers', 'Coordinate with peers via Manager or direct IPC. No solo cowboy moves.', 'advisory', NULL, 'warn', 'WARNING: Coordinate with peers before acting on shared resources.');

