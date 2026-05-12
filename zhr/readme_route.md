# vsomeip 路由内部实现原理

本文深入分析 vsomeip 3.7.2 的路由（Routing）内部机制，涵盖架构设计、启动流程、注册握手、消息路由、UDS 通信、IPC 协议和线程模型。

---

## 1. 架构概述

vsomeip 的本地通信采用 **星型拓扑**：一个中心化的 **路由管理器（Routing Manager）** 作为枢纽，所有应用进程通过 Unix Domain Socket (UDS) 与之相连。

```mermaid
graph TB
    RM["路由管理器
    routing_manager_impl
    
    充当中央交换机"] 
    
    subgraph APP_A["进程 A - 服务端"]
        AI_A["application_impl"]
        RC_A["routing_manager_client
        (路由代理)"]
        AI_A --> RC_A
    end

    subgraph APP_B["进程 B - 客户端"]
        AI_B["application_impl"]
        RC_B["routing_manager_client
        (路由代理)"]
        AI_B --> RC_B
    end
    
    subgraph APP_C["进程 C - 订阅者"]
        AI_C["application_impl"]
        RC_C["routing_manager_client
        (路由代理)"]
        AI_C --> RC_C
    end

    RM <--"UDS 连接"--> RC_A
    RM <--"UDS 连接"--> RC_B
    RM <--"UDS 连接"--> RC_C
    
    style RM fill:#f9f,stroke:#333,stroke-width:3px
```

**关键设计决策：所有本地消息必须经过路由管理器。** 服务端和客户端不在进程中直接通信，而是通过路由管理器中转。这带来几个好处：

- **安全策略集中管控**：路由管理器可检查每条消息的权限
- **服务发现统一**：所有 offer/request 信息汇聚在路由管理器
- **订阅管理集中**：事件通知的分发由路由管理器完成

---

## 2. 核心组件与类层次

```mermaid
classDiagram
    class application {
        <<interface>>
        +init()
        +start()
        +stop()
        +offer_service()
        +request_service()
        +send()
        +notify()
        +register_message_handler()
    }
    
    class application_impl {
        -client_ : client_t
        -state_ : state_type_e
        -routing_ : routing_manager_client
        -routing_app_ : routing_application
        -configuration_ : configuration
        -io_ : io_context
        -handlers_ : deque~sync_handler~
        -dispatchers_ : map~thread~
        +on_message()
        +on_state()
        +on_availability()
    }
    
    class routing_manager_host {
        <<interface>>
        +on_message()
        +on_state()
        +on_availability()
        +get_client()
        +is_routing()
    }
    
    class routing_manager_base {
        #host_ : routing_manager_host
        #io_ : io_context
        #configuration_ : configuration
        +send_local()
        +get_serializer()
        +get_deserializer()
    }
    
    class routing_manager_impl {
        -stub_ : routing_manager_stub
        -discovery_ : service_discovery
        -ep_mgr_impl_ : endpoint_manager_impl
        -local_services_table_
        +offer_service()
        +on_message()
        +find_local_client()
    }
    
    class routing_manager_stub {
        -uds_root_ : local_server
        -routing_info_
        +on_message()
    }
    
    class routing_manager_client {
        -state_machine_ : routing_client_state_machine
        -sender_ : local_endpoint
        -uds_receiver_ : local_server
        +send()
        +notify()
        +offer_service()
        +register_application()
    }
    
    class routing_client_state_machine {
        +start_registration()
        +registered()
        +deregistered()
        +await_registered()
    }

    application <|-- application_impl : implements
    application_impl ..|> routing_manager_host
    routing_manager_base <|-- routing_manager_impl
    routing_manager_base <|-- routing_manager_client
    routing_manager_impl *-- routing_manager_stub
    routing_manager_client *-- routing_client_state_machine
    routing_manager_impl ..|> routing_manager_stub_host
```

### 组件角色

