#include "raderctl.h"

#if CONFIG_APP_SERVER&&CONFIG_APP_SERVER_RADER_HLKLD1040

#include "misc.h"

#include "string.h"
#include "driver/uart.h"
#include "esp_timer.h"

#define LD1040_UART_BAUD 9600
#define RECV_CMD_TIMEOUT 2000 /* ms */

static bool receive_data(uint8_t *buf, uint8_t len){
    uint8_t curlen = 0;
    uint32_t start_time = get_millis();
    for (;;){
        curlen += uart_read_bytes(UART_NUM_1, buf+curlen, len-curlen, pdMS_TO_TICKS(50));
        if (curlen==len){
            return true;
        } else if ((((uint32_t)get_millis())-start_time)>RECV_CMD_TIMEOUT){
            return false;
        }
    }
}

static void cmd_raderctl(uint8_t argc, char **args){
    if (argc<=1){
        println("error: invalid arguments");
    }
    char *mode_str = args[0];
    if (argc==1&&!strcmp(mode_str, "read_th")){
        
    } else if (argc==2&&!strcmp(mode_str, "write_th")){

    } else if (argc==1&&!strcmp(mode_str, "init")){
        uint8_t cmd_buf[] = {
            0x3C, 0x3A, // 帧头
            0x07, // 长度
            0xFB, // 设置工作模式
            0x01, // 手扫
            0x3A, 0x3E, // 帧尾
        };
        uint8_t recv_buf[sizeof(cmd_buf)];
        if (receive_data(recv_buf, sizeof(cmd_buf))){
            if (!memcmp(recv_buf, cmd_buf, sizeof(cmd_buf))){
                println("ok");
            } else {
                println("error: failed");
                for (uint8_t i=0; i<sizeof(cmd_buf); i++){
                    printf(" %02hhX", recv_buf[i]);
                }
                println();
            }
        } else {
            println("error: receive timeout");
        }
    }
}

void init_raderctl(){
    init_peri_uart();
}

#endif // CONFIG_APP_SERVER&&CONFIG_APP_SERVER_RADER_HLKLD1040