/* Elevator floor menu: a 180 x 200 pt frosted black panel in the top-right
 * corner (under the settings gear), one 56 pt row per storey from the top
 * floor down; the current storey is highlighted and cannot be tapped. */
#ifndef MG_FLOOR_MENU_H
#define MG_FLOOR_MENU_H

#include "ui/widgets.h"

/* Returns the storey tapped this frame, or -1. */
int floor_menu(Ui *ui, float w_pt, float safe_t, float safe_r, int floors, int current,
               const char *(*name)(int floor));

#endif