| 组件 | 所在进程 | 职责 |
|------|---------|------|
| `application_impl` | 每个应用进程 | 用户 API 的实现，管理 handler 注册与分发 |
| `routing_manager_impl` | 路由管理器进程 | 中央交换机，维护服务路由表，转发消息 |
| `routing_manager_stub` | 路由管理器进程 | UDS 服务端，接受客户端连接，解析 IPC 协议命令 |
| `routing_manager_client` | 非路由应用进程 | 路由代理，通过 UDS 连接到路由管理器，发送/接收消息 |
| `routing_client_state_machine` | 非路由应用进程 | 管理注册状态机 DEREGISTERED → REGISTERING → REGISTERED |

---

## 3. 应用启动流程

从用户代码 `runtime::get()->create_application("name")` 到应用就绪的完整序列：

```mermaid
sequenceDiagram
    participant User as 用户代码
    participant App as application_impl
    participant Config as configuration_impl
    participant RC as routing_manager_client
    participant RM as routing_manager_impl
    participant Stub as routing_manager_stub

    User->>App: create_application("demo_service")
    Note over App: state = ST_DEREGISTERED<br/>client = 0xFFFF (UNSET)
    
    User->>App: init()
    App->>Config: 加载 JSON 配置文件
    Note over App: 解析 applications、services、routing 等字段
    
    alt 此应用是路由管理器
        App->>RM: 创建 routing_manager_impl
        RM->>Stub: 创建 routing_manager_stub
        RM->>RM: 初始化端点管理器
    else 非路由应用
        App->>RC: 创建 routing_manager_client
        RC->>RC: 创建路由客户端状态机
    end
    
    User->>App: start()
    App->>App: 创建主分发线程 (m_dispatch)
    
    alt 路由管理器
        App->>RM: start()
        RM->>Stub: 创建 UDS 路由根套接字 /tmp/vsomeip-0
        Stub->>Stub: async_accept() 开始监听客户端连接
    else 非路由应用
        App->>RC: start()
        RC->>RC: restart_sender()
        RC->>RC: 连接 /tmp/vsomeip-0
        RC-->>Stub: ASSIGN_CLIENT 命令
        Stub->>Stub: 分配客户端 ID
        Stub-->>RC: ASSIGN_CLIENT_ACK
        RC->>RC: register_application()
        RC->>RC: state = ST_REGISTERED
        RC-->>App: on_state(ST_REGISTERED)
        App->>App: 分发线程调用用户 on_state 回调
    end
    
    App->>App: 创建 IO 线程 (io00, io01, ...)
    App->>App: io_.run() 阻塞运行
```

### 状态机转换

```
 ST_DEREGISTERED ──start_registration()──> ST_REGISTERING ──registered()──> ST_REGISTERED
        ^                                       │
        └──────────deregistered()───────────────┘
```

- **ST_DEREGISTERED**：初始状态，未连接到路由管理器
- **ST_REGISTERING**：已发起 ASSIGN_CLIENT，等待 ACK
- **ST_REGISTERED**：注册完成，可正常通信

---

## 4. 注册握手 (ASSIGN_CLIENT / ASSIGN_CLIENT_ACK)

每个非路由应用启动时，需要与路由管理器完成注册握手才能获得合法客户端 ID。

