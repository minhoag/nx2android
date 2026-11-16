// Offline hook compatibility: the manifest binds pre-rewritten gates/trampolines
// to verified module builds. No runtime executable-memory patching is performed.
#include "dm.h"
#include <errno.h>
#include <stdatomic.h>

#define COMPAT_MAX 64
#define COMPAT_CALLBACKS 8
static struct compat_hook {
    module_t *module;
    uint64_t offset, trampoline, allowed[COMPAT_CALLBACKS];
    _Atomic uint64_t selected;
    int inlined, nallowed;
} hooks[COMPAT_MAX];
static struct { module_t *module; uint64_t offset; int kind; } operations[COMPAT_MAX];
static struct { module_t *module; uint64_t offset, value; unsigned width; } writes[COMPAT_MAX];
static int nhooks, noperations, nwrites, ready, initialized;
static unsigned char referenced[MAX_MOD], verified[MAX_MOD];

static module_t *compat_module(const char *name) {
    for (int i = 0; i < g_nmod; i++) if (!strcmp(g_mod[i].name, name)) {
        referenced[i] = 1;
        return &g_mod[i];
    }
    die("compatibility manifest references unknown module %s", name);
}
static uint64_t number(const char *s) {
    char *end; errno = 0;
    unsigned long long n = strtoull(s, &end, 0);
    if (errno || !*s || *s == '-' || *end) die("invalid compatibility number %s", s);
    return n;
}
static void compat_range(module_t *m, uint64_t offset, size_t length) {
    if (offset > m->size || length > m->size - offset)
        die("compatibility address outside %s", m->name);
}
static uint64_t compat_address(const char *name, const char *offset) {
    module_t *m = compat_module(name); uint64_t o = number(offset);
    compat_range(m, o, 4);
    return (uint64_t)m->base + o;
}

void compat_load(const char *dir) {
    char path[1024], line[1024];
    snprintf(path, sizeof path, "%s/compat.tsv", dir);
    FILE *f = fopen(path, "r");
    if (!f) {
        if (errno != ENOENT) die("cannot read compatibility manifest %s", path);
        return;   // ordinary ports have no compatibility gates
    }
    if (ready) die("compatibility manifest loaded twice");
    while (fgets(line, sizeof line, f)) {
        char *v[8], *save = NULL; unsigned n = 0;
        for (char *p = strtok_r(line, " \t\r\n", &save); p; p = strtok_r(NULL, " \t\r\n", &save)) {
            if (n == 8) die("too many compatibility manifest columns");
            v[n++] = p;
        }
        if (!n || v[0][0] == '#') continue;
        if (n == 3 && !strcmp(v[0], "build")) {
            module_t *m = compat_module(v[1]);
            if (!m->build_id[0] || strcmp(m->build_id, v[2])) die("compatibility build mismatch for %s", m->name);
            verified[m - g_mod] = 1;
        } else if (n == 5 && !strcmp(v[0], "hook")) {
            uint64_t id = number(v[1]);
            if (id >= COMPAT_MAX || hooks[id].module) die("invalid compatibility hook ID");
            struct compat_hook *h = &hooks[id];
            h->module = compat_module(v[2]); h->offset = number(v[3]);
            compat_range(h->module, h->offset, 4);
            if (strcmp(v[4], "function") && strcmp(v[4], "inline")) die("invalid compatibility hook type");
            h->inlined = !strcmp(v[4], "inline");
            if ((int)id >= nhooks) nhooks = (int)id + 1;
        } else if (n == 4 && !strcmp(v[0], "callback")) {
            uint64_t id = number(v[1]);
            if (id >= COMPAT_MAX || hooks[id].nallowed == COMPAT_CALLBACKS) die("invalid compatibility callback ID");
            hooks[id].allowed[hooks[id].nallowed++] = compat_address(v[2], v[3]);
        } else if (n == 4 && !strcmp(v[0], "operation")) {
            if (noperations == COMPAT_MAX) die("too many compatibility operations");
            module_t *m = compat_module(v[1]); uint64_t o = number(v[2]), kind = number(v[3]);
            compat_range(m, o, 4);
            if (kind < 1 || kind > 4) die("invalid compatibility operation");
            operations[noperations].module = m; operations[noperations].offset = o;
            operations[noperations++].kind = (int)kind;
        } else if ((n == 5 && !strcmp(v[0], "write")) || (n == 4 && !strcmp(v[0], "base"))) {
            if (nwrites == COMPAT_MAX) die("too many compatibility writes");
            module_t *m = compat_module(v[1]); uint64_t o = number(v[2]);
            uint64_t width = n == 5 ? number(v[3]) : 8;
            if (width != 4 && width != 8) die("invalid compatibility write width");
            compat_range(m, o, (size_t)width);
            if (o < m->data_off || (m->key_page_off && o + width > m->key_page_off))
                die("compatibility write outside module data");
            writes[nwrites].module = m; writes[nwrites].offset = o; writes[nwrites].width = (unsigned)width;
            writes[nwrites++].value = n == 5 ? number(v[4]) : (uint64_t)compat_module(v[3])->base;
        } else die("invalid compatibility manifest row");
    }
    if (ferror(f)) die("error reading compatibility manifest");
    fclose(f);
    for (int i = 0; i < g_nmod; i++) {
        if (referenced[i] && !verified[i]) die("compatibility manifest references unverified module %s", g_mod[i].name);
        snprintf(path, sizeof path, "%s/%s.compat.tsv", dir, g_mod[i].name);
        f = fopen(path, "r");
        if (!f) { if (errno != ENOENT) die("cannot read compatibility trampolines"); continue; }
        while (fgets(line, sizeof line, f)) {
            unsigned id; unsigned long long offset; char extra;
            if (sscanf(line, "%u %llx %c", &id, &offset, &extra) != 2 || id >= (unsigned)nhooks ||
                hooks[id].module != &g_mod[i] || hooks[id].trampoline) die("invalid compatibility trampoline");
            compat_range(&g_mod[i], offset, 4);
            hooks[id].trampoline = (uint64_t)g_mod[i].base + offset;
        }
        if (ferror(f)) die("error reading compatibility trampolines");
        fclose(f);
    }
    for (int i = 0; i < nhooks; i++)
        if (!hooks[i].module || !hooks[i].trampoline || !hooks[i].nallowed) die("incomplete compatibility hook %d", i);
    ready = 1;
    LOG("offline compatibility: %d hooks, %d operations validated", nhooks, noperations);
}

