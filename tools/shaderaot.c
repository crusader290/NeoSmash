/* Ahead-of-time shader compiler: the game's programs found in data.arc (tools/shader_aot.py
 * packs them) into the renderer's disk cache, translated and compiled exactly as the renderer
 * would, so that a program's first use loads its DXBC instead of compiling it.
 *
 * Usage: shaderaot <pack> <cache dir> <part> <parts> [--dry] [--keys <file>]
 *   Every process translates all vertex programs (their outputs are the paired pixel programs'
 *   inputs) and compiles the programs whose shader hash is <part> modulo <parts>. --dry translates only
 *   and prints the cache key of each program ("vs <hash>", "ps <hash> <vs outputs>"); --keys
 *   compiles only the keys listed in a file of such lines. On stderr, "progress <done> <of>" lines
 *   say how far it is, in keys, and a last line what it did.
 *   --source-report <file> records program index, stage, hash, code size and unsupported
 *   instruction diagnostics (tab separated). Intended for auditing speculative candidates.
 *
 * Pack: "SAOT", u32 count; then per program u32 stage (0 vertex, 1 pixel), u32 length, u32 pairs,
 * u32 pair[pairs] (the vertex programs found before it in its containers), then its bytes
 * (the header, its code, and possibly bytes after its end).
 *
 * Files are written dated 2001-01-01 (gpu_shader_cache.h): the start's prewarm leaves them out
 * until the game has used them. Programs with indirect branches are left out: their key depends
 * on the constant buffers of a draw. Build: python tools/shader_aot.py builds it. */
#define _CRT_SECURE_NO_WARNINGS
#include <setjmp.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#include "../runtime/gpu_shader_cache.h"

u32 shader_size(const u8 *program, u32 max);

static jmp_buf on_fatal;
static char fatal_text[256];
_Noreturn void rt_fatal(const char *fmt, ...) {   /* the translator gave up on a program */
    va_list a;
    va_start(a, fmt);
    vsnprintf(fatal_text, sizeof fatal_text, fmt, a);
    va_end(a);
    longjmp(on_fatal, 1);
}

static u32 zero_cbuf(void *user, u32 bank, u32 offset) { (void)user; (void)bank; (void)offset; return 0; }

typedef struct Program {
    int stage; u32 length, npairs; const u32 *pairs; const u8 *bytes; u32 size; u64 hash; int ok; u32 outputs, ntables;
    int mine;                  /* a vertex program this part compiles */
    u32 nmasks, mask_cap, *masks;  /* every distinct paired vertex output mask */
} Program;

static char *translate(Program *p, u32 vs_outputs, ShaderInfo *info) {
    if (setjmp(on_fatal)) return NULL;
    return shader_hlsl_ex(p->bytes, p->size, p->stage, vs_outputs, info, zero_cbuf, NULL);
}

static int exists(const char *path) { return GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES; }

static void append_pixel_mask(Program *p, u32 mask) {
    for (u32 i = 0; i < p->nmasks; ++i) if (p->masks[i] == mask) return;
    if (p->nmasks == p->mask_cap) {
        u32 cap = p->mask_cap ? p->mask_cap * 2 : 8;
        u32 *grown = realloc(p->masks, cap * sizeof *grown);
        if (!grown) { fprintf(stderr, "cannot allocate shader interface masks\n"); exit(2); }
        p->masks = grown;
        p->mask_cap = cap;
    }
    p->masks[p->nmasks++] = mask;
}

/* All distinct output masks, without the old 16-interface truncation. */
static u32 pixel_masks(Program *p, const Program *prog, u32 total) {
    p->nmasks = 0;
    for (u32 k = 0; k < p->npairs; k++) {
        const Program *v = p->pairs[k] < total ? &prog[p->pairs[k]] : NULL;
        if (!v || !v->ok) continue;
        append_pixel_mask(p, v->outputs);
    }
    return p->nmasks;
}

static struct { u32 compiled, existing, tables, failed, translate_failed, keys; } count;

/* How far this part is, for shader_aot.py's progress bar: "progress <done> <of>" on stderr, counted
   in keys (one compile each, or a look in the cache): first the total, then every 16th and the last. */