```mermaid
sequenceDiagram
    participant Client as routing_manager_client<br/>(应用进程)
    participant Sock as UDS 套接字
    participant Tmp as tmp_connection<br/>(路由管理器)
    participant Stub as routing_manager_stub
    
    Client->>Client: restart_sender()
    Client->>Client: local_endpoint::create_client_ep()
    Client->>Sock: socket(AF_UNIX, SOCK_STREAM, 0)
    Client->>Sock: connect("/tmp/vsomeip-0")
    
    Sock-->>Tmp: accept 新连接
    Tmp->>Tmp: async_receive() 等待命令
    
    Client->>Sock: send ASSIGN_CLIENT<br/>[cmd=0x00, client_id=0xFFFF]
    Sock-->>Tmp: receive_cbk() 收到命令
    Tmp->>Tmp: 解码 ASSIGN_CLIENT
    Tmp->>Stub: assign_client()
    Stub->>Stub: utility::request_client_id()
    Stub->>Stub: 存储认证信息 (UID, GID, PID)
    Stub-->>Tmp: 分配客户端 ID (如 0x4444)
    Tmp->>Tmp: hand_over() 创建 persistent local_endpoint
    
    Stub-->>Client: send ASSIGN_CLIENT_ACK<br/>[cmd=0x01, client_id=0x4444]
    
    Client->>Client: on_client_assign_ack()
    Client->>Client: host_->set_client(0x4444)
    Client->>Client: state_machine_->registered()
    Note over Client: state = ST_REGISTERED
    
    Client->>Client: register_application()
    Client->>Client: 启动 keepalive 定时器
    Client->>Client: 发送队列中积压的 pending 命令
    Client->>Client: host_->on_state(ST_REGISTERED)
```

**关键细节：**

- 路由根套接字路径为 `/tmp/vsomeip-0`（配置中 `network` 字段不为空时为 `/tmp/vsomeip-{network}-0`）
- 客户端 ID 优先从配置文件 `applications[].id` 读取，未配置则自动分配
- 每个应用被分配唯一的 UDS 接收端路径 `/tmp/vsomeip-{client_id}`（如 `/tmp/vsomeip-4444`）
- 路由管理器的客户端 ID 固定为 `0x0000`

---

## 5. 服务注册与发现

### 5.1 offer_service — 服务端注册服务

```mermaid
sequenceDiagram
    participant User as 服务端用户代码
    participant App as application_impl
    participant RC as routing_manager_client
    participant Stub as routing_manager_stub
    participant RM as routing_manager_impl

    User->>App: offer_service(0x1111, 0x2222)
    App->>RC: offer_service()
    RC->>RC: 序列化 OFFER_SERVICE 命令 (0x10)
    RC-->>Stub: send_local(sender_, cmd)
    
    Stub->>Stub: on_message(OFFER_SERVICE)
    Stub->>Stub: 更新 routing_info_{client->service+instance}
    Stub->>Stub: 分发认证信息 (distribute_creds)
    
    Stub->>RM: host_->on_offer_service()
    RM->>RM: 创建 ServiceInfo 加入 local_services_table_
    RM->>RM: on_availability() 通知已 request 该服务的客户端
    
    alt 若启用了 Service Discovery
        RM->>RM: discovery_->offer_service()
        Note over RM: 通过 UDP 多播广播服务可用性
    end
```

### 5.2 request_service — 客户端发现服务

```mermaid
sequenceDiagram
    participant User as 客户端用户代码
    participant App as application_impl
    participant RC as routing_manager_client
    participant Stub as routing_manager_stub
    participant RM as routing_manager_impl

    User->>App: request_service(0x1111, 0x2222)
    App->>RC: request_service()
    RC->>RC: 序列化 REQUEST_SERVICE 命令 (0x14)
    RC-->>Stub: send_local(sender_, cmd)
    
    Stub->>RM: host_->on_request_service()
    
    alt 服务已注册（有服务端在提供）
        RM->>RM: 查表发现 0x1111/0x2222 由客户端 X 提供
        RM-->>RC: on_availability(0x1111, 0x2222, true)
        RC->>App: 用户 on_availability 回调
    else 服务未注册
        RM->>RM: 加入 requested_services_ 等待列表
        Note over RM: 当有新的 OFFER_SERVICE 时重新检查
    end
```

---

## 6. 消息路由 — 请求/响应全流程

这是最核心的消息通路：客户端发送请求 → 路由管理器转发 → 服务端处理 → 响应返回。

