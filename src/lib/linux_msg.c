#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <stdbool.h>
#include <fcntl.h>
#include <termios.h>
#include <errno.h>
#include <string.h>
#include <sys/poll.h>
#include "rpmsg_protocol.h"

#define MSG_PATH "/dev/ttyRPMSG0"
#define TEST_PRINT

// 全局变量
int rpmsg_fd;
bool has_new_array           = false;
pthread_mutex_t g_mutex_lock = PTHREAD_MUTEX_INITIALIZER; // 保护send_msg调用
int16_t sensor_array[200] = {0};

// 设置TTY为原始模式
int set_tty_raw(int fd)
{
    struct termios tty;

    if(tcgetattr(fd, &tty) < 0) {
        perror("tcgetattr failed");
        return -1;
    }
    // 完全禁用输入输出处理
    tty.c_iflag = 0; // 禁用所有输入处理
    tty.c_oflag = 0; // 禁用所有输出处理
    tty.c_lflag = 0; // 禁用所有本地处理
    // 特殊字符设置
    tty.c_cc[VMIN]  = 0; // 非阻塞模式
    tty.c_cc[VTIME] = 0; // 无超时

    // 应用设置
    if(tcsetattr(fd, TCSANOW, &tty) < 0) {
        perror("tcsetattr failed");
        return -1;
    }
    // 刷新缓冲区
    tcflush(fd, TCIOFLUSH);

    return 0;
}

int send_msg(int cmd_type, uint16_t param_id, double param_value)
{
    rpmsg_packet pkt;
    size_t pkt_size = 0;
    bool valid_cmd  = true;

    // 加锁保护，防止并发调用冲突
    pthread_mutex_lock(&g_mutex_lock);

    switch(cmd_type) {
        case CMD_START_EXCITATION: // Start excitation
        {
            pkt.msg_type        = MSG_COMMAND;
            pkt.payload.command = START_EXCITATION;
            pkt_size            = sizeof(pkt.msg_type) + sizeof(uint16_t);
            printf("Sending: Start excitation...\n");
            break;
        }
        case CMD_STOP_EXCITATION: // Stop excitation
        {
            pkt.msg_type        = MSG_COMMAND;
            pkt.payload.command = STOP_EXCITATION;
            pkt_size            = sizeof(pkt.msg_type) + sizeof(uint16_t);
            printf("Sending: Stop excitation...\n");
            break;
        }
        case CMD_START_CONTROL: // start control
        {
            pkt.msg_type        = MSG_COMMAND;
            pkt.payload.command = START_CONTROL;
            pkt_size            = sizeof(pkt.msg_type) + sizeof(uint16_t);
            printf("Sending: Start control...\n");
            break;
        }
        case CMD_STOP_CONTROL: // stop control
        {
            pkt.msg_type        = MSG_COMMAND;
            pkt.payload.command = STOP_CONTROL;
            pkt_size            = sizeof(pkt.msg_type) + sizeof(uint16_t);
            printf("Sending: Stop control...\n");
            break;
        }
        case CMD_SET_PARAM: // Set Parameter
        {
            pkt.msg_type                  = MSG_SET_PARAM;
            pkt.payload.param.param_id    = param_id;
            pkt.payload.param.param_value = param_value;
            pkt_size                      = sizeof(pkt.msg_type) + sizeof(ParamPayload);
            printf("Sending: Set param ID %u to %.2f\n", param_id, param_value);
            break;
        }
        case CMD_GET_ARRAY: // Request Sensor Array
        {
            pkt.msg_type        = MSG_COMMAND;
            pkt.payload.command = START_DAMPING; // 假设START_DAMPING用于请求数据
            pkt_size            = sizeof(pkt.msg_type) + sizeof(uint16_t);
            printf("Sending: Sensor array request...\n");
            break;
        }
        case 0: // Exit
        {
            printf("Exiting...\n");
            close(rpmsg_fd);
            pthread_mutex_unlock(&g_mutex_lock);
            exit(0);
        }
        default: printf("Invalid command.\n"); valid_cmd = false;
    }

    int send_result = 0;
    if(valid_cmd) {
        size_t sent = write(rpmsg_fd, &pkt, pkt_size);
        if(sent != (size_t)pkt_size) {
            perror("Failed to send command");
            send_result = -1;
        }
    }
    usleep(100000); // 100ms延迟
    pthread_mutex_unlock(&g_mutex_lock);

    return send_result;
}

