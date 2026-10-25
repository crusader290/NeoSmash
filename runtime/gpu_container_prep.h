/* Container preparation and the loading barrier (gpu_d3d11.c; hle_fs.c reports data.arc reads).
 *
 * The game keeps its shaders in containers in data.arc (.eff, .nushdb, .arc). When it reads any
 * part of one - a loading screen bringing in a fighter, a stage, an effect set - every shader of
 * that container is queued for the prewarm threads at once (queue 2: below the game's own
 * shaders), so its objects and DXVK's pipeline libraries are made in the background before the
 * game draws with them. tools/container_index.py says which disk-cache files each container's
 * programs are, and where the container lies in data.arc (dump/effect_profiles/containers/).
 *
 * The barrier: a frame presented outside a fight (no compute dispatch in it: fights skin their
 * fighters with compute every frame) waits while announced shaders are still being made, and
 * then for DXVK's shader workers to go quiet, at most 20 s. So the game leaves a loading screen
 * only once what it loaded is ready, and the fight meets none of it for the first time. A fight
 * is never held: what a fight reads is prepared in the background meanwhile.
 *
 * Default on under DXVK with an index. SSBU_CONTAINER_PREP=0 turns both off, SSBU_LOAD_BARRIER=0
 * the barrier only; SSBU_CONTAINER_PREP_MB (default 8192) is the private memory above which no
 * more containers are queued (each queued shader keeps an object until the game takes it).
 * SSBU_CONTAINER_PREP_MAX (default 1000): larger containers are not queued whole. The ten global
 * shader libraries the game reads at boot hold 3,000-4,000 variants each (pixel programs times
 * every interface their file's vertex programs could give them), of which it uses about 180:
 * queuing them held the first boot 90 s and took 5 GB. Their used variants are in the start-up
 * list (the game's history) from the first run on; fighters', stages' and effects' containers -
 * what a fight's loading screen reads - are far smaller and go whole. */

/* Netplay asked for (the launchers' SSBU_NET_HOST / SSBU_NET_RELAY, as net_relay.c reads them;
 * not net_configured(), which would start the network at the game's first file read) */
static int prep_netplay(void) {
    const char *h = getenv("SSBU_NET_HOST"), *r = getenv("SSBU_NET_RELAY");
    return (h && *h && strcmp(h, "0")) || (r && *r);
}

typedef struct { u64 offset, stored; u32 first, count; } PrepContainer;
static struct {
    int on, barrier;
    u32 ncont, nnames, nrefs;
    PrepContainer *cont;
    char (*names)[64];
    u32 *refs;
    volatile LONG *state;          /* per container: 0 unseen, 1 read (to queue), 2 queued */
    volatile LONG ready;           /* the renderer can take jobs (after its start-up preparation) */
    volatile LONG64 announced, queued, skipped_budget, skipped_large;
    u64 budget;
    u32 max;
    u64 held_ms, held_frames, longest_ms, timeouts;
    volatile u64 held_ns;          /* all of it, for scripted input (hle_hid.c) */
} prep;

/* (hle_hid.c) how long the barrier has held frames in all: a script's relative clock leaves it out */
u64 gfx_held_ns(void) { return prep.held_ns; }
static INIT_ONCE prep_once = INIT_ONCE_STATIC_INIT;