```mermaid
sequenceDiagram
    participant Client as 客户端应用
    participant CRC as Client RC
    participant CSock as 客户端 UDS Sender
    participant Stub as routing_manager_stub
    participant RM as routing_manager_impl
    participant SSock as 服务端 UDS Receiver
    participant SRC as Service RC
    participant Service as 服务端应用

    Note over Client,Service: ═══════════ 请求路径 (REQUEST) ═══════════
    
    Client->>Client: send(request_msg)<br/>session_id++, client_id=0x5555
    Client->>CRC: routing_manager_client::send()
    CRC->>CRC: 序列化 SOME/IP 消息
    CRC->>CRC: 包装 SEND 命令头部 [cmd=0x18, target_service]
    CRC->>CSock: send(send_buffer)
    CSock->>CSock: async_write() 经 UDS 发送到路由根
    CSock-->>Stub: [UDS] SEND 命令
    
    Stub->>Stub: 解析 SEND 命令，提取 SOME/IP 消息
    Stub->>RM: host_->on_message(msg, client_data)
    RM->>RM: 检查安全策略
    RM->>RM: find_local_client(0x1111, 0x2222)
    Note over RM: 查 local_services_table_<br/>发现服务端 client=0x4444
    RM->>RM: find_routing_endpoint(0x4444)
    RM->>SSock: send_local(target=0x4444)
    
    SSock-->>SRC: [UDS] 转发 SEND 命令
    SRC->>SRC: 解析 SEND 命令，提取 SOME/IP 消息
    SRC->>SRC: 安全检查
    SRC->>Service: host_->on_message(msg)
    Service->>Service: 查找 (service, instance, method) handler
    Service->>Service: 分发线程调用用户回调
    
    Note over Client,Service: ═══════════ 响应路径 (RESPONSE) ═══════════
    
    Service->>Service: send(response_msg)<br/>client_id=0x5555(原始调用者)
    Service->>SRC: routing_manager_client::send()
    SRC->>SRC: 序列化 + 包装 SEND 命令
    SRC->>SSock: send(send_buffer)
    SSock-->>Stub: [UDS] SEND 命令
    
    Stub->>RM: host_->on_message(msg)
    RM->>RM: 提取 target_client = 0x5555
    RM->>RM: find_routing_endpoint(0x5555)
    RM->>CSock: send_local(target=0x5555)
    
    CSock-->>CRC: [UDS] SEND 命令
    CRC->>CRC: 提取 SOME/IP 响应消息
    CRC->>Client: host_->on_message(resp)
    Client->>Client: 查找响应 handler
    Client->>Client: 用户回调收到响应
```

### 关键点

1. **序列化与反序列化**：`routing_manager_base` 维护序列化器/反序列化器池（每个 IO 线程一个），将 SOME/IP 消息序列化为字节流
2. **目标查找**：`routing_manager_impl::find_local_client()` 查 `local_services_table_` 映射 `(service, instance) → client_id`
3. **消息头部**：原始 SOME/IP 消息中的 `client_id` 字段记录了发起者，响应时用于回寻
4. **安全策略**：每条消息都经过 `check_security_policy()` 验证

---

## 7. 事件通知路由

事件通知是一对多的广播模式，发布者通知一次，路由管理器分发给所有订阅者。

```mermaid
sequenceDiagram
    participant Pub as 发布者应用
    participant PRC as Publisher RC
    participant Stub as routing_manager_stub
    participant RM as routing_manager_impl
    participant SubSock as 订阅者 UDS
    participant SubRC as Subscriber RC
    participant Sub as 订阅者应用

    Note over Pub,Sub: ═══════════ 发布者 notify ═══════════
    
    Pub->>Pub: notify(0x1112, 0x2222, 0x8001, payload)
    Pub->>PRC: routing_manager_client::notify()
    PRC->>PRC: 序列化 NOTIFY 命令 (0x19)
    PRC-->>Stub: send_local(sender_, cmd)
    
    Stub->>Stub: on_message(NOTIFY)
    Stub->>RM: host_->on_notification()
    
    RM->>RM: 查找事件 0x8001 在 events_ 映射
    RM->>RM: 查找 eventgroup 0x7001 的成员列表
    Note over RM: eventgroup 成员 = 所有订阅的客户端
    
    alt 本地订阅者
        RM->>SubSock: send_local(ep_of_subscriber, NOTIFY)
        SubSock-->>SubRC: [UDS] NOTIFY 命令
        SubRC->>SubRC: 提取事件 payload
        SubRC->>Sub: host_->on_message(event_msg)
    end
    
    alt 远程订阅者
        RM->>RM: 通过 boardnet_endpoint 发送
        Note over RM: UDP/TCP → 远程主机
    end
```