static u32 progress_done, progress_of;
static void progress(void) {
    if (++progress_done % 16 == 0 || progress_done == progress_of) {
        fprintf(stderr, "progress %u %u\n", progress_done, progress_of);
        fflush(stderr);
    }
}

/* Compile and write one cache file, dated as not yet used. */
static void compile_to(const char *hlsl, const char *name, int stage, const char *path) {
    ID3DBlob *code, *errors;
    HRESULT hr = shader_cache_compile(hlsl, name, stage, &code, &errors);
    if (FAILED(hr)) {
        count.failed++;
        fprintf(stderr, "%s: fxc failed: %.2000s\n", name, errors ? (const char *)errors->lpVtbl->GetBufferPointer(errors) : "?");
        char diagnostic[1300];
        snprintf(diagnostic, sizeof diagnostic, "%s.failed.hlsl", path);
        FILE *source = fopen(diagnostic, "wb");
        if (source) { fwrite(hlsl, 1, strlen(hlsl), source); fclose(source); }
    } else {
        char tmp[1300];
        snprintf(tmp, sizeof tmp, "%s.%lu.tmp", path, GetCurrentProcessId());
        FILE *f = fopen(tmp, "wb");
        if (f) {
            fwrite(code->lpVtbl->GetBufferPointer(code), 1, code->lpVtbl->GetBufferSize(code), f);
            fclose(f);
            HANDLE h = CreateFileA(tmp, FILE_WRITE_ATTRIBUTES, 0, NULL, OPEN_EXISTING, 0, NULL);
            if (h != INVALID_HANDLE_VALUE) {
                FILETIME t = {(DWORD)SHADER_CACHE_UNPLAYED, (DWORD)(SHADER_CACHE_UNPLAYED >> 32)};
                SetFileTime(h, NULL, NULL, &t);
                CloseHandle(h);
            }
            if (!MoveFileExA(tmp, path, 0)) DeleteFileA(tmp);   /* (another process wrote it first) */
            count.compiled++;
        }
    }
    if (code) code->lpVtbl->Release(code);
    if (errors) errors->lpVtbl->Release(errors);
}

/* Sets of keys: the ones this process has seen (the same program is often packed more than
 * once), and --keys' list. */
typedef struct CompileKey { u64 hash; u32 mask; int stage, used; } CompileKey;
typedef struct KeySet { CompileKey *slot; u32 cap, count; } KeySet;
static KeySet seen = {0, 1u << 17}, only = {0, 1u << 16}, counted = {0, 1u << 17};
static int only_enabled;
static u32 key_bucket(u32 cap, int stage, u64 hash, u32 mask) {
    u64 mixed = (hash ^ ((u64)mask << 17) ^ (u64)stage) * 0x9E3779B97F4A7C15ull;
    return (u32)(mixed >> 32) & (cap - 1);
}
static void grow_keys(KeySet *ks) {
    u32 cap = ks->slot ? ks->cap * 2 : ks->cap;
    CompileKey *slots = calloc(cap, sizeof *slots);
    if (!slots) { fprintf(stderr, "cannot allocate shader key set\n"); exit(2); }
    if (ks->slot) {
        for (u32 i = 0; i < ks->cap; ++i) if (ks->slot[i].used) {
            CompileKey key = ks->slot[i];
            u32 at = key_bucket(cap, key.stage, key.hash, key.mask);
            while (slots[at].used) at = (at + 1) & (cap - 1);
            slots[at] = key;
        }
        free(ks->slot);
    }
    ks->slot = slots;
    ks->cap = cap;
}
static int add_key(KeySet *ks, int stage, u64 hash, u32 mask) {
    if (!ks->slot || (ks->count + 1) * 2 >= ks->cap) grow_keys(ks);
    for (u32 i = key_bucket(ks->cap, stage, hash, mask);; i = (i + 1) & (ks->cap - 1)) {
        CompileKey *key = &ks->slot[i];
        if (!key->used) { *key = (CompileKey){hash, mask, stage, 1}; ks->count++; return 1; }
        if (key->stage == stage && key->hash == hash && key->mask == mask) return 0;
    }
}
static int has_key(const KeySet *ks, int stage, u64 hash, u32 mask) {
    if (!ks->slot) return 0;
    for (u32 i = key_bucket(ks->cap, stage, hash, mask);; i = (i + 1) & (ks->cap - 1)) {
        const CompileKey *key = &ks->slot[i];
        if (!key->used) return 0;
        if (key->stage == stage && key->hash == hash && key->mask == mask) return 1;
    }
}
static int first_time(int stage, u64 hash, u32 mask) {
    return add_key(&seen, stage, hash, mask) && (!only_enabled || has_key(&only, stage, hash, mask));
}
/* the same, ahead of the compiling: for counting the keys to do (seen in the same order) */
static int to_count(int stage, u64 hash, u32 mask) {
    return add_key(&counted, stage, hash, mask) && (!only_enabled || has_key(&only, stage, hash, mask));
}

