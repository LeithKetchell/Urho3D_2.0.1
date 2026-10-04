// ZAR archive tool — create, list, extract, append.
// Copyright (c) 2026 Urho3D project. License: MIT.

#include <Urho3D/Core/ProcessUtils.h>
#include <Urho3D/Engine/EngineDefs.h>
#include <Urho3D/IO/File.h>
#include <Urho3D/IO/FileSystem.h>
#include <Urho3D/IO/Log.h>
#include <Urho3D/IO/ZarArchive.h>

#include "Zar.h"

URHO3D_DEFINE_APPLICATION_MAIN(Zar)

Zar::Zar(Context* context) : Application(context) {}

void Zar::Setup()
{
    engineParameters_[EP_HEADLESS] = true;
    engineParameters_[EP_LOG_NAME] = "";
    engineParameters_[EP_LOG_QUIET] = true;
    engineParameters_[EP_SOUND] = false;
}

void Zar::Start()
{
    const Vector<String>& args = GetArguments();

    if (args.Size() < 2)
    {
        PrintUsage();
        engine_->Exit();
        return;
    }

    String command = args[0].ToLower();

    if (command == "create" && args.Size() >= 3)
    {
        Vector<String> files;
        for (unsigned i = 2; i < args.Size(); ++i)
            files.Push(args[i]);
        DoCreate(args);
    }
    else if (command == "list" && args.Size() >= 2)
    {
        DoList(args[1]);
    }
    else if (command == "extract" && args.Size() >= 2)
    {
        String dest = args.Size() >= 3 ? args[2] : ".";
        DoExtract(args[1], dest);
    }
    else if (command == "add" && args.Size() >= 3)
    {
        Vector<String> files;
        for (unsigned i = 2; i < args.Size(); ++i)
            files.Push(args[i]);
        DoAdd(args[1], files);
    }
    else
    {
        PrintUsage();
    }

    engine_->Exit();
}

void Zar::DoCreate(const Vector<String>& args)
{
    String archivePath = args[1];
    auto* fs = GetSubsystem<FileSystem>();

    ZarWriter writer(context_);
    if (!writer.Open(archivePath))
    {
        PrintLine("Error: Failed to create " + archivePath);
        return;
    }

    unsigned count = 0;
    unsigned long long totalOrig = 0;
    unsigned long long totalComp = 0;

    for (unsigned i = 2; i < args.Size(); ++i)
    {
        String path = args[i];

        if (fs->DirExists(path))
        {
            // Recurse directory
            Vector<String> files;
            fs->ScanDir(files, path, "*", SCAN_FILES, true);
            String dirName = GetFileNameAndExtension(RemoveTrailingSlash(path));

            for (const String& file : files)
            {
                if (file.StartsWith("."))
                    continue;

                String fullPath = AddTrailingSlash(path) + file;
                String entryPath = dirName + "/" + file;

                File srcFile(context_, fullPath, FILE_READ);
                if (!srcFile.IsOpen() || srcFile.GetSize() == 0)
                    continue;

                unsigned origSize = srcFile.GetSize();
                writer.AddFile(entryPath, srcFile, ZAR_ANS);
                totalOrig += origSize;
                count++;
            }
        }
        else if (fs->FileExists(path))
        {
            String entryPath = GetFileNameAndExtension(path);
            File srcFile(context_, path, FILE_READ);
            if (!srcFile.IsOpen())
            {
                PrintLine("Warning: Cannot open " + path);
                continue;
            }

            unsigned origSize = srcFile.GetSize();
            writer.AddFile(entryPath, srcFile, ZAR_ANS);
            totalOrig += origSize;
            count++;
        }
        else
        {
            PrintLine("Warning: Not found: " + path);
        }
    }

    if (!writer.Close())
    {
        PrintLine("Error: Failed to finalize archive");
        return;
    }

    // Report
    File checkFile(context_, archivePath, FILE_READ);
    unsigned long long archiveSize = checkFile.IsOpen() ? checkFile.GetSize() : 0;

    PrintLine("Created " + archivePath);
    PrintLine("  " + String(count) + " entries");
    PrintLine("  Original: " + String((unsigned)(totalOrig / 1024)) + " KB");
    PrintLine("  Archive:  " + String((unsigned)(archiveSize / 1024)) + " KB");
    if (totalOrig > 0)
        PrintLine("  Ratio:    " + String((int)(archiveSize * 100 / totalOrig)) + "%");
}

