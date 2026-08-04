#include "repl.h"
#include "misc.h"

#include <string.h>
#include "driver/uart.h"
#include "esp_crc.h"
#include "esp_task_wdt.h"
#include "esp_system.h"

static const char *TAG = "repl";

struct repl_command {
    uint8_t name_len;
    uint8_t prompt_len;
    uint32_t name_crc32;
    const char *name;
    const char *prompt;
    repl_cmd_callback_func_t func;
};

struct repl_context {
    uint8_t cmdc; // no rst
    uint8_t seek;
    uint8_t cmd_strlen;
    uint8_t args_strlen[REPL_MAX_ARGS];
    char cmd_strbuf[REPL_MAX_CMD_LEN+1];
    char args_strbuf[REPL_MAX_ARGS][REPL_MAX_ARG_LEN+1];
    struct repl_command cmds[REPL_MAX_CMDS]; // no rst
    const char *args_array[REPL_MAX_ARGS];
};

static struct repl_context *ctx = NULL;

static const struct repl_command *repl_find_command(const char *name, uint8_t name_len){
    if (!name_len){
        return NULL;
    }
    uint32_t name_crc32 = esp_crc32_le(REPL_CRC32_DEFAULT, (const uint8_t*)name, name_len);
    for (uint8_t i=0; i<ctx->cmdc; i++){
        const struct repl_command *cmd = &ctx->cmds[i];
        if (!cmd->func){
            continue;
        }
        if (cmd->name_len==name_len
            &&cmd->name_crc32==name_crc32
            &&(memcmp(cmd->name, name, name_len)==0)
        ){
            return cmd;
        }
    }
    return NULL;
}

#pragma region 内置命令

static void cmd_version(uint8_t argc, const char**){
    if (argc){
        println("error: invalid arguments");
        return;
    }
    printf( "# FuckerDetector\r\n"
        "version: " FIRMWARE_VER_TYPE "-" FIRMWARE_VERSION "\r\n"
        "idf version: " CONFIG_IDF_INIT_VERSION "\r\n"
        "compiler: " COMPILER_VERSION "\r\n"
        "build time: " __TIMESTAMP__ "\r\n"
    );
}

static void cmd_help(uint8_t argc, const char **args){
    if (argc==1){
        const struct repl_command *cmd = repl_find_command(args[0], strlen(args[0]));
        if (!cmd||!cmd->func){
            println("error: command not found");
            return;
        }
        println("-------- HELP --------");
        printfln("- (%s): %s", cmd->name, cmd->prompt);        
    } else if (argc==0){
        println("-------- HELP --------");
        for (uint8_t i=0; i<REPL_MAX_CMDS; i++){
            const struct repl_command *cmd = &ctx->cmds[i];
            if (!cmd->func) return;
            printfln("- (%s): %s", cmd->name, cmd->prompt);
        }
    } else {
        println("error: invalid arguments");
    }
}

static void cmd_exit(uint8_t argc, const char **args){
    if (argc){
        println("error: invalid arguments");
        return;
    }
    println("exit");
    fflush(stdin);
    misc_delay_ms(500);
    esp_restart();
}

#pragma endregion 内置命令

void __repl_addcmd(const char *name, const char *prompt, repl_cmd_callback_func_t func){
    assert(name);
    assert(prompt);
    assert(func);
    if (ctx->cmdc>=REPL_MAX_CMDS){
        ESP_LOGE(TAG, "add command failed: command array full");
        return;
    }
    uint8_t len = strlen(name);
    if (len>REPL_MAX_CMD_LEN){
        ESP_LOGE(TAG, "add command failed: name too long");
        return;
    }
    const struct repl_command *cmd = repl_find_command(name, len);
    if (cmd){
        ESP_LOGE(TAG, "add command failed: repeat command: %s", name);
        return;
    }
    struct repl_command *freeslot = ctx->cmds + (ctx->cmdc++);
    freeslot->name = name;
    freeslot->name_len = len;
    freeslot->name_crc32 = esp_crc32_le(REPL_CRC32_DEFAULT, (const uint8_t*)name, len);
    freeslot->prompt = prompt;
    freeslot->prompt_len = strlen(prompt);
    freeslot->func = func;
    ESP_LOGI(TAG, "add command: %s", name);
}

