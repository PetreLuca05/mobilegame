/* assets/npcs.json + npc_names.json, parsed once at startup. Packages compile
 * to step lists: post/sit/bed are one step, patrol loops, route ends in leave. */
#ifndef MG_NPC_DATA_H
#define MG_NPC_DATA_H

#include "core/arena.h"
#include "core/mg_types.h"

#define NPC_ARCHETYPES 18
#define NPC_PACKAGES 3
#define NPC_STEPS 8
#define NPC_NAME_POOL 64
#define NPC_GREETINGS 8

typedef enum NpcType { NPC_STAFF, NPC_OUTPATIENT, NPC_INPATIENT, NPC_VISITOR, NPC_PEDESTRIAN, NPC_BENCH } NpcType;
typedef enum NpcAge { AGE_CHILD, AGE_ADULT, AGE_ELDER } NpcAge;
typedef enum NpcStepKind { STEP_GOTO, STEP_POST, STEP_SIT, STEP_BED, STEP_QUEUE, STEP_WANDER, STEP_ESCORT, STEP_LEAVE } NpcStepKind;
typedef enum NpcPose { POSE_STAND, POSE_SIT, POSE_BED, POSE_MOP, POSE_CROUCH } NpcPose;
typedef enum NpcLookKind { LOOK_CIVILIAN, LOOK_COAT, LOOK_DOCTOR, LOOK_NURSE, LOOK_ORDERLY, LOOK_SECURITY, LOOK_GARDENER, LOOK_GOWN } NpcLookKind;

typedef struct NpcStep {
    NpcStepKind kind;
    NpcPose pose;      /* while holding */
    uint32_t tags;
    float wait_min, wait_max; /* 0 = until something else takes over */
    float radius;      /* WANDER */
} NpcStep;

typedef struct NpcPackage {
    NpcStep steps[NPC_STEPS];
    int step_count;
    bool loop, random;
    float chance, every, duration; /* every > 0: an occasional interruption */
} NpcPackage;

typedef struct NpcArchetype {
    char name[24];
    NpcType type;
    float age[3];      /* child, adult, elder weights */
    float speed;
    NpcLookKind look;
    float staff_weight;
    int storeys[4];
    int storey_count;
    NpcPackage packages[NPC_PACKAGES];
    int package_count;
} NpcArchetype;

typedef struct NpcData {
    NpcArchetype archetypes[NPC_ARCHETYPES];
    int archetype_count;
    float staff_per_room, outpatients_per_exam_room, inpatients_per_bed, visitors_per_ward, cafe_per_seat, pedestrians_per_seat;
    int waiting_families;
    const char *first_adult[NPC_NAME_POOL], *first_child[NPC_NAME_POOL], *surname[NPC_NAME_POOL];
    int n_adult, n_child, n_surname;
    const char *greetings[NPC_ARCHETYPES][NPC_GREETINGS]; /* by archetype */
    int n_greetings[NPC_ARCHETYPES];
} NpcData;

bool npc_data_load(NpcData *d, Arena *perm);
int npc_archetype_find(const NpcData *d, const char *name); /* -1 if none */

#endif
