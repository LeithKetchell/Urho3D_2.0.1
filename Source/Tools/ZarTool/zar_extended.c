/*
 * zar — ZAR Archive Tool
 * Create, list, extract, and verify ZAR archives.
 *
 * ZAR format: sequential entries (like tar), tail-end directory (like zip).
 *   - Writes forward: entry headers + data streamed in order
 *   - Reads random: footer → directory → seek to any entry
 *   - Compression: LZ4 (per-entry, falls back to store if bigger)
 *   - Integrity: CRC-32 per entry
 *
 * Usage:
 *   zar c archive.zar file1 file2 dir/ ...   Create archive
 *   zar t archive.zar                         List contents
 *   zar x archive.zar [file ...]              Extract (all or named)
 *   zar v archive.zar                         Verify CRC integrity
 *   zar cat archive.zar file                  Print file to stdout
 *   zar stat archive.zar                      Archive statistics
 *
 * No Urho3D dependency. Portable C99 + LZ4.
 *
 * Copyright (c) 2026 Leith Bade. MIT License.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/stat.h>
#include <dirent.h>
#include <time.h>
#include <errno.h>

#include "lz4.h"

/* For HTTP server */
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <fcntl.h>
#include <ctype.h>

/* ── Format constants (must match ZarArchive.cpp) ── */

static const uint8_t ENTRY_MAGIC[4]  = {'Z', 'A', 'R', 0x01};
static const uint8_t DIR_MAGIC[4]    = {'Z', 'D', 'I', 'R'};
static const uint8_t FOOTER_MAGIC[4] = {'Z', 'E', 'N', 'D'};
static const uint8_t TAIL_SENTINEL[2] = {'Z', 'R'};
static const uint16_t FORMAT_VERSION = 1;
static const uint32_t FOOTER_SIZE    = 24;

enum { ZAR_STORE = 0, ZAR_LZ4 = 1 };

/* ── Directory entry ── */

typedef struct {
    char*    path;
    uint64_t offset;
    uint64_t origSize;
    uint64_t compSize;
    uint32_t crc32;
    uint8_t  method;
    uint64_t modTime;
} ZarEntry;

/* ── CRC-32 ── */

static uint32_t crc32_table[256];
static int crc32_ready = 0;

static void crc32_init(void)
{
    if (crc32_ready) return;
    for (uint32_t i = 0; i < 256; ++i) {
        uint32_t c = i;
        for (int j = 0; j < 8; ++j)
            c = (c >> 1) ^ (0xEDB88320u & (-(c & 1)));
        crc32_table[i] = c;
    }
    crc32_ready = 1;
}

