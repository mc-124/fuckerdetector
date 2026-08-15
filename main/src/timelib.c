#include "timelib.h"

#if CONFIG_APP_SERVER

#include "misc.h"
#include "esp_log.h"
static const char *TAG = "timelib";

#if CONFIG_APP_SERVER_RTC_DS3231
#include "i2cdev.h"
#include "ds3231.h"

#include <assert.h>

static i2c_dev_t timelib_rtc_h = {0};

struct timelib_slpitvl timelib_slpitvl_array[CONFIG_APP_SERVER_SLPITVL_MAX_NUM];

void timelib_init(){
    ESP_LOGI(TAG, "init RTC");
    ESP_ERROR_CHECK(i2cdev_init());
    ESP_ERROR_CHECK(
        ds3231_init_desc(&timelib_rtc_h, I2C_NUM_0, PIN_IIC_SDA, PIN_IIC_SCL)
    );
}

bool timelib_is_lost_time(){
    uint8_t flags = 0;
    ESP_ERROR_CHECK(i2c_dev_take_mutex(&timelib_rtc_h));
    ESP_ERROR_CHECK(i2c_dev_read_reg(&timelib_rtc_h, 0x0f, &flags, 1));
    ESP_ERROR_CHECK(i2c_dev_give_mutex(&timelib_rtc_h));
    return (flags&0x10000000);
}

void timelib_clear_lost_time(){
    uint8_t flags = 0;
    ESP_ERROR_CHECK(i2c_dev_take_mutex(&timelib_rtc_h));
    ESP_ERROR_CHECK(i2c_dev_read_reg(&timelib_rtc_h, 0x0f, &flags, 1));
    flags &= ~0x10000000;
    ESP_ERROR_CHECK(i2c_dev_write_reg(&timelib_rtc_h, 0x0f, &flags, 1));
    ESP_ERROR_CHECK(i2c_dev_give_mutex(&timelib_rtc_h));
}

void timelib_set_time(struct tm *time){
    assert(time);
    ESP_ERROR_CHECK(ds3231_set_time(&timelib_rtc_h, time));
}

void timelib_get_time(struct tm *time){
    assert(time);
    ESP_ERROR_CHECK(ds3231_get_time(&timelib_rtc_h, time));
}

int timelib_get_day_sec(){
    struct tm time;
    timelib_get_time(&time);
    return time.tm_hour*3600 + time.tm_min*60 + time.tm_sec;
}

const struct timelib_slpitvl *timelib_find_inprog_slpitvl(int now){
    for (int i=0; i<CONFIG_APP_SERVER_SLPITVL_MAX_NUM; i++){
        struct timelib_slpitvl *this = &timelib_slpitvl_array[i];
        if (this->start==FFFF_U32||this->end==FFFF_U32){
            continue;
        }
        if (this->start<=now&&now<=this->end){
            return this;
        }
    }
    return NULL;
}

const struct timelib_slpitvl *timelib_find_next_slpitvl(int now){
    struct timelib_slpitvl *min = NULL;
    int min_diff = 0;
    for (int i=0; i<CONFIG_APP_SERVER_SLPITVL_MAX_NUM; i++){
        struct timelib_slpitvl *this = &timelib_slpitvl_array[i];
        if (this->start==FFFF_U32||this->end==FFFF_U32){
            continue;
        }
        int diff = sec_sub(this->start, now);
        if (!min||diff<min_diff){
            min = this;
            min_diff = diff;
        }
    }
    return min;
}

void timelib_print_slpitvl(const struct timelib_slpitvl *slpitvl){
    assert(slpitvl);
    if (slpitvl->start!=FFFF_U32&&slpitvl->end!=FFFF_U32){
        printfln("(%02hhu:%02hhu:%02hhu -> %02hhu:%02hhu:%02hhu)",
            slpitvl->start/3600, (slpitvl->start%3600)/60, slpitvl->start%60,
            slpitvl->end/3600, (slpitvl->end%3600)/60, slpitvl->end%60
        );
    } else {
        println("(free)");
    }
}

void timelib_print_all_slpitvl(){
    for (int i=0; i<CONFIG_APP_SERVER_SLPITVL_MAX_NUM; i++){
        const struct timelib_slpitvl *slpitvl = &timelib_slpitvl_array[i];
        printf("[%hhd]: ", i);
        timelib_print_slpitvl(slpitvl);
    }
}

void timelib_check_slpitvl(){
    for (int i=0; i<CONFIG_APP_SERVER_SLPITVL_MAX_NUM; i++){
        struct timelib_slpitvl *slpitvl = &timelib_slpitvl_array[i];
        if (slpitvl->start==slpitvl->end
            ||!(0<=slpitvl->start&&slpitvl->start<86400)
            ||!(0<=slpitvl->end&&slpitvl->end<86400)
        ){
            slpitvl->start = FFFF_U32;
            slpitvl->end = FFFF_U32;
        }
    }
}

#else // CONFIG_APP_SERVER_RTC_DISABLED

#endif

#endif // CONFIG_APP_SERVER