static void repl_rst_ctx(){
    assert(ctx);
    ctx->seek = 0;
    ctx->cmd_strlen = 0;
    memset(ctx->args_strlen, 0, sizeof(ctx->args_strlen));
    memset(ctx->cmd_strbuf, 0, sizeof(ctx->cmd_strbuf));
    memset(ctx->args_strbuf, 0, sizeof(ctx->args_strbuf));
}

void repl_init(){
    assert(!ctx);
    ctx = malloc(sizeof(struct repl_context));
    if (!ctx){
        ESP_ERROR_CHECK(ESP_ERR_NO_MEM);
    }
    repl_rst_ctx();
    memset(ctx->cmds, 0, sizeof(ctx->cmds));
    for (uint8_t i=0; i<REPL_MAX_ARGS; i++){
        ctx->args_array[i] = ctx->args_strbuf[i];
    }
    ctx->cmdc = 0;
    ESP_ERROR_CHECK(uart_driver_install(UART_NUM_0, 256, 256, 0, NULL, 0));
    repl_addcmd("help", "Print information for help", cmd_help); // #1
    repl_addcmd("version", "Print firmware version", cmd_version); // #2
    repl_addcmd("exit", "Exit REPL and reset chip", cmd_exit); // #3
}

#define is_valid_char(__b) (32<__b&&__b<127)
#define pr_char(__b) \
    do {                \
        char __lb = __b;\
        uart_write_bytes(UART_NUM_0, &__lb, 1); \
    } while (0);
#define pr_beep() \
    do {                \
        led(0);         \
        pr_char(7);     \
        misc_delay_ms(20);   \
        led(1);         \
    } while(0)
#define pr_backspace() \
    do {                \
        pr_char('\b');  \
        pr_char(' ');   \
        pr_char('\b');  \
    } while (0)
#define pr_nextline() \
    do {                \
        pr_char('\r');  \
        pr_char('\n');  \
    } while (0)
#define pr_repl_prompt() \
    do {                \
        pr_char('>');   \
        pr_char(' ');   \
    } while (0)

static void repl_proc_line(){
    pr_nextline();

    const struct repl_command *cmd = repl_find_command(ctx->cmd_strbuf, ctx->cmd_strlen);
    if (!cmd){
        println("error: invalid command");
        goto end;
    }

    cmd->func(ctx->seek, ctx->args_array);

    end:
    repl_rst_ctx();
    pr_repl_prompt();
}

static void repl_proc_byte(char byte){
    char *buf;
    uint8_t *len;

    if (ctx->seek){
        buf = ctx->args_strbuf[ctx->seek-1];
        len = ctx->args_strlen+ctx->seek-1;
    } else {
        buf = ctx->cmd_strbuf;
        len = &ctx->cmd_strlen;
    }

    if (byte==' '){
        if (*len&&ctx->seek<REPL_MAX_ARGS){
            ctx->seek++;
            pr_char(' ');
        } else {
            pr_beep();
        }
    } else if (byte=='\b'||byte==0x7f){
        if (*len){
            buf[--(*len)] = 0;
            pr_backspace();
        } else if (ctx->seek) {
            ctx->seek--;
            pr_backspace();
        } else {
            pr_beep();
        }
    } else if (byte=='\n'||byte=='\t'){
        if (*len){
            repl_proc_line();
        } else if (ctx->seek){
            ctx->seek--;
            repl_proc_line();
        } else {
            pr_beep();
        }
    } else if (is_valid_char(byte)){
        uint8_t maxlen = ctx->seek ? REPL_MAX_ARG_LEN : REPL_MAX_CMD_LEN ;
        if ((*len)<maxlen){
            buf[(*len)++] = byte;
            pr_char(byte);
        } else {
            pr_beep();
        }
    } else {
        pr_beep();
    }
}

[[noreturn]] void repl_begin(){
    assert(ctx);
    led(1);
    // CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU 被关了应该不会炸了
    //esp_task_wdt_deinit(); 
    println("Type \"help\" for more information.");
    pr_repl_prompt();
    for (;;){
        char byte;
        if (uart_read_bytes(UART_NUM_0, &byte, 1, pdMS_TO_TICKS(50))){
            repl_proc_byte(byte);
        } else {
            misc_delay_ms(5);
        }
    }
}