static uint32_t crc32_calc(const void* data, uint64_t size)
{
    crc32_init();
    uint32_t crc = 0xFFFFFFFF;
    const uint8_t* p = (const uint8_t*)data;
    for (uint64_t i = 0; i < size; ++i)
        crc = crc32_table[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFF;
}

/* ── Little-endian I/O ── */

static int write_u8(FILE* f, uint8_t v) { return fwrite(&v, 1, 1, f) == 1; }
static int write_u16(FILE* f, uint16_t v) { uint8_t b[2]; b[0]=v&0xFF; b[1]=(v>>8)&0xFF; return fwrite(b,1,2,f)==2; }
static int write_u32(FILE* f, uint32_t v) { uint8_t b[4]; for(int i=0;i<4;i++){b[i]=v&0xFF;v>>=8;} return fwrite(b,1,4,f)==4; }
static int write_u64(FILE* f, uint64_t v) { uint8_t b[8]; for(int i=0;i<8;i++){b[i]=v&0xFF;v>>=8;} return fwrite(b,1,8,f)==8; }

static uint8_t  read_u8(FILE* f)  { uint8_t v; fread(&v,1,1,f); return v; }
static uint16_t read_u16(FILE* f) { uint8_t b[2]; fread(b,1,2,f); return (uint16_t)b[0]|((uint16_t)b[1]<<8); }
static uint32_t read_u32(FILE* f) { uint8_t b[4]; fread(b,1,4,f); return (uint32_t)b[0]|((uint32_t)b[1]<<8)|((uint32_t)b[2]<<16)|((uint32_t)b[3]<<24); }
static uint64_t read_u64(FILE* f) { uint8_t b[8]; fread(b,1,8,f); uint64_t v=0; for(int i=7;i>=0;i--) v=(v<<8)|b[i]; return v; }

/* ── Directory read ── */

static int read_directory(FILE* f, ZarEntry** out_entries, uint64_t* out_count)
{
    /* Seek to footer */
    fseeko(f, 0, SEEK_END);
    int64_t fileSize = ftello(f);
    if (fileSize < (int64_t)FOOTER_SIZE) {
        fprintf(stderr, "zar: file too small\n");
        return -1;
    }

    fseeko(f, fileSize - FOOTER_SIZE, SEEK_SET);

    uint8_t magic[4];
    fread(magic, 1, 4, f);
    if (memcmp(magic, FOOTER_MAGIC, 4) != 0) {
        fprintf(stderr, "zar: invalid footer magic\n");
        return -1;
    }

    uint64_t dirOffset = read_u64(f);
    uint64_t dirSize   = read_u64(f);
    uint16_t version   = read_u16(f);
    uint8_t sentinel[2];
    fread(sentinel, 1, 2, f);

    if (memcmp(sentinel, TAIL_SENTINEL, 2) != 0) {
        fprintf(stderr, "zar: invalid tail sentinel\n");
        return -1;
    }

    (void)dirSize;
    if (version > FORMAT_VERSION)
        fprintf(stderr, "zar: warning — archive version %u (we support %u)\n", version, FORMAT_VERSION);

    /* Read central directory */
    fseeko(f, dirOffset, SEEK_SET);

    uint8_t dirMagic[4];
    fread(dirMagic, 1, 4, f);
    if (memcmp(dirMagic, DIR_MAGIC, 4) != 0) {
        fprintf(stderr, "zar: invalid directory magic\n");
        return -1;
    }

    uint64_t count = read_u64(f);
    ZarEntry* entries = (ZarEntry*)calloc((size_t)count, sizeof(ZarEntry));
    if (!entries) {
        fprintf(stderr, "zar: out of memory (%llu entries)\n", (unsigned long long)count);
        return -1;
    }

    for (uint64_t i = 0; i < count; ++i) {
        uint16_t pathLen = read_u16(f);
        entries[i].path = (char*)malloc(pathLen + 1);
        fread(entries[i].path, 1, pathLen, f);
        entries[i].path[pathLen] = '\0';
        entries[i].offset   = read_u64(f);
        entries[i].origSize = read_u64(f);
        entries[i].compSize = read_u64(f);
        entries[i].crc32    = read_u32(f);
        entries[i].method   = read_u8(f);
        entries[i].modTime  = read_u64(f);
    }

    *out_entries = entries;
    *out_count = count;
    return 0;
}

static void free_entries(ZarEntry* entries, uint64_t count)
{
    for (uint64_t i = 0; i < count; ++i)
        free(entries[i].path);
    free(entries);
}

/* ── Read entry data ── */

static uint8_t* read_entry_data(FILE* f, const ZarEntry* e)
{
    /* Skip entry header to reach data */
    uint32_t headerSize = 4 + 2 + (uint32_t)strlen(e->path) + 2 + 1 + 8 + 8 + 4 + 8;
    fseeko(f, e->offset + headerSize, SEEK_SET);

    if (e->method == ZAR_STORE) {
        uint8_t* buf = (uint8_t*)malloc((size_t)e->origSize);
        if (!buf) return NULL;
        fread(buf, 1, (size_t)e->origSize, f);
        return buf;
    }

    /* LZ4 */
    uint8_t* comp = (uint8_t*)malloc((size_t)e->compSize);
    uint8_t* orig = (uint8_t*)malloc((size_t)e->origSize);
    if (!comp || !orig) { free(comp); free(orig); return NULL; }

    fread(comp, 1, (size_t)e->compSize, f);
    int r = LZ4_decompress_safe((const char*)comp, (char*)orig, (int)e->compSize, (int)e->origSize);
    free(comp);

    if (r < 0) {
        fprintf(stderr, "zar: LZ4 decompression failed for %s\n", e->path);
        free(orig);
        return NULL;
    }

    return orig;
}

/* Read compressed entry data (without decompression) */
static uint8_t* read_entry_compressed(FILE* f, const ZarEntry* e, uint64_t* out_size)
{
    uint32_t headerSize = 4 + 2 + (uint32_t)strlen(e->path) + 2 + 1 + 8 + 8 + 4 + 8;
    fseeko(f, e->offset + headerSize, SEEK_SET);

    uint8_t* buf = (uint8_t*)malloc((size_t)e->compSize);
    if (!buf) return NULL;
    
    size_t bytesRead = fread(buf, 1, (size_t)e->compSize, f);
    if (bytesRead != (size_t)e->compSize) {
        free(buf);
        return NULL;
    }
    
    *out_size = e->compSize;
    return buf;
}

/* Find entry by path */
static const ZarEntry* find_entry(const ZarEntry* entries, uint64_t count, const char* filepath)
{
    for (uint64_t i = 0; i < count; ++i) {
        if (strcmp(entries[i].path, filepath) == 0) {
            return &entries[i];
        }
    }
    return NULL;
}

/* ── Recursive file collection ── */

typedef struct {
    char** paths;
    uint64_t count;
    uint64_t cap;
} FileList;

static void filelist_push(FileList* fl, const char* path)
{
    if (fl->count >= fl->cap) {
        fl->cap = fl->cap ? fl->cap * 2 : 256;
        fl->paths = (char**)realloc(fl->paths, fl->cap * sizeof(char*));
    }
    fl->paths[fl->count++] = strdup(path);
}

static void collect_files(FileList* fl, const char* path)
{
    struct stat st;
    if (stat(path, &st) != 0) {
        fprintf(stderr, "zar: cannot stat %s: %s\n", path, strerror(errno));
        return;
    }

    if (S_ISREG(st.st_mode)) {
        filelist_push(fl, path);
        return;
    }

    if (S_ISDIR(st.st_mode)) {
        DIR* d = opendir(path);
        if (!d) { fprintf(stderr, "zar: cannot open dir %s\n", path); return; }

        struct dirent* ent;
        while ((ent = readdir(d)) != NULL) {
            if (ent->d_name[0] == '.' && (ent->d_name[1] == '\0' ||
                (ent->d_name[1] == '.' && ent->d_name[2] == '\0')))
                continue;

            char child[4096];
            snprintf(child, sizeof(child), "%s/%s", path, ent->d_name);
            collect_files(fl, child);
        }
        closedir(d);
    }
}

/* ── Make parent directories ── */

static void mkdirs(const char* path)
{
    char* tmp = strdup(path);
    for (char* p = tmp + 1; *p; ++p) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
    free(tmp);
}

/* ── Human-readable size ── */

static const char* human_size(uint64_t bytes)
{
    static char buf[32];
    if (bytes < 1024)
        snprintf(buf, sizeof(buf), "%llu B", (unsigned long long)bytes);
    else if (bytes < 1024*1024)
        snprintf(buf, sizeof(buf), "%.1f KB", bytes / 1024.0);
    else if (bytes < 1024ULL*1024*1024)
        snprintf(buf, sizeof(buf), "%.1f MB", bytes / (1024.0*1024.0));
    else
        snprintf(buf, sizeof(buf), "%.2f GB", bytes / (1024.0*1024.0*1024.0));
    return buf;
}

/* ── HTTP Server Helpers ── */

static int create_socket(uint16_t port) {
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("socket");
        return -1;
    }

    int opt = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(sockfd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(sockfd);
        return -1;
    }

    if (listen(sockfd, 10) < 0) {
        perror("listen");
        close(sockfd);
        return -1;
    }

    return sockfd;
}