// 合并后的命令输入和发送线程函数
void * cmd_send_thread_func(void * arg)
{
    int cmd;
    int ret;
    (void)arg;

    while(1) {
        printf("\nEnter command \n"
               "1: Start excitation\n"
               "2: Stop excitation\n"
               "3: Start control\n"
               "4: Stop control\n"
               "5: Set parameter\n"
               "6: Request sensor array\n"
               "0: Exit\n");
        ret = scanf("%d", &cmd);
        if(ret != 1) {
            // 清除无效输入
            int c;
            while((c = getchar()) != '\n' && c != EOF);
            printf("Invalid input. Please enter a number.\n");
            continue;
        }

        if(cmd != 5) {
            send_msg(cmd, 0, 0);
        } else {
            uint16_t param_id;
            double param_value;

            printf("Enter parameter ID (1: step size, 2: frequency): ");
            if(scanf("%hu", &param_id) != 1) {
                printf("Invalid parameter ID\n");
                continue;
            }

            printf("Enter parameter value: ");
            if(scanf("%lf", &param_value) != 1) {
                printf("Invalid parameter value\n");
                continue;
            }
            send_msg(CMD_SET_PARAM, param_id, param_value);
        }
    }
    return NULL;
}

void * get_array_thread_func(void * arg)
{
    struct pollfd fds     = {.fd = rpmsg_fd, .events = POLLIN};
    const size_t pkt_size = sizeof(uint16_t) + sizeof(SensorArray);
    uint8_t recv_buffer[sizeof(rpmsg_packet) * 2];
    size_t bytes_received = 0;

    printf("Sensor monitor thread started\n");
    (void)arg;

    while(1) {
        if(poll(&fds, 1, 100) <= 0) {
            if(errno != EINTR) perror("poll error");
            continue;
        }

        if(!(fds.revents & POLLIN)) continue;

        ssize_t n = read(rpmsg_fd, recv_buffer + bytes_received, sizeof(recv_buffer) - bytes_received);
        if(n <= 0) {
            if(n == 0)
                printf("Connection closed\n");
            else if(errno != EAGAIN && errno != EWOULDBLOCK)
                perror("Read error");
            break;
        }

        bytes_received += (size_t)n;

        while(bytes_received >= sizeof(uint16_t)) {
            uint16_t msg_type;
            memcpy(&msg_type, recv_buffer, sizeof(uint16_t));

            if(msg_type != MSG_SENSOR_ARRAY) {
                printf("Unknown message type: 0x%04X\n", msg_type);
                memmove(recv_buffer, recv_buffer + 1, --bytes_received);
                continue;
                // printf("Unknown message type: 0x%04X, discarding all received data\n", msg_type);
                // bytes_received = 0;
                // break;
            }

            if(bytes_received < pkt_size) break;

            rpmsg_packet pkt;
            memcpy(&pkt, recv_buffer, pkt_size);

#ifdef TEST_PRINT
            printf("\nReceived sensor array (%d elements):\n", SENSOR_ARRAY_SIZE);
            for(int i = 0; i < SENSOR_ARRAY_SIZE; i++) {
                if(i % 16 == 0) printf(i > 0 ? "\n[%3d-%3d]:" : "[%3d-%3d]:", i, i + 15);
                printf(" %X", pkt.payload.array[i]);
            }
            printf("\n");
#else
            if(has_new_array == true) {
                printf("WARNING: array processing unfinished!")
            } else {
                memcpy(sensor_array, pkt.payload.array, sizeof(SensorArray));
                has_new_array = true;
            }
#endif
            memmove(recv_buffer, recv_buffer + pkt_size, bytes_received - pkt_size);
            bytes_received -= pkt_size;
        }
    }

    return NULL;
}

int start_rpmsg(void)
{
    // 打开设备
    rpmsg_fd = open(MSG_PATH, O_RDWR | O_NONBLOCK | O_NOCTTY);
    if(rpmsg_fd < 0) {
        perror("Failed to open message path");
        return EXIT_FAILURE;
    }
    printf("Device opened successfully, fd=%d\n", rpmsg_fd);

    // 设置TTY为原始模式
    if(set_tty_raw(rpmsg_fd) < 0) {
        fprintf(stderr, "Failed to set TTY raw mode. Proceeding but may have issues.\n");
    } else {
        printf("TTY raw mode configured successfully\n");
    }

    // 创建两个线程：命令输入/发送线程、打印线程
    pthread_t cmd_send_thread, print_thread;

    if(pthread_create(&cmd_send_thread, NULL, cmd_send_thread_func, NULL) ||
       pthread_create(&print_thread, NULL, get_array_thread_func, NULL)) {
        perror("Failed to create threads");
        close(rpmsg_fd);
        return EXIT_FAILURE;
    }

    // 等待线程结束（通常不会发生）
    pthread_join(cmd_send_thread, NULL);
    pthread_join(print_thread, NULL);

    close(rpmsg_fd);
    return EXIT_SUCCESS;
}