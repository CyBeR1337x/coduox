/*
 * NOT_FROM_ORIGINAL_SOURCE: coduox: client country lookup in a GeoIP legacy
 * country database.
 *
 * GeoIP.dat is read from the game search path on each lookup; nothing is
 * cached, so a replaced file takes effect on the next lookup. The file is a
 * binary tree of 6-byte nodes. Each node holds two 24-bit little-endian
 * records, the left one followed for a 0 address bit and the right one for
 * a 1 bit, walking from the most significant bit. A record below
 * CODUOMP_GEOIP_COUNTRY_BEGIN is the next node number; any other record is
 * CODUOMP_GEOIP_COUNTRY_BEGIN plus the country index.
 *
 * Every read is bounded by the opened file's length, and the walk visits at
 * most one node per address bit, so a malformed file ends the lookup with
 * index 0 instead of reading outside the file or looping.
 */

#include "coduomp_geoip.h"

#include "filesystem/filesystem.h"
#include "qcommon/filesystem_types.h"
#include "qcommon/net_types.h"
#include "qcommon/server_runtime_types.h"

void Com_Printf(const char *format, ...);

extern serverStatic_t svs;
extern cvar_t *sv_maxclients;

#define CODUOMP_GEOIP_FILE "GeoIP.dat"

enum {
    CODUOMP_GEOIP_COUNTRY_BEGIN = 16776960,
    CODUOMP_GEOIP_COUNTRY_COUNT = 256,
    CODUOMP_GEOIP_RECORD_SIZE = 3,
    CODUOMP_GEOIP_NODE_SIZE = 2 * CODUOMP_GEOIP_RECORD_SIZE,
    CODUOMP_GEOIP_ADDRESS_BITS = 32,
    /* A database other than the country edition ends with a structure
     * record: three 0xff bytes then an edition byte, at most this many
     * bytes before the end of the file. */
    CODUOMP_GEOIP_STRUCTURE_INFO_MAX = 20,
    CODUOMP_GEOIP_STRUCTURE_MARKER_SIZE = 3,
    CODUOMP_GEOIP_EDITION_COUNTRY = 1,
    /* Newer files store the edition biased by 105. */
    CODUOMP_GEOIP_EDITION_BIAS = 105
};

/* Reads exactly size bytes at offset; qfalse when they are not all inside
 * the file or cannot be read. */
static qboolean coduomp_geoip_read_at(int32_t handle, int32_t fileLength,
                                      int32_t offset, uint8_t *buffer,
                                      int32_t size)
{
    if (offset < 0 || size < 0 || offset > fileLength - size) {
        return qfalse;
    }
    if (FS_Seek(handle, offset, FS_SEEK_ORIGIN_SET) != 0) {
        return qfalse;
    }
    return FS_Read(buffer, size, handle) == size ? qtrue : qfalse;
}

/* qtrue when the open file is a country-edition database: either it has no
 * structure record or the record names the country edition. */
static qboolean coduomp_geoip_is_country_edition(int32_t handle,
                                                 int32_t fileLength)
{
    for (int32_t back = 0; back < CODUOMP_GEOIP_STRUCTURE_INFO_MAX; ++back) {
        uint8_t record[CODUOMP_GEOIP_STRUCTURE_MARKER_SIZE + 1];
        const int32_t offset = fileLength - (int32_t)sizeof(record) - back;

        if (coduomp_geoip_read_at(handle, fileLength, offset, record,
                                  (int32_t)sizeof(record)) == qfalse) {
            break;
        }
        if (record[0] == 0xffu && record[1] == 0xffu && record[2] == 0xffu) {
            int32_t edition = record[CODUOMP_GEOIP_STRUCTURE_MARKER_SIZE];

            if (edition > CODUOMP_GEOIP_EDITION_BIAS) {
                edition -= CODUOMP_GEOIP_EDITION_BIAS;
            }
            return edition == CODUOMP_GEOIP_EDITION_COUNTRY ? qtrue : qfalse;
        }
    }
    return qtrue;
}

static int32_t coduomp_geoip_seek_country(int32_t handle, int32_t fileLength,
                                          uint32_t address)
{
    uint32_t node = 0;

    for (int32_t bit = CODUOMP_GEOIP_ADDRESS_BITS - 1; bit >= 0; --bit) {
        uint8_t records[CODUOMP_GEOIP_NODE_SIZE];
        const uint8_t *record = records;

        if (node > (uint32_t)(INT32_MAX / CODUOMP_GEOIP_NODE_SIZE) ||
            coduomp_geoip_read_at(
                handle, fileLength,
                (int32_t)node * CODUOMP_GEOIP_NODE_SIZE, records,
                (int32_t)sizeof(records)) == qfalse) {
            return 0;
        }

        if ((address & ((uint32_t)1 << bit)) != 0) {
            record += CODUOMP_GEOIP_RECORD_SIZE;
        }
        node = (uint32_t)record[0] | ((uint32_t)record[1] << 8) |
               ((uint32_t)record[2] << 16);

        if (node >= (uint32_t)CODUOMP_GEOIP_COUNTRY_BEGIN) {
            const uint32_t country =
                node - (uint32_t)CODUOMP_GEOIP_COUNTRY_BEGIN;

            return country < (uint32_t)CODUOMP_GEOIP_COUNTRY_COUNT
                       ? (int32_t)country
                       : 0;
        }
    }

    return 0;
}

int32_t coduomp_sv_client_country_index(int32_t clientNum)
{
    /* One console line per problem per server process, not per lookup. */
    static qboolean missingReported;
    static qboolean editionReported;

    if (clientNum < 0 || clientNum >= sv_maxclients->integer) {
        return 0;
    }

    const client_t *const client = &svs.clients[clientNum];
    const netadr_t address = client->netchan.remoteAddress;
    if (client->state == CS_FREE || address.type != NA_IP) {
        return 0;
    }

    int32_t handle = 0;
    const int32_t fileLength =
        FS_FOpenFileRead(CODUOMP_GEOIP_FILE, &handle, qfalse);
    if (handle == 0) {
        if (missingReported == qfalse) {
            missingReported = qtrue;
            Com_Printf(CODUOMP_GEOIP_FILE
                       " not found; client countries are unknown\n");
        }
        return 0;
    }

    int32_t country = 0;
    if (fileLength < CODUOMP_GEOIP_NODE_SIZE ||
        coduomp_geoip_is_country_edition(handle, fileLength) == qfalse) {
        if (editionReported == qfalse) {
            editionReported = qtrue;
            Com_Printf(CODUOMP_GEOIP_FILE
                       " is not a GeoIP legacy country database; client "
                       "countries are unknown\n");
        }
    } else {
        country = coduomp_geoip_seek_country(
            handle, fileLength,
            ((uint32_t)address.ip[0] << 24) | ((uint32_t)address.ip[1] << 16) |
                ((uint32_t)address.ip[2] << 8) | (uint32_t)address.ip[3]);
    }

    FS_FCloseFile(handle);
    return country;
}
