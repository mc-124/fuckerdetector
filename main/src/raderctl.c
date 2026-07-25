#include "raderctl.h"

#if CONFIG_APP_SERVER&&CONFIG_APP_SERVER_RADER_HLKLD1040

#include "misc.h"
#include "repl.h"

#include <string.h>
#include "driver/uart.h"
#include "esp_timer.h"
#include "esp_sleep.h"

#define LD1040_UART_BAUD 9600
#define RECV_CMD_TIMEOUT 2000 /* ms */

static const uint8_t INST_SET_MOVING_MODE[] = {
    0x3C, 0x3A, // 帧头
    0x07, // 长度
    0xFB, // 设置工作模式
    0x01, // 移动模式
    0x3A, 0x3E // 帧尾
};

static const uint8_t INST_QUERY_CONFIG[] = {
    0x3C, 0x3A, // 帧头
    0x07, // 长度
    0xFA, // 查询参数
    0x01, // 移动模式
    0x03A, 0x03E // 帧尾
};

static const uint8_t INST_SET_TH_A[] = {
    0x3C, 0x3A, // 帧头
    0x13, // 长度
    0xFD, // 设置参数
    0x01, // 移动模式
};

// TH TH TH 雷达门限（MSB）

static const uint8_t INST_SET_TH_B[] = {
    0xFF, // 感光门限
    0x00, 0x00, // 输出延迟
    0xFF, 0xFF, 0xFF, 0xFF, // 模组编号
    0x08, // 输出模式：渐亮渐灭-关
    0x00, // ??
    0x3A, 0x3E
};

static const uint8_t RESP_SET_TH[] = {
    0x3C, 0x3A, // 帧头
    0x07, // 长度
    0xFD, // 设置参数
    0x01, // 移动模式
    0x3A, 0x3E // 帧尾

};

static bool raderctl_recv_bytes(uint8_t *buf, uint8_t len){
    uint8_t curlen = 0;
    int64_t start_time = get_millis();
    for (;;){
        curlen += uart_read_bytes(UART_NUM_1, buf+curlen, len-curlen, pdMS_TO_TICKS(50));
        if (curlen==len){
            return true;
        } else if (((get_millis())-start_time)>RECV_CMD_TIMEOUT){
            return false;
        }
    }
}

static bool raderctl_rader_ready(){
    return CONFIG_APP_SERVER_RADER_PWRON_DURATION+500 < get_millis();
}

static void raderctl_init_rader(){
    uart_flush_input(UART_NUM_1);
    uart_write_bytes(UART_NUM_1, INST_SET_MOVING_MODE, sizeof(INST_SET_MOVING_MODE));
    uint8_t resp_buf[sizeof(INST_SET_MOVING_MODE)];
    if (raderctl_recv_bytes(resp_buf, sizeof(resp_buf))){
        if (!memcmp(resp_buf, INST_SET_MOVING_MODE, sizeof(resp_buf))){
            println("success");
        } else {
            printf("RECEIVED:");
            for (uint8_t i=0; i<sizeof(resp_buf); i++){
                printf(" %hhX", resp_buf[i]);
            }
            println("error: failed");
        }
    } else {
        println("error: timeout");
    }
}

static int raderctl_query_th(){
    uart_flush_input(UART_NUM_1);
    uart_write_bytes(UART_NUM_1, INST_QUERY_CONFIG, sizeof(INST_QUERY_CONFIG));
    uint8_t resp_buf[0x19];
    if (raderctl_recv_bytes(resp_buf, sizeof(resp_buf))){
        if (!memcmp(INST_QUERY_CONFIG, resp_buf, 2) // 帧头
            &&resp_buf[2] == 0x19 // 长度
            &&resp_buf[3] == 0xFA // 操作码
            &&!memcmp(INST_QUERY_CONFIG+5, resp_buf+sizeof(resp_buf)-2, 2) // 帧尾
        ){
            int th = resp_buf[4]<<16 | resp_buf[5]<<8 | resp_buf[6];
            return th;
        } else {
            printf("RECEIVED:");
            for (uint8_t i=0; i<sizeof(resp_buf); i++){
                printf(" %hhX", resp_buf[i]);
            }
            println("error: query failed");
            return -1;
        }
    } else {
        println("error: query timeout");
        return -1;
    }
}

static void raderctl_set_th(int th){
    uart_flush_input(UART_NUM_1);
    uart_write_bytes(UART_NUM_1, INST_SET_TH_A, sizeof(INST_SET_TH_A));
    uint8_t resp_buf[sizeof(RESP_SET_TH)] = {
        (th&0xff0000)>>16,
        (th&0x00ff00)>>8,
        (th&0x0000ff)
    };
    uart_write_bytes(UART_NUM_1, resp_buf, 3);
    uart_write_bytes(UART_NUM_1, INST_SET_TH_B, sizeof(INST_SET_TH_B));
    if (raderctl_recv_bytes(resp_buf, 7)){
        if (!memcmp(resp_buf, RESP_SET_TH, sizeof(RESP_SET_TH))){
            println("set success");
        } else {
            printf("RECEIVED:");
            for (uint8_t i=0; i<sizeof(resp_buf); i++){
                printf(" %hhX", resp_buf[i]);
            }
            println("error: set failed");
            return;
        }
    } else {
        println("error: set timeout");
        return;
    }
    misc_delay_ms(50);
    println("verify config");
    // 验证值
    int recv_th = raderctl_query_th();
    if (recv_th!=-1){
        if (th==recv_th){
            println("success");
        } else {
            println("error: invalid th value");
        }
    } else {
        println("error: verify failed");
    }
}

static void cmd_raderctl(uint8_t argc, const char **args){
    if (!raderctl_rader_ready()){
        do {
            println("Waiting rader ready ...");
            misc_delay_ms(1000);
        } while (!raderctl_rader_ready());
        misc_delay_ms(500);
    }
    if (argc==0){
        printf("Usage:\r\n"
            "  raderctl init | Needs to be used when the radar has never been configured\r\n"
            "  raderctl query-th | Query rader threshold\r\n"
            "  raderctl set-th <threshold> | Set rader threshold\r\n"
        );
    } else if (argc==1){
        if (!strcmp(args[0], "init")){
            raderctl_init_rader();
        } else if (!strcmp(args[0], "query-th")){
            int th = raderctl_query_th();
            if (th!=-1){
                printfln("threshold: %d", th);
            }
        } else {
            println("error: invalid arguments");
            return;
        }
    } else if (argc==2&&!strcmp(args[0], "set-th")){
        int th;
        if (misc_str_to_int(&th, args[1])){
            raderctl_set_th(th);
        } else {
            printfln("error: invalid number: %s", args[1]);
        }
    } else {
        println("error: invalid arguments");
    }
}

void raderctl_addcmds(){
    misc_init_peri_uaer();
    gpio_deep_sleep_hold_dis();
    gpio_hold_dis(PIN_OUTPUT);
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
    gpio_set_level(PIN_OUTPUT, 1);
    repl_addcmd("raderctl", "Query or set rader settings", cmd_raderctl); // #4
}

#endif // CONFIG_APP_SERVER&&CONFIG_APP_SERVER_RADER_HLKLD1040