void Zar::DoList(const String& archivePath)
{
    ZarReader reader(context_);
    if (!reader.Open(archivePath))
    {
        PrintLine("Error: Failed to open " + archivePath);
        return;
    }

    PrintLine("Archive: " + archivePath + " (" + String(reader.GetEntryCount()) + " entries)");
    PrintLine("");
    PrintLine("  Method  Compressed    Original  Ratio  Name");
    PrintLine("  ------  ----------  ----------  -----  ----");

    Vector<String> files = reader.GetFileList();
    unsigned long long totalOrig = 0;
    unsigned long long totalComp = 0;

    for (const String& path : files)
    {
        const ZarEntry* e = reader.GetEntry(path);
        if (!e)
            continue;

        String method = (e->method == ZAR_ANS) ? "ANS" : "STORE";
        unsigned ratio = e->origSize > 0 ? (unsigned)(e->compSize * 100 / e->origSize) : 0;

        PrintLine("  " + method.Substring(0, 6) +
                  String((unsigned)(e->compSize)).Substring(0, 12).Trimmed() +
                  "  " + String((unsigned)e->compSize) +
                  "  " + String((unsigned)e->origSize) +
                  "  " + String(ratio) + "%" +
                  "  " + e->path);

        totalOrig += e->origSize;
        totalComp += e->compSize;
    }

    PrintLine("");
    unsigned totalRatio = totalOrig > 0 ? (unsigned)(totalComp * 100 / totalOrig) : 0;
    PrintLine("  Total: " + String((unsigned)totalComp) + " / " +
              String((unsigned)totalOrig) + " (" + String(totalRatio) + "%)");

    reader.Close();
}

void Zar::DoExtract(const String& archivePath, const String& destDir)
{
    ZarReader reader(context_);
    if (!reader.Open(archivePath))
    {
        PrintLine("Error: Failed to open " + archivePath);
        return;
    }

    auto* fs = GetSubsystem<FileSystem>();
    String dest = AddTrailingSlash(destDir);

    Vector<String> files = reader.GetFileList();
    unsigned count = 0;

    for (const String& entryPath : files)
    {
        const ZarEntry* e = reader.GetEntry(entryPath);
        if (!e)
            continue;

        String fullPath = dest + entryPath;
        String dir = GetPath(fullPath);

        if (!fs->DirExists(dir))
            fs->CreateDir(dir);

        // Read into buffer
        auto* buf = new unsigned char[(unsigned)e->origSize];
        unsigned long long read = reader.ReadFile(entryPath, buf, e->origSize);

        if (read > 0)
        {
            File outFile(context_, fullPath, FILE_WRITE);
            if (outFile.IsOpen())
            {
                outFile.Write(buf, (unsigned)read);
                count++;
                PrintLine("  " + entryPath + " (" + String((unsigned)read) + " bytes)");
            }
            else
            {
                PrintLine("  Error: Cannot write " + fullPath);
            }
        }
        else
        {
            PrintLine("  Error: Failed to read " + entryPath);
        }

        delete[] buf;
    }

    reader.Close();
    PrintLine("Extracted " + String(count) + " files to " + dest);
}

void Zar::DoAdd(const String& archivePath, const Vector<String>& files)
{
    // Read existing archive
    ZarReader reader(context_);
    if (!reader.Open(archivePath))
    {
        PrintLine("Error: Failed to open " + archivePath);
        return;
    }

    // Create new archive with existing + new entries
    String tempPath = archivePath + ".new";
    ZarWriter writer(context_);
    if (!writer.Open(tempPath))
    {
        PrintLine("Error: Failed to create temp archive");
        reader.Close();
        return;
    }

    // Copy existing entries
    Vector<String> existing = reader.GetFileList();
    for (const String& path : existing)
    {
        VectorBuffer data = reader.ReadFileBuffer(path);
        if (data.GetSize() > 0)
        {
            const ZarEntry* e = reader.GetEntry(path);
            writer.AddFile(path, data.GetData(), data.GetSize(), ZAR_ANS,
                           e ? e->modTime : 0);
        }
    }
    reader.Close();

    // Add new files
    auto* fs = GetSubsystem<FileSystem>();
    unsigned added = 0;

    for (const String& path : files)
    {
        if (!fs->FileExists(path))
        {
            PrintLine("Warning: Not found: " + path);
            continue;
        }

        String entryPath = GetFileNameAndExtension(path);
        File srcFile(context_, path, FILE_READ);
        if (!srcFile.IsOpen())
            continue;

        writer.AddFile(entryPath, srcFile, ZAR_ANS);
        added++;
        PrintLine("  Added: " + entryPath);
    }

    writer.Close();

    // Replace original with new
    fs->Delete(archivePath);
    fs->Rename(tempPath, archivePath);

    PrintLine("Added " + String(added) + " files to " + archivePath);
}

void Zar::PrintUsage()
{
    PrintLine("ZAR Archive Tool — rANS entropy coding");
    PrintLine("");
    PrintLine("Usage:");
    PrintLine("  Zar create <archive.zar> <file1> [file2] [dir/] ...");
    PrintLine("  Zar list <archive.zar>");
    PrintLine("  Zar extract <archive.zar> [dest/]");
    PrintLine("  Zar add <archive.zar> <file1> [file2] ...");
}