static int read_line(int sockfd, char* buf, int size) {
    int i = 0;
    char c;
    while (i < size - 1 && read(sockfd, &c, 1) == 1) {
        if (c == '\n') break;
        if (c != '\r') buf[i++] = c;
    }
    buf[i] = '\0';
    return i;
}

static void url_decode(char* str) {
    for (int i = 0; str[i]; i++) {
        if (str[i] == '%') {
            if (isxdigit(str[i+1]) && isxdigit(str[i+2])) {
                char hex[3] = {str[i+1], str[i+2], '\0'};
                str[i] = (char)strtol(hex, NULL, 16);
                memmove(&str[i+1], &str[i+3], strlen(&str[i+3]) + 1);
            }
        } else if (str[i] == '+') {
            str[i] = ' ';
        }
    }
}

static void parse_query(const char* query, char* filepath, int filepath_size) {
    const char* file_param = strstr(query, "file=");
    if (file_param) {
        file_param += 5; // skip "file="
        const char* end = strchr(file_param, '&');
        int len = end ? (int)(end - file_param) : (int)strlen(file_param);
        strncpy(filepath, file_param, filepath_size - 1);
        filepath[filepath_size - 1] = '\0';
        url_decode(filepath);
    } else {
        filepath[0] = '\0';
    }
}

static void send_http_response(int client_sock, int status_code, const char* status_msg, 
                               const char* content_type, const void* data, size_t data_len) {
    char header[1024];
    snprintf(header, sizeof(header),
             "HTTP/1.1 %d %s\r\n"
             "Content-Type: %s\r\n"
             "Content-Length: %zu\r\n"
             "Connection: close\r\n"
             "\r\n",
             status_code, status_msg, content_type, data_len);
    
    write(client_sock, header, strlen(header));
    if (data && data_len > 0) {
        write(client_sock, data, data_len);
    }
}

