#ifndef CODUOMP_PLAYER_COLLISION_H
#define CODUOMP_PLAYER_COLLISION_H

#include "recovered_game.h"

/* NOT_FROM_ORIGINAL_SOURCE: coduox: upper bound on players whose body contents
 * one movement step can suspend; matches the client entity range. */
#define CODUOMP_PLAYER_COLLISION_MAX_SUSPENDED MAX_CLIENTS

/* NOT_FROM_ORIGINAL_SOURCE: coduox: returns nonzero when g_playerCollision
 * lets the two players pass through each other. Never true for an entity
 * paired with itself or with a non-client entity. */
int coduomp_player_collision_ignored(gentity_t *ent, gentity_t *other);

/* NOT_FROM_ORIGINAL_SOURCE: coduox: clears CONTENTS_BODY on every live player
 * that ent may pass through, records their entity numbers in suspended, and
 * returns the count. The caller must pass the same array and count to
 * coduomp_player_collision_restore before any trace other than ent's own
 * movement runs. */
int coduomp_player_collision_suspend(
    gentity_t *ent, int suspended[CODUOMP_PLAYER_COLLISION_MAX_SUSPENDED]);

/* NOT_FROM_ORIGINAL_SOURCE: coduox: restores CONTENTS_BODY on the recorded
 * players whose contents are still cleared. */
void coduomp_player_collision_restore(const int *suspended, int count);

#endif