static BOOL CALLBACK prep_load(PINIT_ONCE o, PVOID p, PVOID *c) {
    (void)o; (void)p; (void)c;
    const char *e = getenv("SSBU_CONTAINER_PREP");
    if (e && !atoi(e)) return TRUE;
    char path[1200];
    if (getenv("SSBU_CONTAINER_INDEX")) snprintf(path, sizeof path, "%s", getenv("SSBU_CONTAINER_INDEX"));
    else snprintf(path, sizeof path, "%s/../effect_profiles/containers/index.bin", cache_dir());
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "[gfx] container preparation: no index (%s); tools/container_index.py makes it\n", path); return TRUE; }
    u32 head[5];
    if (fread(head, 4, 5, f) != 5 || memcmp(head, "SCX1", 4) || head[1] != SHADER_CACHE_VERSION) {
        fprintf(stderr, "[gfx] container preparation: %s is not an index of this shader cache version; ignored\n", path);
        fclose(f);
        return TRUE;
    }
    prep.ncont = head[2]; prep.nnames = head[3]; prep.nrefs = head[4];
    prep.names = malloc((size_t)prep.nnames * 64);
    prep.cont = malloc(sizeof *prep.cont * (size_t)prep.ncont);
    prep.refs = malloc(sizeof *prep.refs * (size_t)prep.nrefs);
    prep.state = calloc(prep.ncont ? prep.ncont : 1, sizeof *prep.state);
    int ok = prep.names && prep.cont && prep.refs && prep.state && fread(prep.names, 64, prep.nnames, f) == prep.nnames;
    for (u32 i = 0; ok && i < prep.ncont; i++) {
        u8 raw[24];
        ok = fread(raw, 1, 24, f) == 24;
        memcpy(&prep.cont[i].offset, raw, 8); memcpy(&prep.cont[i].stored, raw + 8, 8);
        memcpy(&prep.cont[i].first, raw + 16, 4); memcpy(&prep.cont[i].count, raw + 20, 4);
        ok = ok && (u64)prep.cont[i].first + prep.cont[i].count <= prep.nrefs;
    }
    ok = ok && fread(prep.refs, 4, prep.nrefs, f) == prep.nrefs;
    fclose(f);
    for (u32 i = 0; ok && i < prep.nrefs; i++) ok = prep.refs[i] < prep.nnames;
    if (!ok) { fprintf(stderr, "[gfx] container preparation: %s is damaged; ignored\n", path); return TRUE; }
    const char *mb = getenv("SSBU_CONTAINER_PREP_MB");
    prep.budget = (u64)(mb ? atoi(mb) : 8192) << 20;
    const char *mx = getenv("SSBU_CONTAINER_PREP_MAX");
    prep.max = mx ? (u32)atoi(mx) : 1000;
    const char *b = getenv("SSBU_LOAD_BARRIER");
    prep.barrier = !b || atoi(b);
    /* Netplay: the other players' games run on while one is held, and a room or a battle's start
     * can time this player out; preparation still runs, the barrier does not (=1 forces it). */
    if (!b && prep_netplay()) prep.barrier = 0;
    prep.on = 1;
    fprintf(stderr, "[gfx] container preparation: %u containers, %u shaders; loading barrier %s\n", prep.ncont, prep.nnames,
            prep.barrier ? "on" : !b && prep_netplay() ? "off (netplay)" : "off");
    return TRUE;
}

/* Every shader of container i not prepared or used yet: into the prewarm queue. */
static void prep_queue(u32 i) {
    const PrepContainer *c = &prep.cont[i];
    if (c->count > prep.max) {   /* (see SSBU_CONTAINER_PREP_MAX above) */
        InterlockedIncrement64(&prep.skipped_large);
        fprintf(stderr, "[gfx] container preparation: data.arc %#llx (%u shaders) left to the start-up list and first use\n",
                (unsigned long long)c->offset, c->count);
        return;
    }
    if (private_bytes() > prep.budget) {
        if (InterlockedIncrement64(&prep.skipped_budget) == 1)
            fprintf(stderr, "[gfx] container preparation: private memory over %llu MB; further containers are left to their first draw\n",
                    (unsigned long long)(prep.budget >> 20));
        return;
    }
    Shader *head = NULL, *tail = NULL;
    u32 queued = 0;
    for (u32 k = 0; k < c->count; k++) {
        const char *name = prep.names[prep.refs[c->first + k]];
        unsigned version, vs_outputs;
        char stage_name[4];
        unsigned long long hash, tables;
        if (sscanf(name, "v%u_%2[a-z]_%16llx_%8x_%16llx.dxbc", &version, stage_name, &hash, &vs_outputs, &tables) != 5) continue;
        int stage = !strcmp(stage_name, "vs") ? SHADER_VERTEX : !strcmp(stage_name, "ps") ? SHADER_PIXEL : -1;
        if (stage < 0) continue;
        Shader *s = calloc(1, sizeof *s);
        if (!s) break;
        s->hash = hash; s->stage = stage; s->vs_outputs = vs_outputs; s->table_hash = tables;
        snprintf(s->name, sizeof s->name, "%s_%016llx", stage_name, hash);
        AcquireSRWLockExclusive(&prewarm_lock);
        s->prewarm = prewarm_slot(stage, hash, vs_outputs, tables, 1);
        if (s->prewarm && s->prewarm->state == PW_EMPTY) s->prewarm->state = PW_QUEUED;
        else { free(s); s = NULL; }   /* prepared, being prepared, or the game's already */
        ReleaseSRWLockExclusive(&prewarm_lock);
        if (!s) continue;
        if (tail) tail->next_job = s; else head = s;
        tail = s;
        queued++;
    }
    if (!queued) return;
    InterlockedExchangeAdd(&prewarm_total, (LONG)queued);
    InterlockedExchangeAdd64(&prep.queued, queued);
    AcquireSRWLockExclusive(&jobs_lock);
    if (job_queue[2].tail) job_queue[2].tail->next_job = head; else job_queue[2].head = head;
    job_queue[2].tail = tail;
    ReleaseSRWLockExclusive(&jobs_lock);
    WakeAllConditionVariable(&jobs_cv);
    fprintf(stderr, "[gfx] container preparation: data.arc %#llx (%llu KB): %u of its %u shaders queued\n",
            (unsigned long long)c->offset, (unsigned long long)(c->stored >> 10), queued, c->count);
}