extern uint64_t dm_compat_inline_call(uint64_t callback, uint64_t *context, uint64_t flags);
uint64_t compat_dispatch(int hook, int arg, uint64_t ret, uint64_t orig, uint64_t *regs) {
    if (!ready) die("compatibility gate without a validated manifest");
    module_t *m = module_of((void *)ret);
    if (hook) {
        if (arg < 0 || arg >= nhooks || hooks[arg].module != m || hooks[arg].offset != orig)
            die("mismatched compatibility hook gate");
        struct compat_hook *h = &hooks[arg]; uint64_t cb = atomic_load(&h->selected);
        if (!h->inlined) regs[16] = cb ? cb : h->trampoline;
        else if (cb) {
            uint64_t context[30]; memcpy(context, regs, sizeof context);
            context[18] = (uint64_t)dm_tsd_get(g_key18);
            regs[96] = dm_compat_inline_call(cb, context, regs[96]);
            for (int i = 0; i < 30; i++) if (i != 18) regs[i] = context[i];
            dm_tsd_set(g_key18, (void *)context[18]);
            regs[30] = ret + 16;   // skip the stub's saved-register restore after an inline callback
        }
        return 0;
    }
    int found = 0;
    for (int i = 0; i < noperations; i++)
        if (operations[i].module == m && operations[i].offset == orig && operations[i].kind == arg) found = 1;
    if (!found) die("mismatched compatibility operation gate");
    if (arg == 4) { regs[0] = guest_ns(); return regs[0]; }
    if (arg == 1) {
        for (int i = 0; i < nwrites; i++) {
            uint8_t *dst = writes[i].module->base + writes[i].offset;
            if (writes[i].width == 4) { uint32_t value = (uint32_t)writes[i].value; memcpy(dst, &value, 4); }
            else memcpy(dst, &writes[i].value, 8);
        }
        initialized = 1;
        LOG("offline compatibility: initialized");
        return 0;
    }
    if (!initialized) die("compatibility installer before initialization");
    for (int i = 0; i < nhooks; i++) {
        struct compat_hook *h = &hooks[i];
        if (regs[0] != (uint64_t)h->module->base + h->offset) continue;
        if (h->inlined != (arg == 3) || (arg == 2 && (uint8_t)regs[2] != 1)) die("unsupported compatibility installer option");
        int allowed = 0;
        for (int k = 0; k < h->nallowed; k++) if (regs[1] == h->allowed[k]) allowed = 1;
        if (!allowed) die("unsupported compatibility callback");
        atomic_store(&h->selected, regs[1]);
        if (arg == 2) regs[0] = h->trampoline;
        LOG("offline compatibility: installed hook %d", i);
        return 0;
    }
    die("unsupported compatibility target %#llx", (unsigned long long)regs[0]);
}
