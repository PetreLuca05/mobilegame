/* --avatar-gallery NAME (desktop test flag, and --autotest avatar): extra avatars posed at fixed places in the
 * hospital, for screenshots of the LIB-1 avatar (docs/autotest/avatar/). Drawn after the player; nothing is
 * simulated. Pair each with --spawn (the view floor) and --cam (tools/avatar_test.sh has both).
 *   options  every creator option on a lawn in the yard: hair styles, suits, extras, skins, hair colours, bodies
 *   ages     child, adult, elder side by side, plus the NPC looks
 *   seats    the reception chair and a waiting-area chair (G), the yard bench
 *   bed      a ward bed (storey 1)
 *   poses    idle, walk, browse, push, creator look, crouch, mop, talk pop, stretch */
#ifndef MG_AVATAR_GALLERY_H
#define MG_AVATAR_GALLERY_H

#include "avatar.h"

#define GALLERY_MAX 48

typedef enum { GALLERY_OFF, GALLERY_OPTIONS, GALLERY_AGES, GALLERY_SEATS, GALLERY_BED, GALLERY_POSES } GalleryMode;

typedef struct AvatarGallery {
    GalleryMode mode;
    AvatarAnim anim[GALLERY_MAX];
} AvatarGallery;

GalleryMode avatar_gallery_parse(const char *name);
void avatar_gallery_draw(AvatarGallery *gal, GfxContext *g, const AvatarMeshes *m, float dt);

#endif