static void handle_http_request(int client_sock, FILE* archive, ZarEntry* entries, uint64_t count) {
    char request[4096];
    char url[1024];
    char query[1024] = "";
    
    read_line(client_sock, request, sizeof(request));
    
    // Parse request line: "GET /path?query HTTP/1.1"
    char method[16];
    if (sscanf(request, "%15s %1023s", method, url) != 2) {
        send_http_response(client_sock, 400, "Bad Request", "text/plain", "Bad Request", 11);
        return;
    }
    
    // Split URL into path and query
    char* q = strchr(url, '?');
    char path[1024];
    if (q) {
        strncpy(path, url, q - url);
        path[q - url] = '\0';
        strncpy(query, q + 1, sizeof(query) - 1);
    } else {
        strncpy(path, url, sizeof(path) - 1);
        path[sizeof(path) - 1] = '\0';
    }
    
    // Handle different endpoints
    if (strcmp(path, "/list") == 0) {
        char* json = malloc(65536);
        if (!json) {
            send_http_response(client_sock, 500, "Internal Server Error", "text/plain", "Out of memory", 13);
            return;
        }
        
        char* p = json;
        p += sprintf(p, "[");
        for (uint64_t i = 0; i < count; i++) {
            if (i > 0) p += sprintf(p, ",");
            p += sprintf(p, "\"%s\"", entries[i].path);
        }
        p += sprintf(p, "]");
        
        send_http_response(client_sock, 200, "OK", "application/json", json, strlen(json));
        free(json);
        
    } else if (strcmp(path, "/hash") == 0) {
        char filepath[1024];
        parse_query(query, filepath, sizeof(filepath));
        
        const ZarEntry* entry = find_entry(entries, count, filepath);
        if (!entry) {
            send_http_response(client_sock, 404, "Not Found", "text/plain", "File not found", 13);
            return;
        }
        
        char hash_str[16];
        snprintf(hash_str, sizeof(hash_str), "%08x", entry->crc32);
        send_http_response(client_sock, 200, "OK", "text/plain", hash_str, strlen(hash_str));
        
    } else if (strcmp(path, "/get") == 0) {
        char filepath[1024];
        parse_query(query, filepath, sizeof(filepath));
        
        const ZarEntry* entry = find_entry(entries, count, filepath);
        if (!entry) {
            send_http_response(client_sock, 404, "Not Found", "text/plain", "File not found", 13);
            return;
        }
        
        uint8_t* data = read_entry_data(archive, entry);
        if (!data) {
            send_http_response(client_sock, 500, "Internal Server Error", "text/plain", "Decompression failed", 19);
            return;
        }
        
        send_http_response(client_sock, 200, "OK", "application/octet-stream", data, (size_t)entry->origSize);
        free(data);
        
    } else if (strcmp(path, "/get_compressed") == 0) {
        char filepath[1024];
        parse_query(query, filepath, sizeof(filepath));
        
        const ZarEntry* entry = find_entry(entries, count, filepath);
        if (!entry) {
            send_http_response(client_sock, 404, "Not Found", "text/plain", "File not found", 13);
            return;
        }
        
        uint64_t comp_size;
        uint8_t* data = read_entry_compressed(archive, entry, &comp_size);
        if (!data) {
            send_http_response(client_sock, 500, "Internal Server Error", "text/plain", "Read failed", 10);
            return;
        }
        
        send_http_response(client_sock, 200, "OK", "application/octet-stream", data, (size_t)comp_size);
        free(data);
        
    } else {
        const char* help = 
            "<html><body><h1>ZAR Archive Server</h1>"
            "<p>Endpoints:</p>"
            "<ul>"
            "<li>/list - List all files in archive</li>"
            "<li>/hash?file=PATH - Get CRC32 hash of file</li>"
            "<li>/get?file=PATH - Get decompressed file</li>"
            "<li>/get_compressed?file=PATH - Get compressed file data</li>"
            "</ul>"
            "</body></html>";
        send_http_response(client_sock, 200, "OK", "text/html", help, strlen(help));
    }
}

/* ── Commands ── */

