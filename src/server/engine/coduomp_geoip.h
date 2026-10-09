#ifndef CODUOMP_GEOIP_H
#define CODUOMP_GEOIP_H

#include <stdint.h>

/* NOT_FROM_ORIGINAL_SOURCE: coduox: returns the GeoIP country index, 1..255,
 * for the IPv4 address of the client in slot clientNum, read from the
 * GeoIP.dat legacy country database in the game search path. Returns 0 when
 * the slot is out of range or free, the client has no IPv4 address, the
 * database is missing, is not a country database, or is malformed, or the
 * address is not listed. */
int32_t coduomp_sv_client_country_index(int32_t clientNum);

#endif
