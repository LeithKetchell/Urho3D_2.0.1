// YukiMemoryDB — Short-term memory for Yuki (SQLite).
// Facts accumulate with status tracking: pending → training → consumed.
// Online path injects active facts into prompts. Offline path trains them
// into model weights. Consumed facts stay for audit; pruned after 30 days.

#pragma once

#include <Urho3D/Container/Str.h>
#include <Urho3D/Container/Vector.h>

struct sqlite3;

using namespace Urho3D;

/// A single remembered fact.
struct YukiMemoryRow
{
    int id{};
    String text;
    String source;
    String status;         // pending, training, consumed, failed
    unsigned created{};    // unix timestamp
};

class YukiMemoryDB
{
public:
    YukiMemoryDB();
    ~YukiMemoryDB();

    /// Open (or create) the database at dbPath. Returns true on success.
    bool Open(const String& dbPath);
    /// Close database.
    void Close();
    /// Return true if database is open.
    bool IsOpen() const { return db_ != nullptr; }

    /// Flush the write-ahead log into the main database file. When truncate is
    /// true also truncates the -wal to zero — that only wins when no reader is
    /// mid-read on a stale snapshot, otherwise it degrades to a passive flush
    /// (committed frames still land in the main file). Returns true if SQLite
    /// checkpointed cleanly; false on BUSY/error/closed. Safe no-op if not open.
    bool Checkpoint(bool truncate);

    /// Store a statement for future training.
    bool Remember(const String& text, const String& source);

    /// Get all rows (any status).
    Vector<YukiMemoryRow> GetAll();

    /// Get rows available for training (status = 'pending').
    Vector<YukiMemoryRow> GetPending();

    /// Get rows for prompt injection (status in 'pending', 'failed', 'training').
    Vector<YukiMemoryRow> GetActive();

    /// Get all text as a single block (for context injection).
    String GetAllText();

    /// Mark rows as 'training' (claimed by active training job).
    void MarkTraining(const Vector<int>& ids);

    /// Mark rows as 'consumed' (training succeeded).
    void MarkConsumed(const Vector<int>& ids);

    /// Mark rows as 'failed' (training failed, revert to prompt-injectable).
    void MarkFailed(const Vector<int>& ids);

    /// Reset any 'training' rows to 'pending' (crash recovery on boot).
    void RecoverStaleTraining();

    /// Delete consumed rows older than maxAgeDays (garbage collection).
    void PruneConsumed(unsigned maxAgeDays = 30);

    /// Delete consumed rows by ID.
    void DeleteConsumed(const Vector<int>& ids);

    /// Delete all rows.
    void DeleteAll();

    /// Total row count.
    int Count();

    /// Count of pending rows only.
    int CountPending();

private:
    /// Helper: update status for a list of IDs.
    void SetStatus(const Vector<int>& ids, const char* status);

    sqlite3* db_{nullptr};
};
