# Project Hard Rules

# 一、禁止事项

## 禁止动态内存

禁止：

```c
malloc
free
calloc
realloc
```

---

## 禁止长时间阻塞

禁止：

```c
HAL_Delay(>100ms)
osDelay(>100ms)
while(wait)
```

---

## 禁止死循环等待Socket

禁止：

```c
while(getSn_SR(sn) != SOCK_ESTABLISHED)
```

必须：

增加超时机制。

---

## 禁止业务层直接操作MQTT

必须：

通过Queue通信。

---

# 二、FreeRTOS规则

## 所有任务必须可恢复

禁止：

任务内部永久卡死。

---

## 所有任务必须定期释放CPU

必须：

```c
osDelay()
```

---

## 所有任务必须支持Watchdog

禁止：

卡死情况下继续喂狗。

---

# 三、网络规则

## MQTT断线

仅允许：

MQTT层重连。

禁止：

整个网络重置。

---

## Link断开

必须：

自动检测。

恢复后：

自动DHCP。

---

## MQTT重连

必须：

指数退避：

```text
1s
2s
5s
10s
30s
```

禁止：

疯狂重连。

---

# 四、代码质量

## 所有函数必须有返回值检查

禁止：

忽略：

```c
ret
```

---

## 所有模块必须分层

禁止：

网络、业务、硬件混合。

---

# 五、Keil工程要求

禁止：

Agent自动修改：

* .uvprojx
* .uvoptx

除非用户明确要求。

---

## 新增文件

必须提醒用户：

手动加入Keil工程。


