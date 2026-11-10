/* --avatar-gallery NAME (desktop test flag, and --autotest avatar): extra avatars posed at fixed places in the
 * hospital, for screenshots of the LIB-1 avatar (docs/autotest/avatar/). Drawn after the player; nothing is
 * simulated. Pair each with --spawn (the view floor) and --cam (tools/avatar_test.sh has both).
 *   options  every creator option on a lawn in the yard: hair styles, suits, extras, skins, hair colours, bodies
 *            (optionsN: only row N, 1 hair styles .. 6 bodies, for close-ups)
 *   ages     child, adult, elder side by side, plus the NPC looks
 *   seats    the reception chair and a waiting-area chair (G), the yard bench
 *   bed      a ward bed (storey 1)
 *   poses    idle, walk, browse, push, creator look, crouch, mop, talk pop, stretch
 *   subs     the states A1 maps by substitution (docs/AVATAR.md): crouch, mop sway, weight shift, talk pop, seated
 *            stretch, airborne hands, elder, child, held still for side-by-side pictures with the old avatar */
#ifndef MG_AVATAR_GALLERY_H
#define MG_AVATAR_GALLERY_H

#include "avatar.h"

#define GALLERY_MAX 48

typedef enum { GALLERY_OFF, GALLERY_OPTIONS, GALLERY_AGES, GALLERY_SEATS, GALLERY_BED, GALLERY_POSES, GALLERY_SUBS } GalleryMode;

typedef struct AvatarGallery {
    GalleryMode mode;
    int row; /* options: 1..6 draws only that row (1 = hair styles .. 6 = bodies), 0 all */
    AvatarAnim anim[GALLERY_MAX];
} AvatarGallery;

GalleryMode avatar_gallery_parse(const char *name, int *row);
void avatar_gallery_draw(AvatarGallery *gal, GfxContext *g, const AvatarMeshes *m, float dt);

#endif
