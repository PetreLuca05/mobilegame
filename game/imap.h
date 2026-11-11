/* MAP-2: the institute map as compiled by tools/mapc.py (institute.map -> assets/world/institute.mapb, embedded).
 * Fixed-size little-endian records read in place; imap_open checks the header, every table's bounds and record
 * size, every string offset and cross-reference, once at load. No text parsing at run time.
 * Coordinates are the map's: metres, x east, z south (= the engine's +Z, north = -Z), y = storey * 4 m. */
#ifndef MG_IMAP_H
#define MG_IMAP_H

#include "core/mg_types.h"

#define IMAP_NONE 0xFFFFu
#define IMAP_ROOFS 4 /* storey index of the roof deck (lift landing R) */

/* room flags (mapc computes them) */
enum {
    IMAP_RF_OPEN = 1,      /* no walls (the atrium lobby) */
    IMAP_RF_ENTRANCE = 2,  /* main entrance on the south facade */
    IMAP_RF_AMBULANCE = 4, /* ambulance door on the east facade */
    IMAP_RF_OPEN_N = 8,    /* north side open to the corridor */
    IMAP_RF_STAIRS = 16,   /* the stairwell */
    IMAP_RF_LOCKER = 32,   /* staff lockers (a store with windows) */
};

typedef enum ImRoomType {
    IM_EXAM, IM_LAB, IM_WARD_ROOM, IM_ICU_BAY, IM_OR, IM_SCRUB, IM_STERILE, IM_RECOVERY, IM_PHARMACY, IM_MEETING,
    IM_OFFICE, IM_RECEPTION, IM_TREATMENT_BAY, IM_NURSE_STATION, IM_TOILET, IM_STORE, IM_TERRACE, IM_IMAGING,
    IM_WAITING, IM_TRIAGE, IM_ROOM_TYPE_COUNT
} ImRoomType;

/* furniture kinds: same order as FURN_KINDS in tools/mapc.py (imap_open compares the names) */
#define IM_FURN_KINDS(X)                                                                                                   \
    X(ALCOVE_SEAT, "alcove_seat") X(ANAESTHESIA_CART, "anaesthesia_cart") X(ANATOMY_MODEL, "anatomy_model")              \
    X(AQUARIUM, "aquarium") X(AUDITORIUM_SEAT, "auditorium_seat") X(BED, "bed") X(BED_CURTAIN, "bed_curtain")             \
    X(BENCH_SEAT, "bench_seat") X(BIN, "bin") X(CABINET, "cabinet") X(CENTRIFUGE, "centrifuge") X(CHAIR, "chair")         \
    X(CLOCK, "clock") X(COATS, "coats") X(CONFERENCE_TABLE, "conference_table") X(COOLER, "cooler")                       \
    X(CORRIDOR_BENCH, "corridor_bench") X(CORRIDOR_CHAIR, "corridor_chair") X(CORRIDOR_PLANT, "corridor_plant")           \
    X(COUNTER_SCREEN, "counter_screen") X(CRASH, "crash") X(CT_SCANNER, "ct_scanner") X(CUPBOARD, "cupboard")             \
    X(DENTAL, "dental") X(DESK, "desk") X(DESK_PAPERS, "desk_papers") X(DESK_SCREEN, "desk_screen")                       \
    X(DISPENSING_COUNTER, "dispensing_counter") X(DOOR_LEAF, "door_leaf") X(EXAM_COUCH, "exam_couch")                     \
    X(EXAM_DESK, "exam_desk") X(EXTINGUISHER, "extinguisher") X(FRIDGE, "fridge") X(GURNEY, "gurney")                     \
    X(INFO_SCREEN, "info_screen") X(INFUSION_CHAIR, "infusion_chair") X(INSTRUMENT_TABLE, "instrument_table")             \
    X(IV_POLE, "iv_pole") X(LAB_BENCH, "lab_bench") X(LAB_ISLAND, "lab_island") X(LECTERN, "lectern")                     \
    X(LIFT_SIGN, "lift_sign") X(LIGHTBOX_STAND, "lightbox_stand") X(LOCKER, "locker") X(MONITOR, "monitor")               \
    X(NOTICE_POSTER, "notice_poster") X(NURSE_COUNTER, "nurse_counter") X(OR_LAMP, "or_lamp") X(OR_TABLE, "or_table")     \
    X(OXYGEN, "oxygen") X(PALM, "palm") X(PHARMACY_SIGN, "pharmacy_sign") X(PLANT, "plant") X(PLANTER, "planter")         \
    X(PLANTER_TREE, "planter_tree") X(PODIUM, "podium") X(PRINTER, "printer") X(PRIVACY_SCREEN, "privacy_screen")         \
    X(READING_DESK, "reading_desk") X(RECEPTION_DESK, "reception_desk") X(RECLINER, "recliner") X(RISER, "riser")         \
    X(RUG, "rug") X(SCALE, "scale") X(SCOPE_CABINET, "scope_cabinet") X(SCRUB_SINK, "scrub_sink") X(SHELF, "shelf")       \
    X(SIDE_TABLE, "side_table") X(SINK, "sink") X(SOFA, "sofa") X(STAIRS, "stairs")                                       \
    X(STATION_COUNTER, "station_counter") X(SUPPLY_CART, "supply_cart") X(TALL_CABINET, "tall_cabinet")                   \
    X(TROLLEY_TOWER, "trolley_tower") X(TROPHY, "trophy") X(ULTRASOUND, "ultrasound") X(VENDING, "vending")               \
    X(WALL_POSTER, "wall_poster") X(WALL_SCREEN, "wall_screen") X(WC_BASIN, "wc_basin") X(WC_CUBICLE, "wc_cubicle")       \
    X(WHITEBOARD_STAND, "whiteboard_stand")
