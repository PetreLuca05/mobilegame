#include "npc_data.h"

#include <SDL3/SDL_stdinc.h>
#include <cJSON.h>
#include <string.h>

#include "core/assets.h"
#include "core/log.h"
#include "rooms.h"

static cJSON *parse_asset(const char *path)
{
    const MgAsset *a = mg_asset_find(path);
    cJSON *root = a ? cJSON_ParseWithLength((const char *)a->data, a->size) : NULL;
    if (!root) {
        mg_log_error("npc: cannot parse %s", path);
    }
    return root;
}

static uint32_t parse_tags(const cJSON *arr, uint32_t *list, int max, int *count)
{
    uint32_t all = 0;
    int n = 0;
    const cJSON *it;
    cJSON_ArrayForEach(it, arr)
    {
        uint32_t t = cJSON_IsString(it) ? slot_tag_from_name(it->valuestring) : 0;
        if (!t) {
            mg_log_error("npc: unknown slot tag %s", cJSON_IsString(it) ? it->valuestring : "?");
            continue;
        }
        all |= t;
        if (list && n < max) {
            list[n++] = t;
        }
    }
    if (count) {
        *count = n;
    }
    return all;
}

static void parse_range(const cJSON *o, const char *key, float *a, float *b)
{
    const cJSON *r = cJSON_GetObjectItemCaseSensitive(o, key);
    if (cJSON_GetArraySize(r) == 2) {
        *a = (float)cJSON_GetArrayItem(r, 0)->valuedouble;
        *b = (float)cJSON_GetArrayItem(r, 1)->valuedouble;
    }
}

static float num_or(const cJSON *o, const char *key, float dflt)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, key);
    return cJSON_IsNumber(v) ? (float)v->valuedouble : dflt;
}

static NpcPose pose_of(const cJSON *o, NpcPose dflt)
{
    const cJSON *p = cJSON_GetObjectItemCaseSensitive(o, "pose");
    if (!cJSON_IsString(p)) {
        return dflt;
    }
    return SDL_strcmp(p->valuestring, "mop") == 0 ? POSE_MOP : SDL_strcmp(p->valuestring, "crouch") == 0 ? POSE_CROUCH : dflt;
}

static void parse_step(const cJSON *j, NpcStep *s)
{
    const char *verb = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(j, "do"));
    verb = verb ? verb : "goto";
    s->tags = parse_tags(cJSON_GetObjectItemCaseSensitive(j, "tags"), NULL, 0, NULL);
    parse_range(j, "wait", &s->wait_min, &s->wait_max);
    s->kind = SDL_strcmp(verb, "sit") == 0     ? STEP_SIT
              : SDL_strcmp(verb, "post") == 0  ? STEP_POST
              : SDL_strcmp(verb, "queue") == 0 ? STEP_QUEUE
              : SDL_strcmp(verb, "leave") == 0 ? STEP_LEAVE
                                               : STEP_GOTO;
    s->pose = s->kind == STEP_SIT ? POSE_SIT : POSE_STAND;
}

static void parse_package(const cJSON *j, NpcPackage *p)
{
    memset(p, 0, sizeof(*p));
    const char *kind = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(j, "kind"));
    kind = kind ? kind : "";
    const cJSON *tags = cJSON_GetObjectItemCaseSensitive(j, "tags");
    p->chance = num_or(j, "chance", 0.0f);
    p->every = num_or(j, "every", 0.0f);
    p->duration = num_or(j, "duration", 0.0f);
    if (SDL_strcmp(kind, "route") == 0) {
        const cJSON *st;
        cJSON_ArrayForEach(st, cJSON_GetObjectItemCaseSensitive(j, "steps"))
        {
            if (p->step_count < NPC_STEPS) {
                parse_step(st, &p->steps[p->step_count++]);
            }
        }
    } else if (SDL_strcmp(kind, "patrol") == 0) {
        uint32_t list[NPC_STEPS];
        int n = 0;
        parse_tags(tags, list, NPC_STEPS, &n);
        float a = 5.0f, b = 10.0f;
        parse_range(j, "dwell", &a, &b);
        p->loop = true;
        p->random = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(j, "random"));
        for (int i = 0; i < n; i++) {
            p->steps[i] = (NpcStep){STEP_POST, pose_of(j, POSE_STAND), list[i], a, b, 0.0f};
        }
        p->step_count = n;
    } else if (SDL_strcmp(kind, "wander") == 0) {
        p->steps[0] = (NpcStep){STEP_WANDER, POSE_STAND, 0, 2.0f, 6.0f, num_or(j, "radius", 6.0f)};
        p->step_count = 1;
        p->loop = true;
    } else if (SDL_strcmp(kind, "escort") == 0) {
        p->steps[0] = (NpcStep){STEP_ESCORT, POSE_STAND, 0, 0.0f, 0.0f, 0.0f};
        p->step_count = 1;
    } else { /* post, sit, bed */
        NpcStepKind k = SDL_strcmp(kind, "bed") == 0 ? STEP_BED : SDL_strcmp(kind, "sit") == 0 ? STEP_SIT : STEP_POST;
        NpcPose pose = k == STEP_BED ? POSE_BED : k == STEP_SIT ? POSE_SIT : POSE_STAND;
        p->steps[0] = (NpcStep){k, pose, parse_tags(tags, NULL, 0, NULL), 0.0f, 0.0f, 0.0f};
        p->step_count = 1;
    }
}

