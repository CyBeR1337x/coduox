#include "server_client_maintenance.h"

#include "qcommon/qcommon_runtime_types.h"
#include "server_client_message.h"
#include "math/q_math.h"

#include <stdint.h>

enum {
    SERVER_MAX_PING = 999,
    SERVER_TIMEOUT_SECONDS_TO_MSEC = 1000,
    SERVER_TIMEOUT_DROP_SCANS = 5
};

extern serverStatic_t svs;
extern cvar_t *sv_maxclients;
extern cvar_t *sv_timeout;
extern cvar_t *sv_zombietime;
extern cvar_t *cl_paused;
extern cvar_t *sv_paused;
/* NOT_FROM_ORIGINAL_SOURCE: coduox: master-only test-client ping
 * presentation. */
extern cvar_t *sv_botRandomPing;

/* NOT_FROM_ORIGINAL_SOURCE: coduox: base pings reported for test clients while
 * sv_botRandomPing is positive; each report adds -15..15 ms. */
static const int16_t coduomp_bot_random_pings[] = {
    83, 115, 61, 146, 164, 37, 43, 141, 99, 152, 113, 92, 48, 54, 134
};

void Com_DPrintf(const char *format, ...);
/*
 * Complete periodic client ping, timeout, and pause-maintenance cluster
 * shared by the Windows client engine and Linux dedicated engine:
 *
 * Function                    Windows       Linux
 * SV_CalcPings                0x00462d50    0x08094cad
 * SV_CheckTimeouts            0x00462ec0    0x08094e0d
 * SV_CheckPaused              0x00462fb0    0x08094f3a
 *
 * The client-state gates, 32-frame ping sample, 999 clamp, timeout/zombie
 * aging, five-scan grace period, and single-connected-client pause rule agree.
 * The supporting Mac client exports the same canonical names.
 */

/* Source: CoDUOMP.exe 0x00462d50..0x00462e54.
 * Evidence: coduomp/mcode/CoDUOMP/FUN_00462d50_00462e55.mcode.
 * Name and source structure: exact same-module Mac symbol SV_CalcPings. */
void SV_CalcPings(void)
{
    client_t *client = svs.clients;
    for (int32_t clientNum = 0;
         clientNum < sv_maxclients->integer;
         ++clientNum, ++client) {
        /* NOT_FROM_ORIGINAL_SOURCE: coduox: with sv_botRandomPing positive,
         * test clients report a new random ping each frame instead of the
         * measured value. irand excludes its upper bound. */
        if (client->isTestClient && sv_botRandomPing->integer > 0) {
            const int32_t pingIndex = irand(
                0, (int32_t)(sizeof(coduomp_bot_random_pings) /
                             sizeof(coduomp_bot_random_pings[0])));
            client->ping =
                coduomp_bot_random_pings[pingIndex] + irand(-15, 16);
            continue;
        }
        if (client->state != CS_ACTIVE || client->gentity == NULL) {
            client->ping = SERVER_MAX_PING;
            continue;
        }

        int32_t pingTotal = 0;
        int32_t pingSamples = 0;
        for (int32_t frameIndex = 0;
             frameIndex < SERVER_CLIENT_SNAPSHOT_FRAME_COUNT;
             ++frameIndex) {
            const clientSnapshot_t *const frame =
                &client->snapshotFrames[frameIndex];
            if (frame->messageAcknowledgedTime > 0) {
                ++pingSamples;
                pingTotal += frame->messageAcknowledgedTime -
                             frame->messageSentTime;
            }
        }

        if (pingSamples == 0) {
            client->ping = SERVER_MAX_PING;
        } else {
            client->ping = pingTotal / pingSamples;
            if (client->ping > SERVER_MAX_PING)
                client->ping = SERVER_MAX_PING;
        }
    }
}

/* Source: CoDUOMP.exe 0x00462ec0..0x00462faf.
 * Evidence: coduomp/mcode/CoDUOMP/FUN_00462ec0_00462fb0.mcode.
 * Name and source structure: exact same-module Mac symbol SV_CheckTimeouts. */
void SV_CheckTimeouts(void)
{
    const int32_t timeoutLimit =
        svs.realTime -
        sv_timeout->integer * SERVER_TIMEOUT_SECONDS_TO_MSEC;
    const int32_t zombieLimit =
        svs.realTime -
        sv_zombietime->integer * SERVER_TIMEOUT_SECONDS_TO_MSEC;

    client_t *client = svs.clients;
    for (int32_t clientNum = 0;
         clientNum < sv_maxclients->integer;
         ++clientNum, ++client) {
        if (client->lastPacketTime > svs.realTime)
            client->lastPacketTime = svs.realTime;

        if (client->isTestClient != qfalse) {
            /* NOT_FROM_ORIGINAL_SOURCE: coduox: a dropped test client leaves
             * CS_ZOMBIE under the same sv_zombietime rule as other clients.
             * Its unique address is never reconnected, so otherwise the slot
             * stays unusable until the server restarts. */
            if (client->state == CS_ZOMBIE &&
                client->lastPacketTime < zombieLimit) {
                Com_DPrintf("Going from CS_ZOMBIE to CS_FREE for %s\n",
                            client->name);
                client->state = CS_FREE;
            }
            continue;
        }

        if (client->state == CS_ZOMBIE &&
            client->lastPacketTime < zombieLimit) {
            Com_DPrintf("Going from CS_ZOMBIE to CS_FREE for %s\n",
                        client->name);
            client->state = CS_FREE;
        } else if (client->state >= CS_CONNECTED &&
                   client->lastPacketTime < timeoutLimit) {
            ++client->timeoutCount;
            if (client->timeoutCount > SERVER_TIMEOUT_DROP_SCANS) {
                SV_DropClient(client, "EXE_TIMEDOUT");
                client->state = CS_FREE;
            }
        } else {
            client->timeoutCount = 0;
        }
    }
}

/* Source: CoDUOMP.exe 0x00462fb0..0x00463004.
 * Evidence: coduomp/mcode/CoDUOMP/FUN_00462fb0_00463005.mcode.
 * Name and source structure: exact same-module Mac symbol SV_CheckPaused. */
qboolean SV_CheckPaused(void)
{
    if (cl_paused->integer == 0)
        return qfalse;

    int32_t connectedClients = 0;
    client_t *client = svs.clients;
    for (int32_t clientNum = 0;
         clientNum < sv_maxclients->integer;
         ++clientNum, ++client) {
        if (client->state >= CS_CONNECTED)
            ++connectedClients;
    }

    if (connectedClients > 1) {
        sv_paused->integer = 0;
        return qfalse;
    }

    sv_paused->integer = 1;
    return qtrue;
}
