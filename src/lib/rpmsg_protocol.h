#pragma once
#include <stdint.h>
#include <unistd.h>

// 指令类型定义 (Linux -> 实时端)
typedef enum {
    START_EXCITATION  = 0xC1,
    STOP_EXCITATION   = 0xC2,
    START_CONTROL     = 0xC3, // 控制激励
    STOP_CONTROL      = 0xC4, // 停止控制
    START_SP_IDENTIFY = 0xC5, // 开始次级通道辨识
    STOP_SP_IDENTIFY  = 0xC6, // 停止次级通道辨识
    START_DAMPING     = 0xC7
} cmd_type;

// 消息类型定义 (双向独立)
typedef enum {
    MSG_COMMAND   = 0xA1, // Linux->实时端: 控制指令
    MSG_SET_PARAM = 0xB1, // Linux->实时端: 参数设置
    MSG_REF_ARRAY = 0xC1, // 实时端->Linux: 参考信号数组
    MSG_ERR_ARRAY = 0xC2  // 实时端->Linux: 误差信号数组
} msg_Type;

typedef enum {
    PARAM_STEP_SIZE = 0x01, // 步长
    PARAM_FREQUENCY = 0x02, // 频率
} param_type;

// 参数设置负载结构
#pragma pack(push, 1)
typedef struct
{
    uint16_t param_id;  // 参数标识符
    double param_value; // 参数值
} ParamPayload;

// 传感器数组负载结构
#define REF_SIGNAL_ARRAY_SIZE 200
#define ERR_SIGNAL_ARRAY_SIZE 200
typedef uint16_t SensorArray[REF_SIGNAL_ARRAY_SIZE];
#pragma pack(pop)

// 数据包通用结构
typedef struct
{
    uint16_t msg_type; // 消息类型
    union
    {
        uint16_t command;   // MSG_COMMAND负载
        ParamPayload param; // MSG_SET_PARAM负载
        SensorArray array;  // MSG_REF_ARRAY负载
    } payload;
} rpmsg_packet;