### 事件注册流程

订阅者必须完成两步才能接收事件：

1. **request_event**：告诉路由管理器本应用要监听某个事件
2. **subscribe**：订阅 eventgroup，加入广播列表

```
订阅者                   路由管理器
   │                        │
   │── request_event() ────>│  注册事件到 events_ 映射
   │                        │
   │── subscribe() ────────>│  加入 eventgroup 成员表
   │                        │
   │  <── NOTIFY ───────────│  发布者 notify 时转发
```

---

## 8. UDS 通信机制

### 8.1 套接字路径约定

| 角色 | 路径 | 创建者 | 用途 |
|------|------|--------|------|
| 路由根 | `/tmp/vsomeip-0` | `routing_manager_stub` | 监听客户端连接 |
| 应用接收端 | `/tmp/vsomeip-{client_id}` | `routing_manager_client` | 接收路由管理器转发消息 |
| 示例：服务端 | `/tmp/vsomeip-4444` | demo_service 的 RC | 服务端接收消息 |
| 示例：客户端 | `/tmp/vsomeip-5555` | demo_client 的 RC | 客户端接收消息 |

### 8.2 套接字拓扑

```mermaid
graph TB
    subgraph "路由管理器进程"
        Stub["routing_manager_stub"]
        Acceptor["UDS Acceptor<br/>@ /tmp/vsomeip-0"]
        Stub --> Acceptor
    end
    
    subgraph "服务端进程 (0x4444)"
        RC_A["routing_manager_client"]
        Sender_A["UDS Sender<br/>→ 路由根"]
        Receiver_A["UDS Receiver<br/>@ /tmp/vsomeip-4444"]
        RC_A --> Sender_A
        RC_A --> Receiver_A
    end
    
    subgraph "客户端进程 (0x5555)"
        RC_B["routing_manager_client"]
        Sender_B["UDS Sender<br/>→ 路由根"]
        Receiver_B["UDS Receiver<br/>@ /tmp/vsomeip-5555"]
        RC_B --> Sender_B
        RC_B --> Receiver_B
    end
    
    Sender_A <--> Acceptor
    Sender_B <--> Acceptor
    Stub -. "转发消息给服务端" .-> Receiver_A
    Stub -. "转发消息给客户端" .-> Receiver_B
    
    style Acceptor fill:#f9f,stroke:#333,stroke-width:2px
```

### 8.3 连接生命周期

```
路由管理器端（服务端）：
  1. init_routing_endpoint()
     → local_acceptor_uds_impl::init()
       → open() 打开套接字
       → unlink() 清理陈旧套接字文件
       → bind() 绑定到 /tmp/vsomeip-0
       → listen() 开始监听
       → chmod() 设置权限
  2. local_server::start()
     → async_accept() 循环接受连接
  3. 客户端连入
     → tmp_connection 处理握手
     → hand_over() → 创建 persistent local_endpoint
     → add_connection() → 注册新客户端

客户端端：
  1. restart_sender()
     → local_endpoint::create_client_ep()
       → local_socket_uds_impl
       → prepare_connect() 打开套接字
       → async_connect("/tmp/vsomeip-0")
  2. 连接成功 → 发送 CONFIG 命令
  3. 接收 ASSIGN_CLIENT_ACK → 握手完成
```

### 8.4 双工通信

每个非路由应用维护 **两条 UDS 通道**：

- **Sender（发送者）**：客户端 → 路由管理器。用于发送命令 (OFFER_SERVICE, SEND, NOTIFY 等)
- **Receiver（接收者）**：路由管理器 → 客户端。路由管理器在此端口上推送消息给客户端

