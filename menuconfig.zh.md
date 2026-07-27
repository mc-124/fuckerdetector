# MenuConfig 中文翻译

- menu `FuckerDetector config`
    - choice `Firmware type` 固件类型
        - bool `Server` 编译服务端（探测器）的固件
        - bool `Client` 编译客户端（接收器）的固件
    - menu `Misc` 杂项设置
        - hex `Advertising company_id field value` 广告包中厂商信息字段的值
        - hex `Protocol version` 协议版本号
    - menu `Client` 客户端设置
        - int `I2C bus speed (KHz)` I2C 总线速度（KHz）
        - string `Advertising name (length <= 16 char)` 广告包名 大小不要超过 16 字节
        - bool `Enable receive server alarm` 接收服务器发来的警告广告包
        - bool `Enable transmit client alarm` 可发送客户端警告广告包
        - bool `Enable receive client alarm` 接收客户端发来的警告广告包
        - bool `Enable transmit client response` 当收到客户端警告广告包时发送客户端响应广告包
        - bool `Enable receive client response` 接收客户端响应广告包
        - bool `Advertising duration (ms)` 发送广告持续时间（ms）
        - bool `Max advertising interval (ms)` 最大广告间隔（ms）
        - bool `Min advertising interval (ms)` 最小广告间隔（ms）
        - bool `Scan interval (ms)` 扫描持续时间（ms）
        - bool `Scan window (ms)` 扫描窗口（ms
        - bool `Scan response duration (ms)` 扫描响应包的持续时间（ms）
        - bool `Settings and UI use Chinese` 把REPL中的设置和UI改成中文的
    - menu `Server` 服务端设置
        - int `I2C bus speed (KHz)` I2C 总线速度
        - string `Advertising name (length <= 16 char)` 广告包名 大小不要超过 16 字节
        - choice `RTC type` RTC 类型
            - bool `Disabled` 禁用 RTC
            - bool `DS3231` DS3231
        - int `Advertising duration (ms)` 发送广告持续时间
        - int `Max advertising interval (ms)` 最大广告间隔（ms）
        - int `Min advertising interval (ms)` 最小广告间隔（ms）
        - int `Max sleep intervals number` 最大睡眠间隔数量
        - choice `Rader type` 雷达类型
            - bool `Unknown` 未知
            - bool `HLK-LD1040 / HLK-LD1040C` HLK-LD1040(C)
        - choice `Rader trigger level` 雷达触发类型
            - bool `High-level trigger` 高电平触发
            - bool `Low-level trigger` 低电平触发
        - choice `Rader power on wait duration (ms)` 雷达上电初始化时间（ms）
        - bool `Enable low battery voltage poweroff` 启用电池低电压时自动关机
        - int `Poweroff voltage (mV)` 关机电压（mV）


我的英文稀烂但不拿英文写我怕它炸