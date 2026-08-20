* 开发中的项目，不能跑

# fuckerdetector
一个基于ESP32C3的简易行人探测器，我将会把它放在我学校的班门口，防止领导巡堂打扰我的睡眠

本项目使用ESP-IDF v6.0.2，依赖项请看 `main/idf_component.yml`

## 引脚
|Pin    |Mode           |Type       |Desc                   |
|-      |-              |-          |-                      |
|GPIO0  |Input          |All        |按钮或雷达触发信号
|GPIO1  |ADC_CHANNEL1   |All        |读取1/2分压后的电池电压
|GPIO2  |Floating       |All        |NC
|GPIO3  |UART1_Rx       |ServerOnly |和雷达通信
|GPIO4  |UART1_Tx       |ServerOnly |和雷达通信
|GPIO5  |Output         |All        |控制外设
|GPIO6  |I2C_SDA        |All        |和RTC/OLED通信
|GPIO7  |I2C_SCL        |All        |和RTC/OLED通信
|GPIO8  |Output         |All        |板载OLED
|GPIO9  |Input          |All        |BOOT引脚，不使用
|GPIO10 |Input          |All        |启动时进入CLI用的引脚
|GPIO20 |UART0_Rx       |All        |控制台
|GPIO21 |UART0_Tx       |All        |控制台

## 所使用的零件
我不提供设计图和电路图

### Client
* ESP32C3 Supermini/Promini (ESP32C3FN4)
* SSD1306 (128x64)
---
* MT3608 (升压到5v)
* TP4056
* 3.7v 锂电池
* 几个10k电阻
* 几个按钮
* 几个拨动开关
* 一堆电线
* 空电路板

### Server
* ESP32C3 Supermini/Promini (ESP32C3FN4)
* HLK-LD1040
* DS3231（可去 menuconfig 的 `FuckerDetector config`>`Server`>`Server RTC type` 选项选择 `Disabled` 去禁用）
---
* MT3608（升压到5v）
* TP4056
* 3.7v 锂电池
* 几个100k电阻
* 几个10k电阻
* 几个拨动开关
* 几个按钮
* 一堆电线
* 空电路板

## 安装

### 编译
在 ESP-IDF 命令行中：

    $ idf.py build

* 编译服务端（探测器）或客户端（接收器）需要在 menuconfig 的 `FuckerDetector config`>`Firmware type` 选项中选择固件类型
    - `Client` 表示客户端固件
    - `Server` 表示服务端固件

### 烧录
在 ESP-IDF 命令行中：

    $ idf.py flash


### 注意
当因 u8g2 库找不到 driver/gpio.h 而编译失败时，去 managed_components/nixy4__u8g2/CMakeLists.txt，找到有 `REQUIRES` 的那行，追加 `esp_driver_gpio`，就像这样：

```cmake
    REQUIRES driver esp_driver_i2c esp_driver_spi esp_driver_gpio
                                                 ^^^^^^^^^^^^^^^^
```
