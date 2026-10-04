//
// Copyright (c) 2008-2024 the Urho3D project.
// License: MIT
//
// Yuki expert federation — P2: multi-cart registry + loader.
//

#include "../Precompiled.h"

#include "YukiCartRegistry.h"
#include "../IO/Log.h"
#include "../IO/File.h"
#include "../Container/Sort.h"

namespace Urho3D
{

YukiCartRegistry::YukiCartRegistry(Context* context) :
    Object(context)
{
}

YukiCartRegistry::~YukiCartRegistry()
{
    Clear();
}

SharedPtr<YukiModel> YukiCartRegistry::Load(const String& id, const String& path)
{
    if (id.Empty() || path.Empty())
    {
        URHO3D_LOGERROR("YukiCartRegistry::Load: empty id or path");
        return SharedPtr<YukiModel>();
    }

    // Keyed by the FULL String id (not a 32-bit StringHash) — two distinct ids can never collide into one
    // entry. NOTE: mutex_ is non-recursive; this method must not call another locked method.
    MutexLock lock(mutex_);   // whole Load is atomic vs concurrent Get/Unload/Clear (idempotent check + insert)

    // Idempotent: an expert already loaded under this id is returned as-is (as a SharedPtr copy) — never
    // load twice (that would orphan the prior YukiModel and its buffers).
    HashMap<String, SharedPtr<YukiModel> >::ConstIterator it = carts_.Find(id);
    if (it != carts_.End() && it->second_ && it->second_->IsLoaded())
        return it->second_;

    SharedPtr<YukiModel> cart(new YukiModel(context_));
    if (!cart->Load(path))
    {
        URHO3D_LOGERROR("YukiCartRegistry::Load: failed to load expert cart '" + id + "' from " + path);
        return SharedPtr<YukiModel>();   // cart drops here -> freed; nothing registered on failure
    }

    carts_[id] = cart;
    manifest_[id] = path;   // a successfully loaded expert is now KNOWN — catalog it for load-on-demand
    URHO3D_LOGINFO("YukiCartRegistry: loaded expert cart '" + id + "' from " + path +
        " (" + String(carts_.Size()) + " expert(s) resident)");
    return cart;
}

unsigned YukiCartRegistry::LoadManifest(const String& manifestPath)
{
    if (manifestPath.Empty())
    {
        URHO3D_LOGERROR("YukiCartRegistry::LoadManifest: empty manifest path");
        return 0;
    }

    // Engine File I/O (project rule: no raw fopen/ifstream).
    File file(context_);
    if (!file.Open(manifestPath, FILE_READ))
    {
        URHO3D_LOGERROR("YukiCartRegistry::LoadManifest: cannot open manifest " + manifestPath);
        return 0;
    }

    // Parse line-by-line OUTSIDE the registry lock: each Load() below takes mutex_ itself, and Urho's Mutex
    // is non-recursive, so holding it here would self-deadlock. One `id | path` per line; blanks and '#'
    // comments skipped; malformed lines logged and skipped (failure-isolated — one bad line never aborts).
    unsigned loaded = 0;
    while (!file.IsEof())
    {
        const String line = file.ReadLine().Trimmed();
        if (line.Empty() || line.StartsWith("#"))
            continue;
        Vector<String> parts = line.Split('|');
        if (parts.Size() < 2)
        {
            URHO3D_LOGWARNING("YukiCartRegistry::LoadManifest: skipping malformed line '" + line + "'");
            continue;
        }
        const String eid = parts[0].Trimmed();
        const String epath = parts[1].Trimmed();
        Register(eid, epath);              // catalog the DECLARED expert (locks mutex_) — even if the load
                                           // below fails, the manifest faithfully reflects the file's entries
        if (Load(eid, epath))              // attempt to make resident; idempotent + failure-isolated
            ++loaded;
    }
    file.Close();

    URHO3D_LOGINFO("YukiCartRegistry: manifest '" + manifestPath + "' loaded " + String(loaded) +
        " expert(s) (" + String(Count()) + " resident)");
    return loaded;
}

void YukiCartRegistry::Unload(const String& id)
{
    MutexLock lock(mutex_);
    // Erase drops the registry's SharedPtr. The YukiModel is destroyed only when the LAST SharedPtr drops,
    // so a caller still holding a Get()/Load() copy across its forward pass never dangles.
    if (carts_.Erase(id))
    {
        URHO3D_LOGINFO("YukiCartRegistry: unloaded expert cart '" + id + "' (" +
            String(carts_.Size()) + " expert(s) resident)");
    }
}

SharedPtr<YukiModel> YukiCartRegistry::Get(const String& id) const
{
    MutexLock lock(mutex_);
    HashMap<String, SharedPtr<YukiModel> >::ConstIterator it = carts_.Find(id);
    return (it != carts_.End()) ? it->second_ : SharedPtr<YukiModel>();   // SharedPtr COPY, never raw
}

bool YukiCartRegistry::IsLoaded(const String& id) const
{
    MutexLock lock(mutex_);   // own locked lookup (does NOT call Get) — no nested lock
    HashMap<String, SharedPtr<YukiModel> >::ConstIterator it = carts_.Find(id);
    return it != carts_.End() && it->second_ && it->second_->IsLoaded();
}

unsigned YukiCartRegistry::Count() const
{
    MutexLock lock(mutex_);
    return carts_.Size();
}

Vector<String> YukiCartRegistry::GetLoadedIds() const
{
    MutexLock lock(mutex_);
    Vector<String> out;
    out.Reserve(carts_.Size());
    // The map key IS the id now, so iterate carts_ directly — no parallel id map to drift out of sync.
    for (HashMap<String, SharedPtr<YukiModel> >::ConstIterator it = carts_.Begin(); it != carts_.End(); ++it)
        out.Push(it->first_);
    return out;
}

void YukiCartRegistry::Clear()
{
    MutexLock lock(mutex_);
    // Dropping every registry SharedPtr frees each expert YukiModel + its buffers (subject to any handle a
    // caller still holds). The core cart is not here, so this never affects it. The manifest catalog is
    // deliberately NOT cleared — freeing memory does not un-exist the experts on disk; keep them discoverable.
    carts_.Clear();
}

// --- Manifest catalog (id -> .cart path): which experts EXIST, decoupled from what is resident. ---

bool YukiCartRegistry::Register(const String& id, const String& path)
{
    if (id.Empty() || path.Empty())
    {
        URHO3D_LOGERROR("YukiCartRegistry::Register: empty id or path");
        return false;
    }
    MutexLock lock(mutex_);
    manifest_[id] = path;   // catalog only — no load, no memory cost until LoadFromManifest()
    return true;
}

String YukiCartRegistry::GetManifestPath(const String& id) const
{
    MutexLock lock(mutex_);
    HashMap<String, String>::ConstIterator it = manifest_.Find(id);
    return (it != manifest_.End()) ? it->second_ : String::EMPTY;
}

Vector<String> YukiCartRegistry::GetManifestIds() const
{
    MutexLock lock(mutex_);
    Vector<String> out;
    out.Reserve(manifest_.Size());
    for (HashMap<String, String>::ConstIterator it = manifest_.Begin(); it != manifest_.End(); ++it)
        out.Push(it->first_);
    return out;
}

unsigned YukiCartRegistry::ManifestCount() const
{
    MutexLock lock(mutex_);
    return manifest_.Size();
}

SharedPtr<YukiModel> YukiCartRegistry::LoadFromManifest(const String& id)
{
    // Resolve the cataloged path under the lock, then release BEFORE Load() (mutex_ is non-recursive and
    // Load() locks itself). Two sequential lock acquisitions, never nested.
    String path;
    {
        MutexLock lock(mutex_);
        HashMap<String, String>::ConstIterator it = manifest_.Find(id);
        if (it == manifest_.End())
        {
            URHO3D_LOGWARNING("YukiCartRegistry::LoadFromManifest: id '" + id + "' not in the catalog");
            return SharedPtr<YukiModel>();
        }
        path = it->second_;
    }
    return Load(id, path);   // idempotent: already-resident returns the existing copy; locks mutex_ itself
}

bool YukiCartRegistry::SaveManifest(const String& manifestPath)
{
    if (manifestPath.Empty())
    {
        URHO3D_LOGERROR("YukiCartRegistry::SaveManifest: empty manifest path");
        return false;
    }

    // Snapshot the catalog UNDER the lock into (id | path) lines, then write OUTSIDE the lock — no file I/O
    // while holding the non-recursive mutex_. Sorted by id so the manifest file is stable/diffable.
    Vector<String> lines;
    {
        MutexLock lock(mutex_);
        lines.Reserve(manifest_.Size());
        for (HashMap<String, String>::ConstIterator it = manifest_.Begin(); it != manifest_.End(); ++it)
            lines.Push(it->first_ + " | " + it->second_);
    }
    Sort(lines.Begin(), lines.End());

    File file(context_);
    if (!file.Open(manifestPath, FILE_WRITE))
    {
        URHO3D_LOGERROR("YukiCartRegistry::SaveManifest: cannot open " + manifestPath + " for writing");
        return false;
    }
    const String header = "# Yuki expert federation manifest — one 'id | path' per line. Round-trips with LoadManifest.\n";
    file.Write(header.CString(), header.Length());
    for (const String& line : lines)
    {
        const String out = line + "\n";
        file.Write(out.CString(), out.Length());
    }
    file.Close();

    URHO3D_LOGINFO("YukiCartRegistry::SaveManifest: wrote " + String(lines.Size()) + " expert(s) to " + manifestPath);
    return true;
}

}
