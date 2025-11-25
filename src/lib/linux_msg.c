#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <stdbool.h>
#include <fcntl.h>
#include <termios.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#include <sys/poll.h>
#include "lvgl/lvgl.h"
#include "linux_msg.h"
#include "rpmsg_protocol.h"

#define MSG_PATH "/dev/ttyRPMSG0"
#define Y_SCALE 1024

// 全局变量
int rpmsg_fd;
bool has_new_ref_signal                             = false;
bool has_new_err_signal                             = false;
pthread_mutex_t g_mutex_lock                        = PTHREAD_MUTEX_INITIALIZER; // 保护send_msg调用
int16_t ref_signal_array[200]                       = {0};
int16_t err_signal_array[200]                       = {0};
double ref_voltage[REF_SIGNAL_ARRAY_SIZE]           = {0.0};
double err_voltage[REF_SIGNAL_ARRAY_SIZE]           = {0.0};
int32_t converted_ref_values[REF_SIGNAL_ARRAY_SIZE] = {0};
int32_t converted_err_values[ERR_SIGNAL_ARRAY_SIZE] = {0};
double ref_max_val                                  = -10.0;
double ref_min_val                                  = 10.0;
double err_max_val                                  = -10.0;
double err_min_val                                  = 10.0;

extern lv_obj_t * ref_label;
extern lv_obj_t * err_label;

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