static int cmd_create(const char* archive, int argc, char** argv)
{
    FileList fl = {0};
    for (int i = 0; i < argc; ++i)
        collect_files(&fl, argv[i]);

    if (fl.count == 0) {
        fprintf(stderr, "zar: no files to archive\n");
        return 1;
    }

    FILE* f = fopen(archive, "wb");
    if (!f) { fprintf(stderr, "zar: cannot create %s\n", archive); return 1; }

    ZarEntry* entries = (ZarEntry*)calloc((size_t)fl.count, sizeof(ZarEntry));
    uint64_t totalOrig = 0, totalComp = 0;
    uint64_t entryCount = 0;

    for (uint64_t i = 0; i < fl.count; ++i) {
        FILE* src = fopen(fl.paths[i], "rb");
        if (!src) { fprintf(stderr, "zar: cannot read %s\n", fl.paths[i]); continue; }

        fseeko(src, 0, SEEK_END);
        uint64_t size = (uint64_t)ftello(src);
        fseeko(src, 0, SEEK_SET);

        uint8_t* data = (uint8_t*)malloc((size_t)size);
        if (size > 0) fread(data, 1, (size_t)size, src);
        fclose(src);

        /* Get mod time */
        struct stat st;
        uint64_t modTime = 0;
        if (stat(fl.paths[i], &st) == 0)
            modTime = (uint64_t)st.st_mtime;

        /* Compress */
        uint8_t method = ZAR_LZ4;
        const void* writeData = data;
        uint64_t writeSize = size;
        uint8_t* compBuf = NULL;

        if (size > 0) {
            int bound = LZ4_compressBound((int)size);
            if (bound > 0) {
                compBuf = (uint8_t*)malloc(bound);
                int cs = LZ4_compress_default((const char*)data, (char*)compBuf, (int)size, bound);
                if (cs > 0 && (uint64_t)cs < size) {
                    writeData = compBuf;
                    writeSize = (uint64_t)cs;
                } else {
                    method = ZAR_STORE;
                }
            } else {
                method = ZAR_STORE;
            }
        } else {
            method = ZAR_STORE;
        }

        ZarEntry* e = &entries[entryCount];
        e->path = strdup(fl.paths[i]);
        e->offset = (uint64_t)ftello(f);
        e->origSize = size;
        e->compSize = writeSize;
        e->crc32 = crc32_calc(data, size);
        e->method = method;
        e->modTime = modTime;

        /* Write entry header */
        fwrite(ENTRY_MAGIC, 1, 4, f);
        uint16_t pathLen = (uint16_t)strlen(e->path);
        write_u16(f, pathLen);
        fwrite(e->path, 1, pathLen, f);
        write_u16(f, 0);  /* flags */
        write_u8(f, method);
        write_u64(f, e->origSize);
        write_u64(f, e->compSize);
        write_u32(f, e->crc32);
        write_u64(f, e->modTime);

        /* Write data */
        fwrite(writeData, 1, (size_t)writeSize, f);

        totalOrig += size;
        totalComp += writeSize;
        ++entryCount;

        free(compBuf);
        free(data);
    }

    /* Central directory */
    uint64_t dirOffset = (uint64_t)ftello(f);

    fwrite(DIR_MAGIC, 1, 4, f);
    write_u64(f, entryCount);

    for (uint64_t i = 0; i < entryCount; ++i) {
        ZarEntry* e = &entries[i];
        uint16_t pathLen = (uint16_t)strlen(e->path);
        write_u16(f, pathLen);
        fwrite(e->path, 1, pathLen, f);
        write_u64(f, e->offset);
        write_u64(f, e->origSize);
        write_u64(f, e->compSize);
        write_u32(f, e->crc32);
        write_u8(f, e->method);
        write_u64(f, e->modTime);
    }

    uint64_t dirSize = (uint64_t)ftello(f) - dirOffset;

    /* Footer */
    fwrite(FOOTER_MAGIC, 1, 4, f);
    write_u64(f, dirOffset);
    write_u64(f, dirSize);
    write_u16(f, FORMAT_VERSION);
    fwrite(TAIL_SENTINEL, 1, 2, f);

    fclose(f);

    double ratio = totalOrig > 0 ? (1.0 - (double)totalComp / (double)totalOrig) * 100.0 : 0.0;
    printf("Created %s: %llu files, %s -> %s (%.1f%% reduction)\n",
           archive, (unsigned long long)entryCount,
           human_size(totalOrig), human_size(totalComp), ratio);

    free_entries(entries, entryCount);
    for (uint64_t i = 0; i < fl.count; ++i) free(fl.paths[i]);
    free(fl.paths);
    return 0;
}