这种设计使得路由管理器可以主动向客户端推送消息（如事件通知、可用性变更），而不需要客户端持续轮询。

---

## 9. IPC 协议格式

所有 UDS 传输使用自定义二进制协议（非 SOME/IP 协议，而是内部进程间控制协议）。

### 通用命令头部 (9 字节)

```
Byte 0:      命令 ID
Bytes 1-2:   IPC 协议版本 (大端 uint16)
Bytes 3-4:   客户端 ID (大端 uint16)
Bytes 5-8:   负载大小 (大端 uint32)
Bytes 9+:    命令特有负载
```

### 主要命令 ID

| 命令 | ID | 方向 | 说明 |
|------|----|------|------|
| `ASSIGN_CLIENT` | `0x00` | 客户端 → 路由 | 请求分配客户端 ID |
| `ASSIGN_CLIENT_ACK` | `0x01` | 路由 → 客户端 | 分配确认 |
| `ROUTING_INFO` | `0x05` | 路由 → 客户端 | 服务路由信息广播 |
| `PING` | `0x07` | 路由 → 客户端 | 心跳探测 |
| `PONG` | `0x08` | 客户端 → 路由 | 心跳回复 |
| `OFFER_SERVICE` | `0x10` | 客户端 → 路由 | 注册服务 |
| `STOP_OFFER_SERVICE` | `0x11` | 客户端 → 路由 | 停止注册 |
| `SUBSCRIBE` | `0x12` | 客户端 → 路由 | 订阅事件组 |
| `REQUEST_SERVICE` | `0x14` | 客户端 → 路由 | 请求发现服务 |
| `SEND` | `0x18` | 双向 | 传输 SOME/IP 消息 |
| `NOTIFY` | `0x19` | 客户端 → 路由 | 发布事件通知 |
| `NOTIFY_ONE` | `0x1A` | 客户端 → 路由 | 单播通知 |
| `REGISTER_EVENT` | `0x1B` | 客户端 → 路由 | 注册事件 |
| `CONFIG` | `0x31` | 双向 | 配置信息交换 |

### SEND 命令格式 (15 字节头部 + 负载)

```
Bytes 0-8:   通用命令头部 (cmd=SEND_ID=0x18)
Byte 9:      status 检查标志
Byte 10:     保留
Bytes 11-12: 实例 ID (大端 uint16)
Byte 13:     可靠性标志
Byte 14:     target 端口
Bytes 15+:   SOME/IP 消息字节流 (SOME/IP 头部 + payload)
```

SOME/IP 消息本身包含：
- Service ID, Instance ID, Method ID
- Client ID, Session ID
- Message Type (REQUEST=0x00, RESPONSE=0x80, NOTIFICATION=0x02 等)
- Return Code
- Payload 数据

---

## 10. 线程模型

```mermaid
graph TB
    subgraph "应用进程"
        subgraph "主线程 (start() 的调用者)"
            IO00["io00 线程<br/>io_.run()"]
        end
        
        subgraph "IO 线程池"
            IO01["io01 线程<br/>io_.run()"]
            IO02["io02 线程<br/>io_.run()"]
        end
        
        subgraph "分发线程"
            DISPATCH["m_dispatch 线程<br/>处理 handlers_ 队列"]
            DISPATCH2["secondary_dispatch<br/>慢 handler 超时触发的备用分发"]
        end
        
        IO00 -->|async I/O 完成回调| DISPATCH
        IO01 -->|async I/O 完成回调| DISPATCH
        IO02 -->|async I/O 完成回调| DISPATCH
    end

    subgraph "路由管理器进程"
        RM_IO["IO 线程池<br/>处理所有客户端连接<br/>+ 网络 I/O"]
    end
    
    RM_IO <--"UDS"--> IO00
    RM_IO <--"UDS"--> IO01
```

### 线程职责

