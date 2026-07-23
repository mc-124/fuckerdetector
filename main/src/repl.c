#include "repl.h"
#include "misc.h"

#include "string.h"
#include "driver/uart.h"
#include "esp_crc.h"
#include "esp_task_wdt.h"

static const char *TAG = "repl";

struct ReplCommand {
    uint8_t name_len;
    uint8_t prompt_len;
    uint32_t name_crc32;
    const char *name;
    const char *prompt;
    ReplFuncPtr func;
};

struct ReplContext {
    uint8_t cmdc; // no rst
    uint8_t seek;
    uint8_t cmd_strlen;
    uint8_t args_strlen[REPL_MAX_ARGS];
    char cmd_strbuf[REPL_MAX_CMD_LEN+1];
    char args_strbuf[REPL_MAX_ARGS][REPL_MAX_ARG_LEN+1];
    struct ReplCommand cmds[REPL_MAX_CMDS]; // no rst
    const char *args_array[REPL_MAX_ARGS];
};

static struct ReplContext *ctx = NULL;

static const struct ReplCommand *find_command(const char *name, uint8_t name_len){
    if (!name_len){
        return NULL;
    }
    uint32_t name_crc32 = esp_crc32_le(REPL_CRC32_DEFAULT, (const uint8_t*)name, name_len);
    for (uint8_t i=0; i<ctx->cmdc; i++){
        const struct ReplCommand *cmd = ctx->cmds + i;
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

static void cmd_version(uint8_t argc, const char**){
    if (argc){
        println("error: invalid arguments");
        return;
    }
    printf( "# FuckerDetector"
        "version: " FIRMWARE_VER_TYPE "-" FIRMWARE_VERSION "\r\n"
        "idf version: " CONFIG_IDF_INIT_VERSION "\r\n"
        "compiler: " COMPILER_VERSION "\r\n"
        "build time: " __TIMESTAMP__ "\r\n"
    );
}

static void cmd_help(uint8_t argc, const char **args){
    if (argc==1){
        const struct ReplCommand *cmd = find_command(args[0], strlen(args[0]));
        if (!cmd||!cmd->func){
            println("error: command not found");
            return;
        }
        println("-------- HELP --------");
        printfln("- (%s): %s", cmd->name, cmd->prompt);        
    } else if (argc==0){
        println("-------- HELP --------");
        for (uint8_t i=0; i<REPL_MAX_CMDS; i++){
            const struct ReplCommand *cmd = ctx->cmds+i;
            if (cmd->func){
                printfln("- (%s): %s", cmd->name, cmd->prompt);
            }
        }
    } else {
        println("error: invalid arguments");
    }
}

void __add_command(const char *name, const char *prompt, ReplFuncPtr func){
    assert(name);
    assert(prompt);
    assert(func);
    if (ctx->cmdc>=REPL_MAX_CMDS){
        ESP_LOGE(TAG, "command array full");
        return;
    }
    uint8_t len = strlen(name);
    const struct ReplCommand *cmd = find_command(name, len);
    if (cmd){
        ESP_LOGE(TAG, "repeat command: %s", name);
        return;
    }
    struct ReplCommand *freeslot = ctx->cmds + (ctx->cmdc++);
    freeslot->name = name;
    freeslot->name_len = len;
    freeslot->name_crc32 = esp_crc32_le(REPL_CRC32_DEFAULT, (const uint8_t*)name, len);
    freeslot->prompt = prompt;
    freeslot->prompt_len = strlen(prompt);
    freeslot->func = func;
    ESP_LOGI(TAG, "add command: %s", name);
}

static void reset_context(){
    assert(ctx);
    ctx->seek = 0;
    ctx->cmd_strlen = 0;
    memset(ctx->args_strlen, 0, sizeof(ctx->args_strlen));
    memset(ctx->cmd_strbuf, 0, sizeof(ctx->cmd_strbuf));
    memset(ctx->args_strbuf, 0, sizeof(ctx->args_strbuf));
}

void init_repl(){
    assert(!ctx);
    ctx = malloc(sizeof(struct ReplContext));
    if (!ctx){
        ESP_ERROR_CHECK(ESP_ERR_NO_MEM);
    }
    reset_context();
    memset(ctx->cmds, 0, sizeof(ctx->cmds));
    for (uint8_t i=0; i<REPL_MAX_ARGS; i++){
        ctx->args_array[i] = ctx->args_strbuf[i];
    }
    ctx->cmdc = 0;
    ESP_ERROR_CHECK(uart_driver_install(UART_NUM_0, 256, 256, 0, NULL, 0));
    add_command("help", "Print information for help", cmd_help);
    add_command("version", "Print firmware version", cmd_version);
}

#define is_valid_char(__b) (32<__b&&__b<127)
#define pr_char(__b) \
    do {                \
        char __lb = __b;\
        uart_write_bytes(UART_NUM_0, &__lb, 1); \
    } while (0);
#define pr_beep() \
    do {                \
        led(1);         \
        pr_char(7);     \
        delay_ms(20);   \
        led(0);         \
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

static void proc_line(){
    pr_nextline();

    const struct ReplCommand *cmd = find_command(ctx->cmd_strbuf, ctx->cmd_strlen);
    if (!cmd){
        println("error: invalid command");
        goto end;
    }

    cmd->func(ctx->seek, ctx->args_array);

    end:
    reset_context();
    pr_repl_prompt();
}

static void proc_byte(char byte){
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
            proc_line();
        } else if (ctx->seek){
            ctx->seek--;
            proc_line();
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

void begin_repl(){
    assert(ctx);
    led(0);
    esp_task_wdt_deinit();
    pr_repl_prompt();
    for (;;){
        char byte;
        if (uart_read_bytes(UART_NUM_0, &byte, 1, pdMS_TO_TICKS(50))){
            proc_byte(byte);
        } else {
            delay_ms(5);
        }
    }
}
