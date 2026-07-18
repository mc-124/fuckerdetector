#include "cli.h"

#if CONFIG_APP_GENERAL_CLI_ENABLED

#include "misc.h"
#include <stdlib.h>
#include "driver/uart.h"
#include "esp_system.h"
#include "esp_crc.h"

static const char* TAG = "CLI";

inline static void backspace(){
    uart_write_bytes(UART_NUM_0, "\b \b", 3);
}

inline static void space(){
    uart_write_bytes(UART_NUM_0, " ", 1);
}

inline static void nextline(){
    uart_write_bytes(UART_NUM_0, "\r\n", 2);
}

inline static void cli_reset_ctx(struct CliContext* ctx){
    memset(ctx, 0, sizeof(struct CliContext));
}

static void cli_parse_line(struct CliContext *ctx, const struct CliCommands *cmds){
    if (!ctx->cmd_len){
        println("syntax error");
    }
    uint32_t crc32 = esp_crc32_le(0xcc114514, (uint8_t*)ctx->cmd_buf, ctx->cmd_len);
    for (uint8_t i=0;i<CONFIG_APP_GENERAL_CLI_MAX_CMD_COUNT;i++){
        const struct __CliCommandSlot *cmd = cmds->cmds+i;
        if (cmd->crc32==crc32&&cmd->len==ctx->cmd_len){
            if (memcmp(ctx->cmd_buf, cmd->cmd, ctx->cmd_len)&&cmd->func!=NULL){
                cmd->func(ctx, cmds);
                cli_reset_ctx(ctx);
                return;
            }
        }
    }
    println("error: command not found");
}

static void cli_recv_byte(struct CliContext *ctx, const struct CliCommands *cmds, char byte){
    if (byte=='\r'){
        return;
    } else if (byte=='\t'){
        byte = '\n';
    }
    if (ctx->seek==0){
        if (byte=='/'){
            ctx->seek++;
            printf("/");
        }
    } else if (ctx->seek==1){ // command
        if (byte=='\n'){
            cli_parse_line(ctx, cmds);
            nextline();
        } else if (byte==' ') {
            if (ctx->cmd_len){
                ctx->seek++;
                space();
            }
        } else if (byte=='\b') {
            if (ctx->cmd_len){
                ctx->cmd_len--;
            } else {
                ctx->seek--;
            }
            backspace();
        } else if (ctx->cmd_len<CONFIG_APP_GENERAL_CLI_MAX_CMD_LEN){
            ctx->cmd_buf[ctx->cmd_len++] = byte;
            uart_write_bytes(UART_NUM_0, &byte, 1);
        }
    } else { // arguments
        uint8_t arg_i = ctx->seek - 2;
        struct CliArgument *arg = ctx->args+arg_i;
        if (byte==' '){ // next argument
            if (arg->len&&arg_i+1<CONFIG_APP_GENERAL_CLI_MAX_ARG_COUNT){
                ctx->seek++;
                space();
            }
        } else if (byte=='\n'){
            if (!arg->len) ctx->seek--;
            cli_parse_line(ctx, cmds);
            nextline();
        } else if (byte=='\b'){
            if (arg->len){
                arg->len++;
            } else {
                ctx->seek--;
            }
            backspace();
        } else if (arg->len<CONFIG_APP_GENERAL_CLI_MAX_ARG_LEN){
            arg->buf[arg->len++] = byte;
            uart_write_bytes(UART_NUM_0, &byte, 1);
        }
    }
}

void cli_init(struct CliContext **pctx, struct CliCommands **pcmds){
    struct CliContext *ctx = malloc(sizeof(struct CliContext));
    if (!ctx){
        ESP_LOGE(TAG, "allocate CliContext failed");
        ESP_ERROR_CHECK(ESP_ERR_NO_MEM);
    }
    cli_reset_ctx(ctx);
    *pctx = ctx;
    struct CliCommands *cmds = malloc(sizeof(struct CliCommands));
    if (!cmds){
        ESP_LOGE(TAG, "allocate CliCommands failed");
        ESP_ERROR_CHECK(ESP_ERR_NO_MEM);
    }
    memset(cmds, 0, sizeof(struct CliCommands));
    *pcmds = cmds;
}

[[noreturn]] void cli_loop(struct CliContext *ctx, struct CliCommands *cmds){
    println("begin cli\r\nType \"/help\" for more information.");
    for(;;){
        char byte;
        int len = uart_read_bytes(UART_NUM_0, &byte, 1, 100);
        if (len){
            cli_recv_byte(ctx, cmds, byte);
        }
    }
}

void cli_add_cmd(struct CliCommands *cmds, const char *command, CliCommandFuncPtr func, const char* prompt){
    if (!command){
        ESP_ERROR_CHECK(ESP_ERR_INVALID_ARG);
    }
    if (cmds->len<CONFIG_APP_GENERAL_CLI_MAX_CMD_COUNT){
        uint32_t len = strlen(command);
        if (len>=CONFIG_APP_GENERAL_CLI_MAX_CMD_LEN){
            ESP_LOGE(TAG, "command too long");
            ESP_ERROR_CHECK(ESP_ERR_NOT_ALLOWED);
        }
        for (uint32_t i=0;i<CONFIG_APP_GENERAL_CLI_MAX_CMD_LEN;i++){
            char byte = command[i];
            if (('A'<=byte&&byte<='Z')||('a'<=byte&&byte<='z')||('0'<=byte&&byte<='9')||byte=='_'){
                ESP_LOGE(TAG, "invalid command char. at index %d: '\\x%X'", i, byte);
                ESP_ERROR_CHECK(ESP_ERR_NOT_ALLOWED);
            }
        }
        struct __CliCommandSlot cmd = {
            .len = len,
            .crc32 = esp_crc32_le(0xcc114514, (uint8_t*)command, len),
            .cmd = command,
            .func = func,
            .prompt = prompt
        };
        cmds->cmds[cmds->len++] = cmd;
    } else {
        ESP_LOGE(TAG, "Add cmd \"%s\" failed", command);
        ESP_ERROR_CHECK(ESP_ERR_NO_MEM);
    }
}

#endif