| 线程 | 数量 | 职责 |
|------|------|------|
| `io00` | 1 | `start()` 的调用者线程，进入 `io_.run()` 事件循环 |
| `io01, io02...` | 可配置（默认 1-2） | 异步 I/O 事件循环，处理套接字读写、定时器 |
| `m_dispatch` | 1 | 从 `handlers_` 队列取出回调并执行（用户 message_handler、state_handler 等） |
| `secondary_dispatch` | 按需（最多 `max_dispatchers_`） | 当 `m_dispatch` 上一个回调执行超过 `max_dispatch_time_` 时创建 |

### 消息分发流程

```
IO 线程收到 UDS 数据
  → 反序列化 IPC 命令
  → 路由查找、转发
  → application_impl::on_message() 被调用
    → 将用户回调封装为 sync_handler
    → 推入 handlers_ 队列
    → 通知 dispatcher_condition_
      → m_dispatch 线程被唤醒
        → 从队列弹出 handler
        → 调用用户回调
```

这种 **IO 线程与分发线程分离** 的设计确保：
- IO 线程不会被用户代码阻塞
- 用户回调按顺序串行执行（一个分发线程）
- 长时间运行的回调不会阻塞 IO 操作

---

## 11. 以 field demo 为例看完整路由

以 `03_field` demo 为例，演示一条 SET 请求从客户端到服务端再到订阅者的完整路径：

```mermaid
sequenceDiagram
    participant FC as field_client (0x5555)
    participant FS as field_service (0x4444)

    Note over FC,FS: 第 1 步：客户端订阅 Field
    
    FC->>FC: register_state_handler()
    FC->>FC: init() + start()
    FC->>FC: 注册握手完成 → on_state(ST_REGISTERED)
    FC->>FC: request_service(0x1113, 0x2222)
    
    FS->>FS: init() + start() + 注册完成
    FS->>FS: offer_event(ET_FIELD) + offer_service()
    FS-->>FC: on_availability(true)
    
    FC->>FC: request_event(0x1113, 0x2222, 0x8002)
    FC->>FC: subscribe(0x1113, 0x2222, 0x7002)
    
    Note over FC,FS: 第 2 步：GET 请求/响应
    
    FC->>FC: send(GET request)
    FC->>RM: SEND 命令 (cmd=0x18)
    RM-->>FS: 转发 GET 请求
    FS->>FS: on_get() → 返回当前值 "Hello"
    FS->>RM: SEND 命令 (RESPONSE)
    RM-->>FC: 转发响应 "[FIELD_CLIENT] GET response: Hello"
    
    Note over FC,FS: 第 3 步：SET 请求 + 事件通知
    
    FC->>FC: send(SET request, "Hello vsomeip!")
    FC->>RM: SEND 命令
    RM-->>FS: 转发 SET 请求
    FS->>FS: on_set() → 更新字段值
    FS->>FS: notify(0x1113, 0x2222, 0x8002)
    FS->>RM: NOTIFY 命令 (cmd=0x19)
    RM->>RM: 查 eventgroup 0x7002 成员 = [0x5555]
    RM-->>FC: 转发 NOTIFY "[FIELD_CLIENT] Event notification: Hello vsomeip!"
    FS->>RM: SEND 命令 (SET RESPONSE)
    RM-->>FC: 转发响应 "[FIELD_CLIENT] SET response: Hello vsomeip!"
```

---

## 总结

vsomeip 的路由架构核心思想：

1. **中心化路由**：所有消息经由一个路由管理器进程转发，实现安全管控和统一服务发现
2. **UDS IPC**：通过 Unix Domain Socket 实现高效的本地进程间通信，利用 SO_PEERCRED 进行认证
3. **双通道设计**：每个应用维护一条发送通道和一条接收通道，支持路由管理器主动推送
4. **二进制协议**：自定义 IPC 命令协议（非 SOME/IP），轻量高效
5. **IO 与分发分离**：异步 IO 线程处理网络收发，分发线程串行执行用户回调，互不阻塞

这种架构在保证高效本地通信的同时，提供了集中化的服务发现、事件订阅和安全管理能力，适合汽车电子等对实时性和安全性有严格要求的场景。
