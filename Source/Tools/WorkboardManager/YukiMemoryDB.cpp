// YukiMemoryDB — Short-term memory for Yuki (SQLite).

#include "YukiMemoryDB.h"

#include <Urho3D/IO/Log.h>

#include <SQLite/sqlite3.h>

#include <ctime>

static const char* SCHEMA =
    "CREATE TABLE IF NOT EXISTS memory ("
    "  id INTEGER PRIMARY KEY,"
    "  text TEXT NOT NULL,"
    "  source TEXT,"
    "  created INTEGER NOT NULL,"
    "  status TEXT DEFAULT 'pending'"
    ");";

YukiMemoryDB::YukiMemoryDB() = default;

YukiMemoryDB::~YukiMemoryDB()
{
    Close();
}

bool YukiMemoryDB::Open(const String& dbPath)
{
    if (db_)
        Close();

    int rc = sqlite3_open(dbPath.CString(), &db_);
    if (rc != SQLITE_OK)
    {
        URHO3D_LOGERROR("YukiMemoryDB: Failed to open " + dbPath + ": " + String(sqlite3_errmsg(db_)));
        sqlite3_close(db_);
        db_ = nullptr;
        return false;
    }

    sqlite3_exec(db_, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);
    sqlite3_exec(db_, "PRAGMA synchronous=NORMAL;", nullptr, nullptr, nullptr);

    char* errMsg = nullptr;
    rc = sqlite3_exec(db_, SCHEMA, nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK)
    {
        String err = errMsg ? String(errMsg) : "unknown";
        sqlite3_free(errMsg);
        URHO3D_LOGERROR("YukiMemoryDB: Schema error: " + err);
    }

    // Migration: add status column if missing (pre-0.7.3 databases)
    sqlite3_exec(db_, "ALTER TABLE memory ADD COLUMN status TEXT DEFAULT 'pending';",
                 nullptr, nullptr, nullptr);
    // Errors here are expected (column already exists) — silently ignored

    URHO3D_LOGINFOF("YukiMemoryDB: Opened %s (%d rows, %d pending)",
                    dbPath.CString(), Count(), CountPending());
    return true;
}

void YukiMemoryDB::Close()
{
    if (db_)
    {
        sqlite3_close(db_);
        db_ = nullptr;
    }
}

bool YukiMemoryDB::Checkpoint(bool truncate)
{
    if (!db_)
        return false;

    int nLog = 0, nCkpt = 0;
    const int mode = truncate ? SQLITE_CHECKPOINT_TRUNCATE : SQLITE_CHECKPOINT_PASSIVE;
    const int rc = sqlite3_wal_checkpoint_v2(db_, nullptr, mode, &nLog, &nCkpt);

    // nLog  = frames left in the WAL after the checkpoint (-1 if not in WAL mode)
    // nCkpt = frames moved back into the main database file
    if (rc == SQLITE_OK)
    {
        URHO3D_LOGDEBUGF("YukiMemoryDB: WAL checkpoint(%s) ok — wal=%d frames, flushed=%d",
                         truncate ? "TRUNCATE" : "PASSIVE", nLog, nCkpt);
        return true;
    }

    if (rc == SQLITE_BUSY)
    {
        // A reader was mid-read on a stale snapshot, so the truncate deferred —
        // committed frames were still flushed passively. Expected state, not a fault.
        URHO3D_LOGDEBUGF("YukiMemoryDB: WAL checkpoint(%s) busy — wal=%d frames, flushed=%d (truncate deferred)",
                         truncate ? "TRUNCATE" : "PASSIVE", nLog, nCkpt);
        return false;
    }

    URHO3D_LOGWARNING("YukiMemoryDB: WAL checkpoint failed: " + String(sqlite3_errmsg(db_)));
    return false;
}

