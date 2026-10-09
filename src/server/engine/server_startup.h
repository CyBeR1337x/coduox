#ifndef SHARED_SERVER_STARTUP_H
#define SHARED_SERVER_STARTUP_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void SV_CreateBaseline(void);
void SV_BoundMaxClients(int32_t minimumClientCount);
void SV_Startup(void);
void SV_ChangeMaxClients(void);
void SV_SetExpectedHunkUsage(const char *mapBspPath);
void SV_ClearServer(void);
void SV_InitCvar(void);
void SV_SpawnServer(const char *serverName);
/* NOT_FROM_ORIGINAL_SOURCE: coduox: drops and frees every test client while
 * the outgoing game module is loaded; reason labels the console report. */
void coduomp_sv_drop_all_test_clients(const char *reason);

#ifdef __cplusplus
}
#endif

#endif