#define X(e, s) IMF_##e,
typedef enum ImFurnKind { IM_FURN_KINDS(X) IM_FURN_KIND_COUNT } ImFurnKind;
#undef X

/* book prop kinds (prop_kinds.json) */
typedef enum ImIPropKind {
    IMP_POSTER, IMP_PLAQUE, IMP_BOARD, IMP_LIGHTBOX, IMP_SHELF, IMP_CABINET, IMP_COUNTER, IMP_BENCH, IMP_CART,
    IMP_MONITOR, IMP_SCOPE_TOWER, IMP_MODEL, IMP_PODIUM, IMP_MICROSCOPE, IMP_IV_STAND, IMP_KIND_COUNT
} ImIPropKind;

typedef enum ImSlotKind {
    IMS_DESK, IMS_CHAIR, IMS_BENCH, IMS_SCREEN, IMS_BOARD, IMS_COUNTER, IMS_BED, IMS_OR_TABLE, IMS_POST, IMS_SCOPE,
    IMS_TABLE, IMS_READING_DESK, IMS_KIND_COUNT
} ImSlotKind;

enum { IM_MOUNT_FREE, IM_MOUNT_WALL, IM_MOUNT_FLOOR, IM_MOUNT_DOOR };
enum { IM_SITE_PLOT, IM_SITE_GATE, IM_SITE_ROAD, IM_SITE_CARPARK, IM_SITE_PATH, IM_SITE_POND, IM_SITE_TREE,
       IM_SITE_BENCH, IM_SITE_LAMP, IM_SITE_AMBULANCE, IM_SITE_HELIPAD };
enum { IM_CORR_RING, IM_CORR_ALCOVE, IM_CORR_DECK };

typedef struct ImFloor {
    int32_t storey;
    uint32_t name;
} ImFloor;

typedef struct ImRoom {
    uint32_t id, name, type_name, role; /* strings; role "" = none (garda, round, boss, ...) */
    float x0, z0, x1, z1;
    int8_t storey;
    uint8_t type;   /* ImRoomType */
    uint16_t flags; /* IMAP_RF_* */
    int8_t chapter; /* of its first topic, -1 none (door plaque colour) */
    uint8_t pad0;
    uint16_t first_door, door_count; /* first_door IMAP_NONE if none; the first is the room's main door */
    uint16_t pad1;
} ImRoom;

