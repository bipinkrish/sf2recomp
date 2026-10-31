// Game network policy. Keep the checks ahead of any future host transport.
#pragma once
#include "cpu.h"

typedef struct IpcReq IpcReq;
typedef struct IpcResp IpcResp;

int network_hook(Cpu *c, uint64_t site);  // 0: original instruction; 1: returned offline
bool network_service_blocked(const char *name);
bool network_hle_blocked(const char *name);
void network_deny_ipc(const char *name, const IpcReq *req, IpcResp *resp);