bool YukiMemoryDB::Remember(const String& text, const String& source)
{
    if (!db_ || text.Trimmed().Empty())
        return false;

    // Dedup — skip if identical text already exists
    sqlite3_stmt* check = nullptr;
    int rc = sqlite3_prepare_v2(db_,
        "SELECT COUNT(*) FROM memory WHERE text = ?;",
        -1, &check, nullptr);
    if (rc == SQLITE_OK)
    {
        sqlite3_bind_text(check, 1, text.CString(), text.Length(), SQLITE_TRANSIENT);
        if (sqlite3_step(check) == SQLITE_ROW && sqlite3_column_int(check, 0) > 0)
        {
            sqlite3_finalize(check);
            return true;  // Already exists
        }
        sqlite3_finalize(check);
    }

    sqlite3_stmt* stmt = nullptr;
    rc = sqlite3_prepare_v2(db_,
        "INSERT INTO memory (text, source, created, status) VALUES (?, ?, ?, 'pending');",
        -1, &stmt, nullptr);
    if (rc != SQLITE_OK)
        return false;

    time_t now = time(nullptr);
    sqlite3_bind_text(stmt, 1, text.CString(), text.Length(), SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, source.CString(), source.Length(), SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 3, (int)now);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE)
    {
        URHO3D_LOGERROR("YukiMemoryDB: INSERT failed");
        return false;
    }

    return true;
}

Vector<YukiMemoryRow> YukiMemoryDB::GetAll()
{
    Vector<YukiMemoryRow> rows;
    if (!db_)
        return rows;

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_,
        "SELECT id, text, source, created, COALESCE(status,'pending') FROM memory ORDER BY created;",
        -1, &stmt, nullptr);
    if (rc != SQLITE_OK)
        return rows;

    while (sqlite3_step(stmt) == SQLITE_ROW)
    {
        YukiMemoryRow row;
        row.id = sqlite3_column_int(stmt, 0);
        row.text = (const char*)sqlite3_column_text(stmt, 1);
        row.source = (const char*)sqlite3_column_text(stmt, 2);
        row.created = (unsigned)sqlite3_column_int(stmt, 3);
        row.status = (const char*)sqlite3_column_text(stmt, 4);
        rows.Push(row);
    }

    sqlite3_finalize(stmt);
    return rows;
}

Vector<YukiMemoryRow> YukiMemoryDB::GetPending()
{
    Vector<YukiMemoryRow> rows;
    if (!db_)
        return rows;

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_,
        "SELECT id, text, source, created, status FROM memory "
        "WHERE status = 'pending' ORDER BY created;",
        -1, &stmt, nullptr);
    if (rc != SQLITE_OK)
        return rows;

    while (sqlite3_step(stmt) == SQLITE_ROW)
    {
        YukiMemoryRow row;
        row.id = sqlite3_column_int(stmt, 0);
        row.text = (const char*)sqlite3_column_text(stmt, 1);
        row.source = (const char*)sqlite3_column_text(stmt, 2);
        row.created = (unsigned)sqlite3_column_int(stmt, 3);
        row.status = (const char*)sqlite3_column_text(stmt, 4);
        rows.Push(row);
    }

    sqlite3_finalize(stmt);
    return rows;
}

Vector<YukiMemoryRow> YukiMemoryDB::GetActive()
{
    Vector<YukiMemoryRow> rows;
    if (!db_)
        return rows;

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_,
        "SELECT id, text, source, created, COALESCE(status,'pending') FROM memory "
        "WHERE COALESCE(status,'pending') IN ('pending','failed','training') ORDER BY created;",
        -1, &stmt, nullptr);
    if (rc != SQLITE_OK)
        return rows;

    while (sqlite3_step(stmt) == SQLITE_ROW)
    {
        YukiMemoryRow row;
        row.id = sqlite3_column_int(stmt, 0);
        row.text = (const char*)sqlite3_column_text(stmt, 1);
        row.source = (const char*)sqlite3_column_text(stmt, 2);
        row.created = (unsigned)sqlite3_column_int(stmt, 3);
        row.status = (const char*)sqlite3_column_text(stmt, 4);
        rows.Push(row);
    }

    sqlite3_finalize(stmt);
    return rows;
}

