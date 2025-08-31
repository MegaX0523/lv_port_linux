#ifndef LINUX_MSG_H
#define LINUX_MSG_H

typedef enum {
    CMD_START_EXCITATION = 1,
    CMD_STOP_EXCITATION  = 2,
    CMD_START_CONTROL    = 3,
    CMD_STOP_CONTROL     = 4,
    CMD_SET_PARAM        = 5,
    CMD_GET_ARRAY        = 6,
    QUIT                 = 0
} cmd;

int start_rpmsg(void);
int send_msg(int cmd_type, u_int16_t param_id, double param_value);

#endif // LINUX_MSG_H