typedef struct ImDoor { /* also windows (informative: the scene places windows by the facade rule) */
    float x, z, w;  /* centre on the wall line, width */
    int16_t room;
    int8_t storey;
    uint8_t side;   /* 'N' 'S' 'E' 'W' */
} ImDoor;

typedef struct ImRect {
    float x0, z0, x1, z1;
    int8_t storey;
    uint8_t kind; /* corridors: IM_CORR_* */
    uint16_t pad;
} ImRect;

typedef struct ImLift {
    uint32_t name;
    float x, z;     /* car centre */
    uint8_t landing_mask, landing_count; /* bit s = storey s (4 = the roof) */
    uint8_t bed, pad;
} ImLift;

typedef struct ImPoint {
    float x, z;
} ImPoint;

typedef struct ImSlot {
    float x, z, yaw; /* yaw degrees, 0 faces +z (south), 90 east */
    int16_t room;
    int8_t storey;
    uint8_t kind; /* ImSlotKind */
} ImSlot;

typedef struct ImISlot {
    uint32_t id;
    float x, z, yaw;
    int16_t room;
    int8_t storey;
    uint8_t kind; /* 0 stand, 1 seat */
    int16_t furniture; /* on_model row, -1 none */
    uint16_t pad;
} ImISlot;

typedef struct ImIProp {
    uint32_t id, label;
    float x, z, yaw;
    int16_t room;
    int8_t storey;
    uint8_t kind;      /* ImIPropKind */
    int16_t furniture; /* on_model: the map furniture already there, no new mesh; -1 none */
    uint16_t pad;
} ImIProp;

typedef struct ImProp {
    uint32_t id;
    float x, z;        /* footprint centre (door_leaf: the hinge) */
    float yaw;         /* 0 faces +z; the room's door side: S 0, E 90, N 180, W 270 */
    float w, d, h;     /* w across the facing direction, d along it, h top above the floor */
    float open_x, open_z; /* door_leaf: the side it swings to */
    int16_t room;      /* -1: corridor */
    int8_t storey;
    uint8_t kind;      /* ImFurnKind */
    uint8_t mount;     /* IM_MOUNT_* */
    uint8_t pad0;
    uint16_t pad1;
} ImProp;

typedef struct ImSite {
    uint8_t kind; /* IM_SITE_* */
    int8_t storey;
    uint8_t side; /* gates */
    uint8_t pad;
    float v[6];   /* plot / road / path: x0 z0 x1 z1; carpark + bays; pond x z r; gate x z w; props x z yaw p q r */
} ImSite;

typedef struct ImSpawn {
    float x, z;
    int8_t storey; /* -1 yard */
    uint8_t pad0;
    uint16_t pad1;
} ImSpawn;

typedef struct ImLearn {
    uint32_t pack, kind, topic; /* strings */
    int16_t room;
    int8_t chapter;
    uint8_t pad;
} ImLearn;

typedef struct IMap {
    const char *strings;
    uint32_t strings_size;
    const ImFloor *floors;
    const ImRoom *rooms;
    const ImDoor *doors, *windows;
    const ImRect *corridors, *voids;
    const ImLift *lifts;
    const ImPoint *stairs;
    const ImSlot *slots;
    const ImISlot *islots;
    const ImIProp *iprops;
    const ImProp *props;
    const ImSite *site;
    const ImSpawn *spawns;
    const ImLearn *learn;
    int floor_count, room_count, door_count, window_count, corridor_count, void_count, lift_count, stairs_count;
    int slot_count, islot_count, iprop_count, prop_count, site_count, spawn_count, learn_count;
} IMap;

/* Checks and points into `blob` (8-byte aligned, as embedded). False (logged) if anything is off. */
bool imap_open(IMap *m, const void *blob, size_t size);
/* The embedded assets/world/institute.mapb. */
bool imap_open_embedded(IMap *m);
static inline const char *imap_str(const IMap *m, uint32_t off) { return off < m->strings_size ? m->strings + off : ""; }
/* Room index by id, -1 if none. */
int imap_room_find(const IMap *m, const char *id);
/* Room containing (x, z) on a storey, -1 if none. */
int imap_room_at(const IMap *m, int storey, float x, float z);
const char *imap_furn_kind_name(int kind);

#endif