String YukiMemoryDB::GetAllText()
{
    String result;
    if (!db_)
        return result;

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_,
        "SELECT text FROM memory ORDER BY created;",
        -1, &stmt, nullptr);
    if (rc != SQLITE_OK)
        return result;

    while (sqlite3_step(stmt) == SQLITE_ROW)
    {
        if (!result.Empty())
            result += "\n";
        result += (const char*)sqlite3_column_text(stmt, 0);
    }

    sqlite3_finalize(stmt);
    return result;
}

void YukiMemoryDB::SetStatus(const Vector<int>& ids, const char* status)
{
    if (!db_ || ids.Empty())
        return;

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_,
        "UPDATE memory SET status = ? WHERE id = ?;",
        -1, &stmt, nullptr);
    if (rc != SQLITE_OK)
        return;

    for (unsigned i = 0; i < ids.Size(); ++i)
    {
        sqlite3_reset(stmt);
        sqlite3_bind_text(stmt, 1, status, -1, SQLITE_STATIC);
        sqlite3_bind_int(stmt, 2, ids[i]);
        sqlite3_step(stmt);
    }

    sqlite3_finalize(stmt);
}

void YukiMemoryDB::MarkTraining(const Vector<int>& ids)
{
    SetStatus(ids, "training");
}

void YukiMemoryDB::MarkConsumed(const Vector<int>& ids)
{
    SetStatus(ids, "consumed");
}

void YukiMemoryDB::MarkFailed(const Vector<int>& ids)
{
    SetStatus(ids, "failed");
}

void YukiMemoryDB::RecoverStaleTraining()
{
    if (!db_)
        return;

    int changed = 0;
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_,
        "UPDATE memory SET status = 'pending' WHERE status = 'training';",
        -1, &stmt, nullptr);
    if (rc == SQLITE_OK)
    {
        sqlite3_step(stmt);
        changed = sqlite3_changes(db_);
        sqlite3_finalize(stmt);
    }

    if (changed > 0)
        URHO3D_LOGWARNINGF("YukiMemoryDB: Recovered %d stale training rows to pending", changed);
}

void YukiMemoryDB::PruneConsumed(unsigned maxAgeDays)
{
    if (!db_)
        return;

    time_t cutoff = time(nullptr) - (time_t)(maxAgeDays * 86400);

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_,
        "DELETE FROM memory WHERE status = 'consumed' AND created < ?;",
        -1, &stmt, nullptr);
    if (rc == SQLITE_OK)
    {
        sqlite3_bind_int(stmt, 1, (int)cutoff);
        sqlite3_step(stmt);
        int pruned = sqlite3_changes(db_);
        sqlite3_finalize(stmt);

        if (pruned > 0)
            URHO3D_LOGINFOF("YukiMemoryDB: Pruned %d consumed rows older than %u days", pruned, maxAgeDays);
    }
}

void YukiMemoryDB::DeleteConsumed(const Vector<int>& ids)
{
    if (!db_ || ids.Empty())
        return;

    for (unsigned i = 0; i < ids.Size(); ++i)
    {
        char sql[64];
        snprintf(sql, sizeof(sql), "DELETE FROM memory WHERE id = %d;", ids[i]);
        sqlite3_exec(db_, sql, nullptr, nullptr, nullptr);
    }
}

void YukiMemoryDB::DeleteAll()
{
    if (!db_)
        return;
    sqlite3_exec(db_, "DELETE FROM memory;", nullptr, nullptr, nullptr);
}

int YukiMemoryDB::Count()
{
    if (!db_)
        return 0;

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, "SELECT COUNT(*) FROM memory;", -1, &stmt, nullptr);
    if (rc != SQLITE_OK)
        return 0;

    int count = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW)
        count = sqlite3_column_int(stmt, 0);

    sqlite3_finalize(stmt);
    return count;
}

int YukiMemoryDB::CountPending()
{
    if (!db_)
        return 0;

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_,
        "SELECT COUNT(*) FROM memory WHERE COALESCE(status,'pending') = 'pending';",
        -1, &stmt, nullptr);
    if (rc != SQLITE_OK)
        return 0;

    int count = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW)
        count = sqlite3_column_int(stmt, 0);

    sqlite3_finalize(stmt);
    return count;
}
