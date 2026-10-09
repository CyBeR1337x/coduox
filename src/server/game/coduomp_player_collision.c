/*
 * NOT_FROM_ORIGINAL_SOURCE: coduox: g_playerCollision policy for
 * player-versus-player movement collision.
 *
 *   1  every player blocks every other player (stock behavior, default)
 *   2  only enemies block; teammates pass through each other
 *   0  no player blocks another player
 *
 * The policy applies only to movement. Body contents are cleared on the
 * affected players for the duration of one player's Pmove and restored
 * immediately afterward, so bullet, melee, explosive, and use traces still
 * see every player. Clients predict with their own copy of the movement
 * code, so they continue to predict a stop when walking into a player the
 * server lets them pass; the server position then corrects them.
 */

#include "coduomp_player_collision.h"

#include "game_functions.h"
#include "game_globals.h"

enum {
    CODUOMP_PLAYER_COLLISION_NONE = 0,
    CODUOMP_PLAYER_COLLISION_ALL = 1,
    CODUOMP_PLAYER_COLLISION_ENEMIES = 2
};

int coduomp_player_collision_ignored(gentity_t *ent, gentity_t *other)
{
    if (ent == other || ent->client == NULL || other->client == NULL) {
        return 0;
    }

    switch (g_playerCollision.integer) {
    case CODUOMP_PLAYER_COLLISION_NONE:
        return 1;
    case CODUOMP_PLAYER_COLLISION_ENEMIES:
        /* OnSameTeam is false for TEAM_FREE, so free-for-all game types keep
         * full collision under this mode. */
        return OnSameTeam(ent, other) != 0;
    default:
        return 0;
    }
}

int coduomp_player_collision_suspend(
    gentity_t *ent, int suspended[CODUOMP_PLAYER_COLLISION_MAX_SUSPENDED])
{
    int count = 0;
    int maxClients = level.maxclients;

    if (g_playerCollision.integer == CODUOMP_PLAYER_COLLISION_ALL) {
        return 0;
    }
    if (maxClients > CODUOMP_PLAYER_COLLISION_MAX_SUSPENDED) {
        maxClients = CODUOMP_PLAYER_COLLISION_MAX_SUSPENDED;
    }

    for (int entityNum = 0; entityNum < maxClients; entityNum++) {
        gentity_t *other = &g_entities[entityNum];

        /* Only the plain live-body contents value is suspended; noclip, dead,
         * anti-stuck, and script-assigned contents are left untouched. */
        if (other->linked == 0 ||
            other->scriptContents != CONTENTS_BODY ||
            coduomp_player_collision_ignored(ent, other) == 0) {
            continue;
        }

        other->scriptContents = 0;
        suspended[count++] = entityNum;
    }

    return count;
}

void coduomp_player_collision_restore(const int *suspended, int count)
{
    for (int i = 0; i < count; i++) {
        gentity_t *other = &g_entities[suspended[i]];

        if (other->scriptContents == 0) {
            other->scriptContents = CONTENTS_BODY;
        }
    }
}
