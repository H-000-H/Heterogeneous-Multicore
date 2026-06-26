# 嵌入式多节点系统实现计划清单

## 1. 设备与环境准备

- [x] 准备 STM32F407 板子
- [x] 准备 CH32V307 板子
- [x] 准备 ESP32-C3 板子
- [x] 准备 i.MX6ULL 板子
- [x] 准备 CAN 总线物理连线及收发器
- [x] 安装交叉编译工具链（arm-linux-gnueabihf-gcc / RISC-V）
- [x] 配置开发电脑与串口调试环境
- [x] 准备 Flash / eMMC 存储及烧录工具

## 2. STM32F407 实时控制层实现

- [x] 搭建裸机或 FreeRTOS 环境
- [ ] 配置 TIM 定时器 1kHz ISR
- [ ] 实现 PWM 输出控制
- [ ] 实现定点 PID 核心算法
- [ ] 配置 DMA 无锁双缓冲数据采集
- [ ] I2C / ADC 异步非阻塞读取
- [ ] 执行器闭环健康监测（过流/断路检测）
- [ ] 心跳帧上报 CAN（0x100 等）

## 3. ESP32-C3 无线层实现

- [x] 搭建 ESP-IDF 项目
- [ ] 配置 SPI 从机读取 CH32V307 数据
- [ ] 实现稳定 2.4GHz Wi-Fi AP
- [ ] 测试与 i.MX6ULL / CH32V307 数据透传
- [ ] 支持 MQTT 上行 / 下行数据透传

## 4. i.MX6ULL 交互层实现

- [ ] 配置 Yocto Linux BSP
- [ ] 编译 LVGL 依赖及应用
- [ ] 配置 SocketCAN 驱动
- [ ] 部署本地 Mosquitto MQTT Broker
- [ ] 搭建 Nginx 静态 Web 服务（可选）
- [ ] 实现 LVGL UI 基本页面（主页/传感器/能耗/PID/OTA）
- [ ] 配置 systemd 启动顺序与自愈 rescue 服务

## 5. CH32V307 网关层实现

- [x] 配置 FreeRTOS
- [ ] 配置 SPI 外部 Flash (LittleFS / OTA 缓存)
- [ ] 实现 CAN 数据路由与令牌桶流量整形
- [ ] 实现异步 Flash 写入与擦除任务
- [ ] 实现节点心跳监控及低功耗协作
- [ ] 断电保护与 OTA B 区备份逻辑

## 6. CAN 总线与通信协议实现

- [ ] 定义 CAN 帧协议 (0x100~0x313)
- [ ] 实现心跳帧周期 200ms
- [ ] 实现 PID / 温控 / 光照 / 能耗控制帧
- [ ] 实现 Rolling Code + HMAC 安全校验（可选）
- [ ] 测试全系统 CAN 通信可靠性

## 7. OTA 升级系统实现

- [ ] 双阶段 OTA 设计
  - 阶段一：静默下载与全局验签
  - 阶段二：本地高可靠烧录
- [ ] CRC 或 SHA256 校验
- [ ] A/B 升级区逻辑
- [ ] OTA 断电恢复与重试机制
- [ ] OTA UI 进度显示与历史记录

## 8. LVGL UI 完善与功能验证

- [ ] 温湿度/气压/光照实时显示
- [ ] 能耗功率显示与控制
- [ ] PID 控制折线图 + 滑块 + 输入框
- [ ] OTA 升级目标设备选择
- [ ] 错误报警显示

## 9. 工业级安全与故障自愈

- [ ] 配置 watchdog（STM32/CH32/i.MX6ULL）
- [ ] LVGL 进程崩溃 rescue 服务
- [ ] 节点离线报警及 MQTT 上报
- [ ] 执行器安全边界 CLAMP
- [ ] 系统低功耗 Suspend-to-RAM 协作

## 10. 系统测试与调优

- [ ] 单节点功能验证
- [ ] 跨节点通信稳定性测试
- [ ] OTA 升级完整性测试
- [ ] CAN 总线负载测试
- [ ] UI 响应与性能测试
- [ ] 工业场景长时间运行验证