int main(int argc, char **argv) {
    if (argc < 5) { fprintf(stderr, "usage: shaderaot <pack> <cache dir> <part> <parts> [--dry]\n"); return 2; }
    const char *dir = argv[2];
    u32 part = (u32)atoi(argv[3]), parts = (u32)atoi(argv[4]);
    int dry = 0;
    FILE *source_report = NULL;
    for (int a = 5; a < argc; a++) {
        if (!strcmp(argv[a], "--dry")) dry = 1;
        else if (!strcmp(argv[a], "--source-report") && a + 1 < argc) {
            source_report = fopen(argv[++a], "w");
            if (!source_report) { fprintf(stderr, "cannot open source report\n"); return 1; }
        }
        else if (!strcmp(argv[a], "--keys") && a + 1 < argc) {
            FILE *k = fopen(argv[++a], "r");
            if (!k) { fprintf(stderr, "cannot open shader key list: %s\n", argv[a]); return 1; }
            only_enabled = 1;
            char kind[4];
            unsigned long long hash;
            unsigned mask = 0;
            char line[128];
            while (k && fgets(line, sizeof line, k)) {
                int n = sscanf(line, "%3s %llx %x", kind, &hash, &mask);
                if (n >= 2 && !strcmp(kind, "vs")) add_key(&only, SHADER_VERTEX, hash, 0);
                else if (n == 3 && !strcmp(kind, "ps")) add_key(&only, SHADER_PIXEL, hash, mask);
            }
            if (k) fclose(k);
        }
    }
    FILE *f = fopen(argv[1], "rb");
    if (!f) { fprintf(stderr, "cannot open %s\n", argv[1]); return 1; }
    _fseeki64(f, 0, SEEK_END);
    long long n = _ftelli64(f);
    _fseeki64(f, 0, SEEK_SET);
    u8 *pack = malloc((size_t)n);
    if (!pack || fread(pack, 1, (size_t)n, f) != (size_t)n || memcmp(pack, "SAOT", 4)) { fprintf(stderr, "bad pack\n"); return 1; }
    fclose(f);
    u32 total;
    memcpy(&total, pack + 4, 4);
    Program *prog = calloc(total, sizeof *prog);
    const u8 *q = pack + 8;
    for (u32 i = 0; i < total; i++) {
        u32 w[3];
        memcpy(w, q, 12);
        prog[i].stage = w[0] ? SHADER_PIXEL : SHADER_VERTEX;
        prog[i].length = w[1];
        prog[i].npairs = w[2];
        prog[i].pairs = (const u32 *)(q + 12);
        prog[i].bytes = q + 12 + 4ull * w[2];
        q = prog[i].bytes + w[1];
        if (!shader_program_header_valid(prog[i].bytes, prog[i].length)) {
            fprintf(stderr, "ignored invalid SPH in pack record %u\n", i);
            prog[i].length = 0;   /* preserve pair indices; neither translate nor compile this record */
        }
    }
    ShaderInfo info;
    char path[1200], name[64];
    /* every vertex program: its key and outputs (what the pixel programs paired with it read);
       which of them are this part's to compile */
    for (u32 i = 0; i < total; i++) {
        Program *p = &prog[i];
        if (p->stage != SHADER_VERTEX || p->length <= 0x50) continue;
        if (setjmp(on_fatal)) { count.translate_failed++; continue; }
        p->size = shader_size(p->bytes, p->length);
        if (p->size <= 0x50) continue;
        p->hash = shader_program_hash(p->bytes, p->size);
        char *hlsl = translate(p, 0, &info);
        if (!hlsl) { count.translate_failed++; continue; }
        free(hlsl);
        if (source_report) fprintf(source_report, "%u\tvs\t%016llx\t%u\t%s\n", i,
                                   (unsigned long long)p->hash, p->size, info.unsupported);
        p->ok = 1;
        p->outputs = info.outputs;
        p->ntables = info.ntables;
        p->mine = p->hash % parts == part && first_time(SHADER_VERTEX, p->hash, 0);
        progress_of += p->mine;
    }
    /* this part's pixel programs: one key per distinct output mask of their vertex programs */
    for (u32 i = 0; i < total; i++) {
        Program *p = &prog[i];
        if (p->stage != SHADER_PIXEL || p->length <= 0x50) continue;
        if (setjmp(on_fatal)) { count.translate_failed++; continue; }
        p->size = shader_size(p->bytes, p->length);
        if (p->size <= 0x50) continue;
        p->hash = shader_program_hash(p->bytes, p->size);
        if (source_report) {
            char *probe = translate(p, 0xFFFFFFFFu, &info);
            fprintf(source_report, "%u\tps\t%016llx\t%u\t%s\n", i,
                    (unsigned long long)p->hash, p->size, probe ? info.unsupported : "translation failed");
            free(probe);
        }
        p->mine = p->hash % parts == part;
        if (!p->mine) continue;
        p->nmasks = pixel_masks(p, prog, total);
        if (!p->nmasks) {   /* learn its input mask once, before counting unique work */
            char *probe = translate(p, 0xFFFFFFFFu, &info);
            if (!probe) { count.translate_failed++; p->mine = 0; continue; }
            free(probe);
            append_pixel_mask(p, info.inputs);
        }
        for (u32 m = 0; m < p->nmasks; m++) progress_of += to_count(SHADER_PIXEL, p->hash, p->masks[m]);
    }
    fprintf(stderr, "progress 0 %u\n", progress_of);   /* (the total, before the first compile) */
    fflush(stderr);
    /* this part's programs, in the pack's order: vertex and pixel ones come mixed, so the pace
       stays even and the time left holds from the start. A pixel program: once per mask. */
    for (u32 i = 0; i < total; i++) {
        Program *p = &prog[i];
        if (!p->mine) continue;
        if (p->stage == SHADER_VERTEX && p->mine) {
            progress();
            count.keys++;
            snprintf(name, sizeof name, "vs_%016llx", (unsigned long long)p->hash);
            shader_cache_file(path, sizeof path, dir, SHADER_VERTEX, p->hash, 0, 0);
            if (dry) { printf("vs %016llx\n", (unsigned long long)p->hash); continue; }
            if (p->ntables) { count.tables++; continue; }
            if (exists(path)) { count.existing++; continue; }
            char *hlsl = translate(p, 0, &info);
            if (!hlsl) { count.translate_failed++; continue; }
            compile_to(hlsl, name, SHADER_VERTEX, path);
            free(hlsl);
            continue;
        }
        if (p->stage != SHADER_PIXEL || p->size <= 0x50) continue;
        u32 nmasks = p->nmasks;
        const u32 *masks = p->masks;
        for (u32 m = 0; m < nmasks; m++) {
            int first = first_time(SHADER_PIXEL, p->hash, masks[m]);
            if (first) progress();
            if (!first) continue;
            count.keys++;
            snprintf(name, sizeof name, "ps_%016llx", (unsigned long long)p->hash);
            shader_cache_file(path, sizeof path, dir, SHADER_PIXEL, p->hash, masks[m], 0);
            if (dry) { printf("ps %016llx %08x\n", (unsigned long long)p->hash, masks[m]); continue; }
            if (exists(path)) { count.existing++; continue; }
            char *hlsl = translate(p, masks[m], &info);
            if (!hlsl) { count.translate_failed++; continue; }
            if (info.ntables) count.tables++;
            else compile_to(hlsl, name, SHADER_PIXEL, path);
            free(hlsl);
        }
    }
    fprintf(stderr, "progress %u %u\n", progress_done, progress_of);
    fprintf(stderr, "part %u/%u: %u keys; %u compiled, %u already cached, %u with jump tables (left out), %u fxc failures, %u not translated\n",
            part, parts, count.keys, count.compiled, count.existing, count.tables, count.failed, count.translate_failed);
    if (source_report) fclose(source_report);
    return 0;
}