static NpcType type_of(const char *s)
{
    return SDL_strcmp(s, "outpatient") == 0   ? NPC_OUTPATIENT
           : SDL_strcmp(s, "inpatient") == 0  ? NPC_INPATIENT
           : SDL_strcmp(s, "visitor") == 0    ? NPC_VISITOR
           : SDL_strcmp(s, "pedestrian") == 0 ? NPC_PEDESTRIAN
           : SDL_strcmp(s, "bench") == 0      ? NPC_BENCH
                                              : NPC_STAFF;
}

static NpcLookKind look_of(const char *s)
{
    static const char *const names[] = {"civilian", "coat", "doctor", "nurse", "orderly", "security", "gardener", "gown"};
    for (int i = 0; i < 8; i++) {
        if (SDL_strcmp(s, names[i]) == 0) {
            return (NpcLookKind)i;
        }
    }
    return LOOK_CIVILIAN;
}

static int load_names(const cJSON *root, const char *key, const char **out, Arena *perm)
{
    int n = 0;
    const cJSON *it;
    cJSON_ArrayForEach(it, cJSON_GetObjectItemCaseSensitive(root, key))
    {
        if (cJSON_IsString(it) && n < NPC_NAME_POOL) {
            size_t len = SDL_strlen(it->valuestring) + 1;
            char *copy = ARENA_PUSH(perm, char, len);
            if (copy) {
                memcpy(copy, it->valuestring, len);
                out[n++] = copy;
            }
        }
    }
    return n;
}

bool npc_data_load(NpcData *d, Arena *perm)
{
    memset(d, 0, sizeof(*d));
    cJSON *root = parse_asset("assets/npcs.json");
    cJSON *names = parse_asset("assets/npc_names.json");
    if (!root || !names) {
        cJSON_Delete(root);
        cJSON_Delete(names);
        return false;
    }
    const cJSON *ro = cJSON_GetObjectItemCaseSensitive(root, "roster");
    d->staff_per_room = num_or(ro, "staff_per_room", 0.5f);
    d->outpatients_per_exam_room = num_or(ro, "outpatients_per_exam_room", 3.0f);
    d->inpatients_per_bed = num_or(ro, "inpatients_per_bed", 0.7f);
    d->visitors_per_ward = num_or(ro, "visitors_per_ward", 1.0f);
    d->cafe_per_seat = num_or(ro, "cafe_visitors_per_seat", 0.5f);
    d->pedestrians_per_seat = num_or(ro, "pedestrians_per_bench_seat", 1.25f);
    d->waiting_families = (int)num_or(ro, "waiting_families", 1.0f);
    const cJSON *a;
    cJSON_ArrayForEach(a, cJSON_GetObjectItemCaseSensitive(root, "archetypes"))
    {
        const cJSON *en = cJSON_GetObjectItemCaseSensitive(a, "enabled");
        if ((en && cJSON_IsFalse(en)) || d->archetype_count >= NPC_ARCHETYPES) {
            continue; /* reserved rows: data only */
        }
        NpcArchetype *ar = &d->archetypes[d->archetype_count++];
        const char *name = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(a, "name"));
        SDL_snprintf(ar->name, sizeof(ar->name), "%s", name ? name : "?");
        const char *type = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(a, "type"));
        ar->type = type_of(type ? type : "");
        const char *look = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(a, "look"));
        ar->look = look_of(look ? look : "");
        ar->speed = num_or(a, "speed", 1.6f);
        ar->staff_weight = num_or(a, "staff_weight", 0.0f);
        const cJSON *ages = cJSON_GetObjectItemCaseSensitive(a, "age");
        for (int i = 0; i < 3; i++) {
            ar->age[i] = cJSON_GetArraySize(ages) == 3 ? (float)cJSON_GetArrayItem(ages, i)->valuedouble : (i == 1 ? 1.0f : 0.0f);
        }
        const cJSON *st;
        cJSON_ArrayForEach(st, cJSON_GetObjectItemCaseSensitive(a, "storeys"))
        {
            if (ar->storey_count < 4) {
                ar->storeys[ar->storey_count++] = st->valueint;
            }
        }
        const cJSON *pk;
        cJSON_ArrayForEach(pk, cJSON_GetObjectItemCaseSensitive(a, "packages"))
        {
            if (ar->package_count < NPC_PACKAGES) {
                parse_package(pk, &ar->packages[ar->package_count++]);
            }
        }
    }
    d->n_adult = load_names(names, "adult", d->first_adult, perm);
    d->n_child = load_names(names, "child", d->first_child, perm);
    d->n_surname = load_names(names, "surname", d->surname, perm);
    cJSON_Delete(root);
    cJSON_Delete(names);
    mg_log_info("npc: %d archetypes, %d/%d/%d names", d->archetype_count, d->n_adult, d->n_child, d->n_surname);
    return d->archetype_count > 0;
}

int npc_archetype_find(const NpcData *d, const char *name)
{
    for (int i = 0; i < d->archetype_count; i++) {
        if (SDL_strcmp(d->archetypes[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}