static int cmd_list(const char* archive)
{
    FILE* f = fopen(archive, "rb");
    if (!f) { fprintf(stderr, "zar: cannot open %s\n", archive); return 1; }

    ZarEntry* entries; uint64_t count;
    if (read_directory(f, &entries, &count) != 0) { fclose(f); return 1; }

    printf("%-8s %-8s %-5s %-6s %s\n", "SIZE", "COMP", "RATIO", "METHOD", "PATH");

    for (uint64_t i = 0; i < count; ++i) {
        double ratio = entries[i].origSize > 0 ?
            (1.0 - (double)entries[i].compSize / (double)entries[i].origSize) * 100.0 : 0.0;
        printf("%-8llu %-8llu %4.1f%% %-6s %s\n",
               (unsigned long long)entries[i].origSize,
               (unsigned long long)entries[i].compSize,
               ratio,
               entries[i].method == ZAR_LZ4 ? "lz4" : "store",
               entries[i].path);
    }

    printf("\n%llu entries\n", (unsigned long long)count);

    free_entries(entries, count);
    fclose(f);
    return 0;
}

static int cmd_extract(const char* archive, int argc, char** argv)
{
    FILE* f = fopen(archive, "rb");
    if (!f) { fprintf(stderr, "zar: cannot open %s\n", archive); return 1; }

    ZarEntry* entries; uint64_t count;
    if (read_directory(f, &entries, &count) != 0) { fclose(f); return 1; }

    uint64_t extracted = 0;
    for (uint64_t i = 0; i < count; ++i) {
        /* If specific files requested, skip non-matches */
        if (argc > 0) {
            int found = 0;
            for (int a = 0; a < argc; ++a) {
                if (strcmp(entries[i].path, argv[a]) == 0 ||
                    strstr(entries[i].path, argv[a]) != NULL) {
                    found = 1; break;
                }
            }
            if (!found) continue;
        }

        uint8_t* data = read_entry_data(f, &entries[i]);
        if (!data) { fprintf(stderr, "zar: failed to read %s\n", entries[i].path); continue; }

        /* Verify CRC */
        uint32_t crc = crc32_calc(data, entries[i].origSize);
        if (crc != entries[i].crc32) {
            fprintf(stderr, "zar: CRC MISMATCH %s (expected %08x, got %08x)\n",
                    entries[i].path, entries[i].crc32, crc);
        }

        /* Create parent dirs and write */
        mkdirs(entries[i].path);
        FILE* out = fopen(entries[i].path, "wb");
        if (!out) {
            fprintf(stderr, "zar: cannot write %s: %s\n", entries[i].path, strerror(errno));
            free(data);
            continue;
        }
        fwrite(data, 1, (size_t)entries[i].origSize, out);
        fclose(out);
        free(data);

        printf("  %s\n", entries[i].path);
        ++extracted;
    }

    printf("Extracted %llu files\n", (unsigned long long)extracted);
    free_entries(entries, count);
    fclose(f);
    return 0;
}

/* NEW: Get file (decompressed) - explicit version */
static int cmd_get(const char* archive, const char* filepath)
{
    FILE* f = fopen(archive, "rb");
    if (!f) { fprintf(stderr, "zar: cannot open %s\n", archive); return 1; }

    ZarEntry* entries; uint64_t count;
    if (read_directory(f, &entries, &count) != 0) { fclose(f); return 1; }

    for (uint64_t i = 0; i < count; ++i) {
        if (strcmp(entries[i].path, filepath) == 0) {
            uint8_t* data = read_entry_data(f, &entries[i]);
            if (!data) { fprintf(stderr, "zar: failed to read %s\n", filepath); break; }
            fwrite(data, 1, (size_t)entries[i].origSize, stdout);
            free(data);
            free_entries(entries, count);
            fclose(f);
            return 0;
        }
    }

    fprintf(stderr, "zar: %s not found in archive\n", filepath);
    free_entries(entries, count);
    fclose(f);
    return 1;
}

/* NEW: Get CRC32 hash of a file */
static int cmd_hash(const char* archive, const char* filepath)
{
    FILE* f = fopen(archive, "rb");
    if (!f) { fprintf(stderr, "zar: cannot open %s\n", archive); return 1; }

    ZarEntry* entries; uint64_t count;
    if (read_directory(f, &entries, &count) != 0) { fclose(f); return 1; }

    const ZarEntry* entry = find_entry(entries, count, filepath);
    if (!entry) {
        fprintf(stderr, "zar: %s not found in archive\n", filepath);
        free_entries(entries, count);
        fclose(f);
        return 1;
    }

    printf("%08x\n", entry->crc32);
    free_entries(entries, count);
    fclose(f);
    return 0;
}