int send_msg(int cmd_type, u_int16_t param_id, double param_value)
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
            pkt_size            = sizeof(pkt.msg_type) + sizeof(u_int16_t);
            printf("Sending: Start excitation...\n");
            break;
        }
        case CMD_STOP_EXCITATION: // Stop excitation
        {
            pkt.msg_type        = MSG_COMMAND;
            pkt.payload.command = STOP_EXCITATION;
            pkt_size            = sizeof(pkt.msg_type) + sizeof(u_int16_t);
            printf("Sending: Stop excitation...\n");
            break;
        }
        case CMD_START_CONTROL: // start control
        {
            pkt.msg_type        = MSG_COMMAND;
            pkt.payload.command = START_CONTROL;
            pkt_size            = sizeof(pkt.msg_type) + sizeof(u_int16_t);
            printf("Sending: Start control...\n");
            break;
        }
        case CMD_STOP_CONTROL: // stop control
        {
            pkt.msg_type        = MSG_COMMAND;
            pkt.payload.command = STOP_CONTROL;
            pkt_size            = sizeof(pkt.msg_type) + sizeof(u_int16_t);
            printf("Sending: Stop control...\n");
            break;
        }
        case CMD_START_IDENTIFY: // start identify
        {
            pkt.msg_type        = MSG_COMMAND;
            pkt.payload.command = START_SP_IDENTIFY;
            pkt_size            = sizeof(pkt.msg_type) + sizeof(u_int16_t);
            printf("Sending: Start identify...\n");
            break;
        }
        case CMD_STOP_IDENTIFY: // stop identify
        {
            pkt.msg_type        = MSG_COMMAND;
            pkt.payload.command = STOP_SP_IDENTIFY;
            pkt_size            = sizeof(pkt.msg_type) + sizeof(u_int16_t);
            printf("Sending: Stop identify...\n");
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
            pkt_size            = sizeof(pkt.msg_type) + sizeof(u_int16_t);
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
               "5: Start identify\n"
               "6: Stop identify\n"
               "7: Set parameter\n"
               "8: Request sensor array\n"
               "0: Exit\n");
        ret = scanf("%d", &cmd);
        if(ret != 1) {
            // 清除无效输入
            int c;
            while((c = getchar()) != '\n' && c != EOF);
            printf("Invalid input. Please enter a number.\n");
            continue;
        }

        if(cmd != 7) {
            send_msg(cmd, 0, 0);
        } else {
            u_int16_t param_id;
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

// 异步回调函数（在主线程中执行）
void update_label_cb(void * arg)
{
    label_update_t * update = (label_update_t *)arg;
    if(update && update->label) {
        lv_label_set_text(update->label, update->text);
    }
    free(update); // 记得释放内存
}

void * get_array_thread_func(void * arg)
{
    struct pollfd fds     = {.fd = rpmsg_fd, .events = POLLIN};
    const size_t pkt_size = sizeof(u_int16_t) + sizeof(SensorArray);
    uint8_t recv_buffer[sizeof(rpmsg_packet) * 2];
    size_t bytes_received = 0;

    printf("Sensor monitor thread started\n");
    (void)arg;

    while(1) {
        if(poll(&fds, 1, -1) <= 0) {
            if(errno != EINTR) perror("poll error");
            continue;
        }

        ssize_t n = read(rpmsg_fd, recv_buffer + bytes_received, sizeof(recv_buffer) - bytes_received);
        if(n <= 0) {
            if(n == 0)
                printf("Connection closed\n");
            else if(errno != EAGAIN && errno != EWOULDBLOCK)
                perror("Read error");
            break;
        }
        bytes_received += (size_t)n;

        static bool warn_printed = false;

        while(bytes_received >= sizeof(u_int16_t)) {
            u_int16_t msg_type;
            memcpy(&msg_type, recv_buffer, sizeof(u_int16_t));

            if(msg_type != MSG_REF_ARRAY && msg_type != MSG_ERR_ARRAY) {
                if(!warn_printed) {
                    printf("WARNING: Unknown message type: 0x%04X (data may be misaligned)\n", msg_type);
                    warn_printed = true; // 置为true，后续不再打印
                }
                memmove(recv_buffer, recv_buffer + 1, --bytes_received);
                continue;
            }

            warn_printed = false; // 重置警告打印标志

            if(bytes_received < pkt_size) break;

            rpmsg_packet pkt;
            memcpy(&pkt, recv_buffer, pkt_size);
            if(msg_type == MSG_REF_ARRAY) {
                int wait = 10000;
                while(has_new_ref_signal == true) {
                    wait--;
                    if(wait < 0) {
                        printf("WARNING: array processing unfinished!");
                        break;
                    }
                }
                memcpy(ref_signal_array, pkt.payload.array, sizeof(SensorArray));
                static int ref_scale = 1 * (1024 - 20);
                for(int i = 0; i < REF_SIGNAL_ARRAY_SIZE; i++) {
                    ref_voltage[i]          = (int16_t)ref_signal_array[i] * 10.0 / 32767.0f; // 32768 = 0x8000
                    converted_ref_values[i] = (int32_t)(ref_voltage[i] / 10.0 * ref_scale);
                    if(ref_voltage[i] > ref_max_val) ref_max_val = ref_voltage[i];
                    if(ref_voltage[i] < ref_min_val) ref_min_val = ref_voltage[i];
                }
                // const char* text;
                // static int ref_divide_now = 1;
                // static bool ref_need_update = false;
                // if(ref_divide_now != 5 && ref_max_val < 0.9 && ref_min_val > -0.9) {
                //     ref_scale = 5 * (Y_SCALE - 20);
                //     text      = "Ref / 5 / V";
                //     ref_need_update = true;
                //     ref_divide_now = 5;
                // } else if(ref_divide_now != 2 && ref_max_val < 4.0 && ref_min_val > -4.0) {
                //     ref_scale = 2 * (Y_SCALE - 20);
                //     text      = "Ref / 2 / V";
                //     ref_need_update = true;
                //     ref_divide_now = 2;
                // } else if(ref_divide_now != 1) {
                //     ref_scale = 1 * (Y_SCALE - 20);
                //     text      = "Ref / 1 / V";
                //     ref_need_update = true;
                //     ref_divide_now = 1;
                // }

                // label_update_t * update = malloc(sizeof(label_update_t));
                // if(update && ref_need_update) {
                //     update->label = ref_label;
                //     update->text  = text;
                //     lv_async_call(update_label_cb, update);
                // }
                has_new_ref_signal = true;
            } else if(msg_type == MSG_ERR_ARRAY) {
                int wait = 10000;
                while(has_new_err_signal == true) {
                    wait--;
                    if(wait < 0) {
                        printf("WARNING: array processing unfinished!");
                        break;
                    }
                }
                memcpy(err_signal_array, pkt.payload.array, sizeof(SensorArray));
                static int err_scale = 1 * (Y_SCALE - 20);
                for(int i = 0; i < ERR_SIGNAL_ARRAY_SIZE; i++) {
                    err_voltage[i]          = (int16_t)err_signal_array[i] * 10.0 / 32767.0f; // 32768 = 0x8000
                    converted_err_values[i] = (int32_t)(err_voltage[i] / 10.0 * err_scale);
                    if(err_voltage[i] > err_max_val) err_max_val = err_voltage[i];
                    if(err_voltage[i] < err_min_val) err_min_val = err_voltage[i];
                }
                // const char* text;
                // static int err_divide_now = 1;
                // static bool err_need_update = false;
                // if(err_divide_now != 5 && err_max_val < 0.9 && err_min_val > -0.9) {
                //     err_scale = 5 * (Y_SCALE - 20);
                //     text      = "Err / 5 / V";
                //     err_need_update = true;
                //     err_divide_now = 5;
                // } else if(err_divide_now != 2 && err_max_val < 4.0 && err_min_val > -4.0) {
                //     err_scale = 2 * (Y_SCALE - 20);
                //     text      = "Err / 2 / V";
                //     err_need_update = true;
                //     err_divide_now = 2;
                // } else if(err_divide_now != 1) {
                //     err_scale = 1 * (Y_SCALE - 20);
                //     text      = "Err / 1 / V";
                //     err_need_update = true;
                //     err_divide_now = 1;
                // }

                // label_update_t * update = malloc(sizeof(label_update_t));
                // if(update && err_need_update) {
                //     update->label = err_label;
                //     update->text  = text;
                //     lv_async_call(update_label_cb, update);
                // }
                has_new_err_signal = true;
            }
            memmove(recv_buffer, recv_buffer + pkt_size, bytes_received - pkt_size);
            bytes_received -= pkt_size;
        }
    }

    return NULL;
}

int start_rpmsg(void)
{
    // 打开rpmsg设备
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

    time_t rawtime;
    struct tm * timeinfo;
    char filename[80];

    time(&rawtime);
    timeinfo = localtime(&rawtime);

    // 格式化时间为文件名：年-月-日_时-分-秒.txt
    strftime(filename, sizeof(filename), "%Y-%m-%d_%H-%M-%S.txt", timeinfo);

    // 创建并打开文件
    FILE * file = fopen(filename, "w");
    if(file == NULL) {
        perror("File creation failed");
        return 1;
    }

    fprintf(file, "File creation time: %s\n", asctime(timeinfo));
    fprintf(file, "This is automatically generated file content\n");
    fprintf(file, "Timestamp: %ld\n", rawtime);

    // 关闭文件
    fclose(file);

    // 创建两个线程：命令输入/发送线程、打印线程
    pthread_t cmd_send_thread, print_thread;

    if(pthread_create(&cmd_send_thread, NULL, cmd_send_thread_func, NULL) ||
       pthread_create(&print_thread, NULL, get_array_thread_func, NULL)) {
        perror("Failed to create threads");
        close(rpmsg_fd);
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}