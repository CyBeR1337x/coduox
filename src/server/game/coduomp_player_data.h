#ifndef CODUOMP_PLAYER_DATA_H
#define CODUOMP_PLAYER_DATA_H

#include <stdint.h>

/* NOT_FROM_ORIGINAL_SOURCE: coduox: setKey, getKey, and deleteKey player
 * methods. */
void coduomp_player_cmd_setkey(uint32_t entityNum);
void coduomp_player_cmd_getkey(uint32_t entityNum);
void coduomp_player_cmd_deletekey(uint32_t entityNum);

/* NOT_FROM_ORIGINAL_SOURCE: coduox: discards one client's in-memory store;
 * called when the client disconnects. Persistent values are already on
 * disk. */
void coduomp_player_data_clear(int clientNum);

/* NOT_FROM_ORIGINAL_SOURCE: coduox: discards every in-memory store; called
 * when the game shuts down or restarts. */
void coduomp_player_data_clear_all(void);

#endif
