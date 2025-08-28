#pragma once
#include <stdint.h>
#include <unistd.h>

// ָ�����Ͷ��� (Linux -> ʵʱ��)
typedef enum {
    START_EXCITATION = 0xC1,
    STOP_EXCITATION  = 0xC2,
    START_CONTROL   = 0xC3, // ���Ƽ���
    STOP_CONTROL    = 0xC4, // ֹͣ����
    START_DAMPING    = 0xC5,
    STOP_DAMPING     = 0xC6
} cmd_type;

// ��Ϣ���Ͷ��� (˫�����)
typedef enum {
    MSG_COMMAND       = 0xA1, // Linux->ʵʱ��: ����ָ��
    MSG_SET_PARAM     = 0xB1, // Linux->ʵʱ��: ��������
    MSG_SENSOR_ARRAY  = 0xC1  // ʵʱ��->Linux: ����������
} msg_Type;

typedef enum 
{
    PARAM_STEP_SIZE = 0x01,    // ����
    PARAM_FREQUENCY = 0x02, // Ƶ��
}param_type;

// �������ø��ؽṹ  
#pragma pack(push, 1)
typedef struct {
    uint16_t param_id;  // ������ʶ��
    double param_value; // ����ֵ
} ParamPayload;

// ���������鸺�ؽṹ
#define SENSOR_ARRAY_SIZE 200
typedef uint16_t SensorArray[SENSOR_ARRAY_SIZE];
#pragma pack(pop)

// ���ݰ�ͨ�ýṹ
typedef struct {
    uint16_t msg_type;   // ��Ϣ����
    union {
        uint16_t command;       // MSG_COMMAND����
        ParamPayload param;    // MSG_SET_PARAM����
        SensorArray array;     // MSG_SENSOR_ARRAY����
    } payload;
} rpmsg_packet;