/* (hle_fs.c, any game thread) the game read [offset, offset + size) of data.arc */
void gfx_arc_read(u64 offset, u64 size) {
    InitOnceExecuteOnce(&prep_once, prep_load, NULL, NULL);
    if (!prep.on || !size) return;
    u32 lo = 0, hi = prep.ncont;   /* the first container that ends after offset */
    while (lo < hi) {
        u32 mid = (lo + hi) / 2;
        if (prep.cont[mid].offset + prep.cont[mid].stored <= offset) lo = mid + 1; else hi = mid;
    }
    for (u32 i = lo; i < prep.ncont && prep.cont[i].offset < offset + size; i++) {
        if (prep.cont[i].offset + prep.cont[i].stored <= offset) continue;
        if (InterlockedCompareExchange(&prep.state[i], 1, 0) != 0) continue;
        InterlockedIncrement64(&prep.announced);
        if (prep.ready && InterlockedCompareExchange(&prep.state[i], 2, 1) == 1) prep_queue(i);
    }
}

/* (the renderer's start, after its own preparation) the containers read before it existed */
static void prep_start(void) {
    InitOnceExecuteOnce(&prep_once, prep_load, NULL, NULL);
    if (!prep.on || !prewarm_table) { prep.on = 0; return; }
    InterlockedExchange(&prep.ready, 1);
    for (u32 i = 0; i < prep.ncont; i++)
        if (InterlockedCompareExchange(&prep.state[i], 2, 1) == 1) prep_queue(i);
}

/* (gfx_present, before the swap chain's Present) hold a loading screen until what it loaded is ready */
static void prep_barrier(void) {
    static u64 last_dispatched;
    u64 dispatched = gfx_stat_dispatched - last_dispatched;
    last_dispatched = gfx_stat_dispatched;
    if (!prep.on || !prep.barrier || dispatched) return;   /* (a fight: never held) */
    if (InterlockedCompareExchange(&prewarm_done, 0, 0) >= prewarm_total) return;
    u64 start = GetTickCount64(), deadline = start + 20000;
    while (InterlockedCompareExchange(&prewarm_done, 0, 0) < prewarm_total && GetTickCount64() < deadline) Sleep(2);
    int timed_out = InterlockedCompareExchange(&prewarm_done, 0, 0) < prewarm_total;
#ifdef _WIN32
    /* then DXVK's own workers: the pipeline libraries of what was just made (quiet for 250 ms,
     * at most 5 s: from the warm driver cache they take milliseconds each) */
    u64 previous = 0, idle_since = 0, quiet_deadline = GetTickCount64() + 5000;
    if (quiet_deadline > deadline) quiet_deadline = deadline;
    while (!timed_out && GetTickCount64() < quiet_deadline) {
        u64 cpu, ids, now = GetTickCount64();
        unsigned count;
        if (!prewarm_dxvk_activity(&cpu, &ids, &count)) break;
        if (previous && cpu - previous <= 1000) { if (!idle_since) idle_since = now; if (now - idle_since >= 250) break; }
        else idle_since = 0;
        previous = cpu;
        Sleep(25);
    }
#endif
    u64 held = GetTickCount64() - start;
    InterlockedExchangeAdd64((volatile LONG64 *)&prep.held_ns, (LONG64)(held * 1000000ull));
    prep.held_ms += held;
    prep.held_frames++;
    if (held > prep.longest_ms) prep.longest_ms = held;
    if (timed_out) {
        prep.timeouts++;
        fprintf(stderr, "[gfx] loading barrier: TIME-OUT after 20 s with %ld shaders still being made; presenting\n",
                (long)(prewarm_total - prewarm_done));
    }
    else if (held >= 50) fprintf(stderr, "[gfx] loading barrier: a loading frame held %llu ms until its containers' shaders were ready\n",
                                  (unsigned long long)held);
}

/* One line for the statistics log. */
static void prep_report(void) {
    if (!prep.on) return;
    fprintf(stderr, "[stats] container preparation: %lld containers read, %lld shaders queued, %lld large ones left to the start-up list; loading barrier held %llu frames, %llu ms (longest %llu), %llu time-outs%s\n",
            (long long)prep.announced, (long long)prep.queued, (long long)prep.skipped_large, (unsigned long long)prep.held_frames, (unsigned long long)prep.held_ms,
            (unsigned long long)prep.longest_ms, (unsigned long long)prep.timeouts, prep.skipped_budget ? "; memory budget reached" : "");
}
