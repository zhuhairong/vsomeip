# vsomeip 通信模式示例 (Demos)

基于 [vsomeip 3.7.2](https://github.com/COVESA/vsomeip) 的三种通信模式示例，展示 SOME/IP 协议的核心用法。

## 前置条件

- vsomeip 3.7.2 已编译，安装路径：`/local_ssd/zhuhairong/pytorch-ssd/zhr/vsomeip/build`
- C++14 编译器、CMake、make

## 构建

```bash
cd /local_ssd/zhuhairong/pytorch-ssd/zhr/vsomeip/zhr
mkdir -p build && cd build
cmake .. && make -j$(nproc)
```

产物在对应子目录的 build 目录下：

| 二进制 | 路径 |
|--------|------|
| `demo_service`, `demo_client` | `build/01_request_response/` |
| `publisher`, `subscriber` | `build/02_event/` |
| `field_service`, `field_client` | `build/03_field/` |

## 使用方式

所有 demo 都使用**静态路由**（无服务发现），服务端同时充当路由管理器。因此必须**先启动服务端，再启动客户端**。

清理可能残留的 vsomeip 状态：

```bash
pkill -9 demo_service demo_client publisher subscriber field_service field_client 2>/dev/null
rm -f /tmp/vsomeip.lck /tmp/vsomeip-*
```

### 1. Request/Response (请求/响应)

**通信模式**：客户端发送请求，服务端处理后返回响应。一对一同步通信。

**服务端**：注册 `0x1111/0x2222/0x3333`，收到请求后将 payload 拼接为 `"Hello " + msg` 返回。  
**客户端**：发现服务后发送 `"World"`，收到响应 `"Hello World"` 后退出。

```bash
# 终端 1：启动服务端（路由管理器）
VSOMEIP_CONFIGURATION=/absolute/path/to/zhr/01_request_response/vsomeip.json \
  ./build/01_request_response/demo_service &

# 终端 2：启动客户端
VSOMEIP_CONFIGURATION=/absolute/path/to/zhr/01_request_response/vsomeip.json \
  ./build/01_request_response/demo_client
```

预期输出：

```
[CLIENT] Service available, sending request...
[CLIENT] Sent: World
[CLIENT] Received: Hello World
```

### 2. Event (事件通知)

**通信模式**：发布者周期性推送事件，订阅者接收通知。一对多异步通信。

**发布者**：注册事件 `0x1112/0x2222/0x8001`，每 2 秒 notify 递增的 uint32 计数器。  
**订阅者**：订阅 eventgroup `0x7001`，收到事件后打印计数器值。订阅者需手动退出（Ctrl+C 或 timeout）。

```bash
# 终端 1：启动发布者（路由管理器）
VSOMEIP_CONFIGURATION=/absolute/path/to/zhr/02_event/vsomeip.json \
  ./build/02_event/publisher &

# 终端 2：启动订阅者
VSOMEIP_CONFIGURATION=/absolute/path/to/zhr/02_event/vsomeip.json \
  timeout 10 ./build/02_event/subscriber
```

预期输出：

```
[PUBLISHER] Offered event service
[PUBLISHER] Notified counter = 0
[SUBSCRIBER] Service available, subscribing...
[SUBSCRIBER] Received event: counter = 1
[SUBSCRIBER] Received event: counter = 2
...
```

### 3. Field (属性读写 + 通知)

**通信模式**：可读可写的远程属性，支持 GET 读取、SET 写入，写入后自动通知所有订阅者。结合了 Request/Response 与 Event。

**服务端**：注册 field `0x1113/0x2222/0x8002`，初始值 `"Hello"`。GET 返回当前值，SET 更新值并通过 `notify()` 广播。  
**客户端**：发现服务后订阅 field、发送 GET 请求，收到当前值后发送 SET `"Hello vsomeip!"`，收到 SET 响应后自动退出。

```bash
# 终端 1：启动服务端（路由管理器）
VSOMEIP_CONFIGURATION=/absolute/path/to/zhr/03_field/vsomeip.json \
  ./build/03_field/field_service &

# 终端 2：启动客户端
VSOMEIP_CONFIGURATION=/absolute/path/to/zhr/03_field/vsomeip.json \
  ./build/03_field/field_client
```

预期输出：

```
[FIELD_SERVICE] Offered field service
[FIELD_CLIENT] Service available
[FIELD_CLIENT] GET -> initial value
[FIELD_CLIENT] GET response: "Hello"
[FIELD_CLIENT] SET -> "Hello vsomeip!"
[FIELD_CLIENT] SET response: "Hello vsomeip!"
[FIELD_CLIENT] Event notification: "Hello vsomeip!"
```

## 配置文件说明

每个 demo 的 `vsomeip.json` 配置了应用名称、ID、服务实例映射和端口。关键字段：

- `applications`：应用名称与客户端 ID（十六进制）
- `services`：Service ID、Instance ID、通信端口（unreliable = UDP）
- `routing`：指定哪个应用作为路由管理器（通常是服务端）
- `service-discovery.enable`：设为 `"false"` 使用静态路由

## 服务/事件 ID 分配

| Demo | Service ID | Instance ID | Method/Event ID | Eventgroup ID |
|------|-----------|-------------|-----------------|---------------|
| Request/Response | `0x1111` | `0x2222` | `0x3333` | - |
| Event | `0x1112` | `0x2222` | `0x8001` | `0x7001` |
| Field | `0x1113` | `0x2222` | GET `0x0001`, SET `0x0002`, Event `0x8002` | `0x7002` |

## 三种模式对比

| 维度 | Request/Response | Event | Field |
|------|-----------------|-------|-------|
| **通信方向** | 客户端 → 服务端 → 客户端 | 发布者 → 所有订阅者 | 客户端 ↔ 服务端（双向），服务端 → 所有订阅者 |
| **消息流** | 一问一答，每个请求对应一个响应 | 发布者主动推送，订阅者被动接收 | GET 请求 → 响应；SET 请求 → 响应 + 广播通知 |
| **耦合方式** | 1:1，客户端必须知道服务端和方法 | 1:N，发布者不感知订阅者 | 1:N，客户端读写属性时与属性交互，变更通知推送给所有订阅者 |
| **触发方式** | 客户端主动调用 | 发布者按时间/事件触发 | 客户端主动读写触发，服务端变更后自动通知 |
| **核心 vsomeip API** | `send()` / 响应 `MT_RESPONSE` | `offer_event()` + `notify()` / `subscribe()` | `offer_event(ET_FIELD)` / `request_event(ET_FIELD)` + GET/SET 方法 + `notify()` |
| **Eventgroup** | 不需要 | 必需（订阅的基础） | 必需（与 Event 相同） |
| **事件类型** | - | `ET_EVENT` | `ET_FIELD` |
| **适用场景** | RPC 调用、查询/操作远程服务 | 状态广播、传感器数据推送 | 远程属性读写（如车辆座椅位置、空调温度），需要"读取当前值 + 监听变更" |

### 典型使用场景

| 模式 | 汽车领域示例 | 通用软件示例 |
|------|-------------|-------------|
| Request/Response | 车门解锁指令、查询车速 | HTTP API、远程过程调用 |
| Event | 方向盘转角上报、电池电压变化通知 | WebSocket 推送、消息队列订阅 |
| Field | 空调目标温度（可读可写，变更通知） | 配置中心远程配置项（读/写/监听变更） |
