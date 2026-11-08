/* Character creator panel (M4): tabs Body / Skin / Hair / Suit / Extras,
 * each a row (or two) of 56 pt tiles that scroll sideways. Edits a NetSkin
 * in place; every tap applies at once (live preview in the front end). */
#ifndef MG_CREATOR_H
#define MG_CREATOR_H

#include "net/net_transport.h"
#include "ui/widgets.h"

typedef struct Creator {
    int tab;
    UiScroll rows[2];
} Creator;

/* Returns true when the skin changed this frame. */
bool creator_ui(Creator *c, Ui *ui, NetSkin *skin, float x, float y, float w, float h);
/* Every field at random (the dice button). */
void creator_randomise(NetSkin *skin);

#endif