/* NEW: Get compressed file data */
static int cmd_get_compressed(const char* archive, const char* filepath)
{
    FILE* f = fopen(archive, "rb");
    if (!f) { fprintf(stderr, "zar: cannot open %s\n", archive); return 1; }

    ZarEntry* entries; uint64_t count;
    if (read_directory(f, &entries, &count) != 0) { fclose(f); return 1; }

    const ZarEntry* entry = find_entry(entries, count, filepath);
    if (!entry) {
        fprintf(stderr, "zar: %s not found in archive\n", filepath);
        free_entries(entries, count);
        fclose(f);
        return 1;
    }

    uint64_t comp_size;
    uint8_t* data = read_entry_compressed(f, entry, &comp_size);
    if (!data) {
        fprintf(stderr, "zar: failed to read compressed data for %s\n", filepath);
        free_entries(entries, count);
        fclose(f);
        return 1;
    }

    fwrite(data, 1, (size_t)comp_size, stdout);
    free(data);
    free_entries(entries, count);
    fclose(f);
    return 0;
}

/* NEW: HTTP Server */
static int cmd_serve(const char* archive, int port)
{
    FILE* f = fopen(archive, "rb");
    if (!f) { fprintf(stderr, "zar: cannot open %s\n", archive); return 1; }

    ZarEntry* entries; uint64_t count;
    if (read_directory(f, &entries, &count) != 0) { fclose(f); return 1; }

    int sockfd = create_socket((uint16_t)port);
    if (sockfd < 0) {
        free_entries(entries, count);
        fclose(f);
        return 1;
    }

    printf("ZAR server listening on port %d\n", port);
    printf("Archive: %s (%llu entries)\n", archive, (unsigned long long)count);
    printf("Endpoints:\n");
    printf("  /list                    - List all files\n");
    printf("  /hash?file=PATH          - Get CRC32 hash\n");
    printf("  /get?file=PATH           - Get decompressed file\n");
    printf("  /get_compressed?file=PATH - Get compressed data\n");
    printf("Press Ctrl+C to stop...\n");

    for (;;) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_sock = accept(sockfd, (struct sockaddr*)&client_addr, &client_len);
        
        if (client_sock < 0) {
            perror("accept");
            continue;
        }

        handle_http_request(client_sock, f, entries, count);
        close(client_sock);
    }

    free_entries(entries, count);
    fclose(f);
    close(sockfd);
    return 0;
}

static int cmd_verify(const char* archive)
{
    FILE* f = fopen(archive, "rb");
    if (!f) { fprintf(stderr, "zar: cannot open %s\n", archive); return 1; }

    ZarEntry* entries; uint64_t count;
    if (read_directory(f, &entries, &count) != 0) { fclose(f); return 1; }

    uint64_t ok = 0, fail = 0;
    for (uint64_t i = 0; i < count; ++i) {
        uint8_t* data = read_entry_data(f, &entries[i]);
        if (!data) { fprintf(stderr, "FAIL  %s (read error)\n", entries[i].path); ++fail; continue; }

        uint32_t crc = crc32_calc(data, entries[i].origSize);
        free(data);

        if (crc != entries[i].crc32) {
            fprintf(stderr, "FAIL  %s (CRC %08x != %08x)\n", entries[i].path, crc, entries[i].crc32);
            ++fail;
        } else {
            ++ok;
        }
    }

    printf("%llu OK, %llu FAILED (of %llu)\n",
           (unsigned long long)ok, (unsigned long long)fail, (unsigned long long)count);

    free_entries(entries, count);
    fclose(f);
    return fail > 0 ? 1 : 0;
}

static int cmd_cat(const char* archive, const char* filepath)
{
    FILE* f = fopen(archive, "rb");
    if (!f) { fprintf(stderr, "zar: cannot open %s\n", archive); return 1; }

    ZarEntry* entries; uint64_t count;
    if (read_directory(f, &entries, &count) != 0) { fclose(f); return 1; }

    for (uint64_t i = 0; i < count; ++i) {
        if (strcmp(entries[i].path, filepath) == 0) {
            uint8_t* data = read_entry_data(f, &entries[i]);
            if (!data) { fprintf(stderr, "zar: failed to read %s\n", filepath); break; }
            fwrite(data, 1, (size_t)entries[i].origSize, stdout);
            free(data);
            free_entries(entries, count);
            fclose(f);
            return 0;
        }
    }

    fprintf(stderr, "zar: %s not found in archive\n", filepath);
    free_entries(entries, count);
    fclose(f);
    return 1;
}

