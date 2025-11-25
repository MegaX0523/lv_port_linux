#ifndef LINUX_MSG_H
#define LINUX_MSG_H

typedef enum {
    CMD_START_EXCITATION = 1,
    CMD_STOP_EXCITATION  = 2,
    CMD_START_CONTROL    = 3,
    CMD_STOP_CONTROL     = 4,
    CMD_START_IDENTIFY    = 5,
    CMD_STOP_IDENTIFY     = 6,
    CMD_SET_PARAM        = 7,
    CMD_GET_ARRAY        = 8,
    QUIT                 = 0
} cmd;

typedef struct {
    lv_obj_t* label;
    const char* text;
} label_update_t;

int start_rpmsg(void);
int send_msg(int cmd_type, u_int16_t param_id, double param_value);
void update_label_cb(void* arg);

#endif // LINUX_MSG_H