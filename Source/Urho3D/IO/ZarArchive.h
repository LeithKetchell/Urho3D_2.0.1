// ZAR Archive Format — Urho3D 2.0.1
// Copyright (c) 2026 Urho3D project. License: MIT.
//
// WHAT:  Random-access archive format with per-entry compression.
//        Directory at front for HTTP/network-friendly access — read the directory,
//        seek to any entry, serve it compressed. No full archive download needed.
//
// FORMAT:
//        [ZDIR central directory]  — at front (offset 0)
//        [ZAR entry 1]            — magic + header + compressed data
//        [ZAR entry 2]            — ...
//        [ZEND footer]            — last 24 bytes: magic + dirOffset + dirSize + version + sentinel
//
//        Entry magic: ZAR\x01 (forward-scannable for recovery if directory lost)
//        Dir magic:   ZDIR
//        Footer magic: ZEND + "ZR" sentinel
//
// COMPRESSION:
//        ZAR_STORE (0) — raw, no compression.
//        ZAR_ANS (1)   — RLE + rANS entropy coding. No Lempel-Ziv, no Huffman tree.
//                         Pipeline: data → RLE (collapse runs) → rANS (entropy code).
//                         512-byte frequency table stored per entry.
//                         Decompression is a table lookup per byte — fast.
//
// INTEGRITY: CRC32 per entry, verified on read.
//
// REMOTE ACCESS:
//        Read directory (first few KB) → get offsets + compressed sizes for all entries.
//        Seek to entry offset, read compSize bytes → still compressed, ready to serve.
//        Receiver decompresses. The archive IS the CDN.
//
// API:   ZarWriter::Open/AddFile/Close — create archives.
//        ZarReader::Open/GetFileList/ReadFile/ReadFileBuffer — read archives.
//
// DEPS:  File, FileSystem, VectorBuffer. No external compression libraries.

#pragma once

#include "../Core/Object.h"
#include "../Container/HashMap.h"
#include "../Container/Str.h"
#include "../Container/Vector.h"
#include "../IO/VectorBuffer.h"

namespace Urho3D
{

class File;

/// Compression methods for ZAR entries.
enum ZarMethod : unsigned char
{
    ZAR_STORE = 0,
    ZAR_ANS = 1       ///< rANS entropy coding — no dictionary, fast decode.
};

/// Directory entry for a file in a ZAR archive.
struct ZarEntry
{
    String path;
    unsigned long long offset{};       // offset to file entry from archive start
    unsigned long long origSize{};
    unsigned long long compSize{};
    unsigned crc32{};
    ZarMethod method{ZAR_STORE};
    unsigned long long modTime{};
};

/// Archive writer. Entries buffered to temp file, directory written at front on Close().
class URHO3D_API ZarWriter : public Object
{
    URHO3D_OBJECT(ZarWriter, Object);

public:
    explicit ZarWriter(Context* context);
    ~ZarWriter() override;

    /// Open archive file for writing. Returns true on success.
    bool Open(const String& path);
    /// Add a file from a memory buffer.
    bool AddFile(const String& entryPath, const void* data, unsigned long long size,
                 ZarMethod method = ZAR_ANS, unsigned long long modTime = 0);
    /// Add a file from an Urho3D File object (reads from current position to end).
    bool AddFile(const String& entryPath, File& srcFile, ZarMethod method = ZAR_ANS);
    /// Finalize: write directory + entries + footer to output file. Must be called before destruction.
    bool Close();

private:
    SharedPtr<File> tempFile_;      // entry data accumulates here
    String outputPath_;             // final archive path
    String tempPath_;               // temp file path
    Vector<ZarEntry> entries_;
    bool open_{false};
};

/// Random-access archive reader. Reads footer + directory on open, then seeks to entries.
class URHO3D_API ZarReader : public Object
{
    URHO3D_OBJECT(ZarReader, Object);

public:
    explicit ZarReader(Context* context);
    ~ZarReader() override;

    /// Open archive and read central directory. Returns true on success.
    bool Open(const String& path);
    /// Close archive.
    void Close();
    /// Return true if archive is open.
    bool IsOpen() const { return open_; }

    /// Get list of all file paths in the archive.
    Vector<String> GetFileList() const;
    /// Check if a file exists in the archive.
    bool HasFile(const String& entryPath) const;
    /// Get original (uncompressed) size of a file.
    unsigned long long GetFileSize(const String& entryPath) const;
    /// Get entry info.
    const ZarEntry* GetEntry(const String& entryPath) const;
    /// Get number of entries.
    unsigned GetEntryCount() const { return entries_.Size(); }

    /// Read a file into a caller-provided buffer. Returns bytes read, or 0 on failure.
    unsigned long long ReadFile(const String& entryPath, void* dest, unsigned long long destSize);
    /// Read a file into a VectorBuffer and return it.
    VectorBuffer ReadFileBuffer(const String& entryPath);

private:
    SharedPtr<File> file_;
    Vector<ZarEntry> entries_;
    HashMap<String, unsigned> entryIndex_;  // path -> index into entries_
    bool open_{false};
};

}
