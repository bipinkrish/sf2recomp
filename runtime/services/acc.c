#include "../constants/services.h"
// acc:u0 (one local user) and the preselected-user launch parameter.
#include <string.h>

#include "../hle.h"
#include "../ipc.h"

const uint8_t g_user_id[SF2C_ACC_N_16_B17E] = {SF2C_ACC_N_0X4C_C1BA, SF2C_ACC_N_0X41_BE08, SF2C_ACC_N_0X52_EE58, SF2C_ACC_N_0X65_0237, SF2C_ACC_N_0X63_A6C6, SF2C_ACC_N_0X6F_F59F, SF2C_ACC_N_0X6D_CAE4, SF2C_ACC_N_0X70_B0F6, SF2C_ACC_N_0X01_B51C, 0, 0, 0, 0, 0, 0, SF2C_ACC_N_0X01_B51C};

static void write_uid_list(IpcReq *req) {
    IpcBuf *c = out_buf(req, 0);
    if (!c) return;
    memset((void *)(uintptr_t)c->addr, 0, c->size);
    if (c->size >= SF2C_ACC_N_16_B17E) memcpy((void *)(uintptr_t)c->addr, g_user_id, SF2C_ACC_N_16_B17E);
}

static void profile(Service *s, IpcReq *req, IpcResp *resp) {
    (void)s;
    switch (req->cmd) {
    case 0: case 1: {  // Get / GetBase -> ProfileBase {uid, u64 timestamp, nickname[0x21]}
        memset(resp->data, 0, SF2C_ACC_N_0X38_AE4F);
        memcpy(resp->data, g_user_id, SF2C_ACC_N_16_B17E);
        *(uint64_t *)(resp->data + SF2C_ACC_N_16_B17E) = SF2C_ACC_N_1600000000_0934;
        strcpy((char *)resp->data + SF2C_ACC_N_24_C235, SF2C_ACC_S_PLAYER_D946);
        resp_size(resp, SF2C_ACC_N_0X38_AE4F);
        if (req->cmd == 0 && out_buf(req, 0)) memset((void *)(uintptr_t)out_buf(req, 0)->addr, 0, out_buf(req, 0)->size);
        break;
    }
    case SF2C_ACC_N_10_4A44: OUT(uint32_t, 0) = 0; resp_size(resp, SF2C_ACC_N_4_4B22); break;  // GetImageSize
    case SF2C_ACC_N_11_4FC8: OUT(uint32_t, 0) = 0; resp_size(resp, SF2C_ACC_N_4_4B22); break;  // LoadImage
    default: LOG(SF2C_ACC_S_ACC_IPROFILE_CMD_U_OK_N_2AFF, req->cmd);
    }
}

static void manager(Service *s, IpcReq *req, IpcResp *resp) {
    (void)s;
    switch (req->cmd) {
    case 0: break;  // CheckAvailability
    case 1: OUT(uint64_t, 0) = SF2C_ACC_N_0X1234567890ABCDEFULL_7745; resp_size(resp, SF2C_ACC_N_8_2C62); break;  // GetAccountId
    default: LOG(SF2C_ACC_S_ACC_IMANAGERFORAPPLICATION_C_A920, req->cmd); resp_size(resp, SF2C_ACC_N_0X10_BF9C);
    }
}

SERVICE(SF2C_ACC_S_ACC_U0_CB1C, acc_u0) {
    (void)s;
    switch (req->cmd) {
    case 0: OUT(int32_t, 0) = 1; resp_size(resp, SF2C_ACC_N_4_4B22); break;  // GetUserCount
    case 1: OUT(uint8_t, 0) = !memcmp(req->data, g_user_id, SF2C_ACC_N_16_B17E); resp_size(resp, 1); break;
    case SF2C_ACC_N_2_D473: case SF2C_ACC_N_3_4E07: case SF2C_ACC_N_141_2C7D: write_uid_list(req); break;  // ListAllUsers / ListOpenUsers / ListQualifiedUsers
    case SF2C_ACC_N_4_4B22: case SF2C_ACC_N_51_031B: memcpy(resp->data, g_user_id, SF2C_ACC_N_16_B17E); resp_size(resp, SF2C_ACC_N_16_B17E); break;  // GetLastOpenedUser
    case SF2C_ACC_N_5_EF2D: ipc_return_object(resp, service_new(SF2C_ACC_S_IPROFILE_DE64, profile, NULL)); break;
    case SF2C_ACC_N_50_1A65: OUT(uint8_t, 0) = 0; resp_size(resp, 1); break;
    case SF2C_ACC_N_100_AD57: case SF2C_ACC_N_140_DBAE: break;  // InitializeApplicationInfo
    case SF2C_ACC_N_101_16DC: ipc_return_object(resp, service_new(SF2C_ACC_S_IMANAGERFORAPPLICATION_419C, manager, NULL)); break;
    case SF2C_ACC_N_150_9AE2: OUT(uint8_t, 0) = 0; resp_size(resp, 1); break;  // IsUserAccountSwitchLocked
    default: LOG(SF2C_ACC_S_ACC_ACC_U0_CMD_U_OK_N_908A, req->cmd); resp_size(resp, SF2C_ACC_N_0X10_BF9C);
    }
}