static int cmd_stat(const char* archive)
{
    FILE* f = fopen(archive, "rb");
    if (!f) { fprintf(stderr, "zar: cannot open %s\n", archive); return 1; }

    fseeko(f, 0, SEEK_END);
    uint64_t fileSize = (uint64_t)ftello(f);

    ZarEntry* entries; uint64_t count;
    if (read_directory(f, &entries, &count) != 0) { fclose(f); return 1; }

    uint64_t totalOrig = 0, totalComp = 0;
    uint64_t lz4Count = 0, storeCount = 0;
    uint64_t largest = 0;
    const char* largestPath = "";

    for (uint64_t i = 0; i < count; ++i) {
        totalOrig += entries[i].origSize;
        totalComp += entries[i].compSize;
        if (entries[i].method == ZAR_LZ4) ++lz4Count; else ++storeCount;
        if (entries[i].origSize > largest) {
            largest = entries[i].origSize;
            largestPath = entries[i].path;
        }
    }

    double ratio = totalOrig > 0 ? (1.0 - (double)totalComp / (double)totalOrig) * 100.0 : 0.0;

    printf("Archive:      %s\n", archive);
    printf("Archive size: %s\n", human_size(fileSize));
    printf("Entries:      %llu\n", (unsigned long long)count);
    printf("Original:     %s\n", human_size(totalOrig));
    printf("Compressed:   %s\n", human_size(totalComp));
    printf("Ratio:        %.1f%% reduction\n", ratio);
    printf("LZ4:          %llu entries\n", (unsigned long long)lz4Count);
    printf("Stored:       %llu entries\n", (unsigned long long)storeCount);
    printf("Largest:      %s (%s)\n", human_size(largest), largestPath);
    printf("Format:       ZAR v%u\n", FORMAT_VERSION);

    free_entries(entries, count);
    fclose(f);
    return 0;
}

/* ── Main ── */

static void usage(void)
{
    fprintf(stderr,
        "zar — ZAR Archive Tool (Extended for Warm-Start Context)\n"
        "\n"
        "Usage:\n"
        "  zar c <archive.zar> <file|dir> ...   Create archive\n"
        "  zar t <archive.zar>                   List contents\n"
        "  zar x <archive.zar> [file ...]        Extract (all or named)\n"
        "  zar v <archive.zar>                   Verify integrity\n"
        "  zar cat <archive.zar> <file>           Print file to stdout\n"
        "  zar stat <archive.zar>                 Archive statistics\n"
        "  zar hash <archive.zar> <file>         Get CRC32 hash of file\n"
        "  zar get <archive.zar> <file>          Get decompressed file\n"
        "  zar get_compressed <archive.zar> <file>  Get compressed file data\n"
        "  zar serve <archive.zar> <port>        HTTP server for remote access\n"
        "\n"
        "ZAR: writes like tar, reads like zip. LZ4 compressed, CRC-32 verified.\n"
        "Extended for AI warm-start context with hash queries and HTTP serving.\n"
    );
}

int main(int argc, char** argv)
{
    if (argc < 3) { usage(); return 1; }

    const char* cmd = argv[1];
    const char* archive = argv[2];

    if (strcmp(cmd, "c") == 0) {
        if (argc < 4) { fprintf(stderr, "zar: 'c' requires at least one input file\n"); return 1; }
        return cmd_create(archive, argc - 3, argv + 3);
    }
    if (strcmp(cmd, "t") == 0)
        return cmd_list(archive);
    if (strcmp(cmd, "x") == 0)
        return cmd_extract(archive, argc - 3, argv + 3);
    if (strcmp(cmd, "v") == 0)
        return cmd_verify(archive);
    if (strcmp(cmd, "cat") == 0) {
        if (argc < 4) { fprintf(stderr, "zar: 'cat' requires a file path\n"); return 1; }
        return cmd_cat(archive, argv[3]);
    }
    if (strcmp(cmd, "stat") == 0)
        return cmd_stat(archive);
    
    /* NEW COMMANDS */
    if (strcmp(cmd, "hash") == 0) {
        if (argc < 4) { fprintf(stderr, "zar: 'hash' requires a file path\n"); return 1; }
        return cmd_hash(archive, argv[3]);
    }
    if (strcmp(cmd, "get") == 0) {
        if (argc < 4) { fprintf(stderr, "zar: 'get' requires a file path\n"); return 1; }
        return cmd_get(archive, argv[3]);
    }
    if (strcmp(cmd, "get_compressed") == 0) {
        if (argc < 4) { fprintf(stderr, "zar: 'get_compressed' requires a file path\n"); return 1; }
        return cmd_get_compressed(archive, argv[3]);
    }
    if (strcmp(cmd, "serve") == 0) {
        if (argc < 4) { fprintf(stderr, "zar: 'serve' requires a port number\n"); return 1; }
        return cmd_serve(archive, atoi(argv[3]));
    }

    fprintf(stderr, "zar: unknown command '%s'\n", cmd);
    usage();
    return 1;
}
