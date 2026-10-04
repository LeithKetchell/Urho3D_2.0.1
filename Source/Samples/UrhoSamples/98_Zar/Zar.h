// Zar — ZAR Archive Command-Line Tool
// Copyright (c) 2026 Urho3D project. License: MIT.
//
// WHAT:  Headless CLI tool for creating, listing, extracting, and appending
//        ZAR archives. Exercises the ZarWriter/ZarReader API.
//
// USAGE:
//        Zar create <archive.zar> <file1> [file2] [dir/] ...
//        Zar list <archive.zar>
//        Zar extract <archive.zar> [dest/]
//        Zar add <archive.zar> <file1> [file2] ...
//
// CREATE: Recursively adds files and directories. Reports original size,
//         archive size, and compression ratio.
// LIST:   Shows per-entry method, compressed/original sizes, ratio, path.
// EXTRACT: Recreates directory structure under destination. CRC verified.
// ADD:    Reads existing archive, appends new files, writes new archive.
//         Not in-place — creates temp file and replaces original.
//
// DEPS:  ZarArchive (engine IO), FileSystem, File. Headless (EP_HEADLESS).

#pragma once

#include <Urho3D/Engine/Application.h>

using namespace Urho3D;

class Zar : public Application
{
    URHO3D_OBJECT(Zar, Application);

public:
    explicit Zar(Context* context);

    void Setup() override;
    void Start() override;

private:
    void DoCreate(const Vector<String>& args);
    void DoList(const String& archivePath);
    void DoExtract(const String& archivePath, const String& destDir);
    void DoAdd(const String& archivePath, const Vector<String>& files);
    void PrintUsage();
};
