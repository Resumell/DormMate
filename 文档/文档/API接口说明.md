# API 接口说明

## 基础信息

- **Base URL**: `http://<服务器IP>:5000`
- **Content-Type**: `application/json`
- **设备ID**: 默认 `dormmate_01`

---

## 1. 传感器上报（核心端点）

### POST /api/report

ESP32 上报传感器数据并获取 AI 决策指令。

**请求体**：
```json
{
  "device_id": "dormmate_01",
  "sensors": {
    "light": 2470,
    "human": 1,
    "temperature": 27.5,
    "humidity": 55.0,
    "current": 150,
    "relay": 0,
    "servo": -1
  },
  "context": {
    "query_type": "normal|predict",
    "event": "human_first",
    "current_time": "14:30",
    "history": [{"date":"2026-07-08","time":"21:15"}, ...]
  }
}
```

**context 字段说明**：
| 字段 | 类型 | 说明 |
|------|------|------|
| query_type | string | "normal"(普通上报) 或 "predict"(预测请求) |
| event | string | "human_first"(首次检测到人) |
| current_time | string | 当前时间 HH:MM（预测模式必传） |
| history | array | 最近7天回寝历史（预测模式必传） |
| first_seen_time | string | 首次检测到人的时间 |
| weekday | int | 星期 0-6 |
| date | string | 日期 YYYY-MM-DD |

**返回**：
```json
{
  "actions": {
    "relay": 0,
    "servo": -1,
    "buzzer": 0,
    "tts": "当前光线太暗，是否要为您开灯",
    "oled_line1": "Temp:27.5C",
    "oled_line2": "Hum:55%",
    "preheat": 0,
    "predict_return_time": "",
    "target_temp": 24,
    "reason": "",
    "voice_notify": 1
  }
}
```

**actions 字段说明**：
| 字段 | 类型 | 说明 |
|------|------|------|
| relay | int | 继电器状态 0=关 1=开 |
| servo | int | 舵机角度(-1=不操作) |
| buzzer | int | 蜂鸣器状态 |
| tts | string | 语音播报文本 |
| oled_line1/2 | string | OLED 显示内容(两行) |
| preheat | int | 预处理触发标志 0/1 |
| voice_notify | int | 语音播报编号(见voice_module.md) |

---

## 2. 单次执行模式

### POST /api/run_once

**Token节省模式**：ESP32 调用一次即可完成采集→AI决策→执行全流程，完成后休眠。

**请求体**：
```json
{
  "device_id": "dormmate_01",
  "sensors": {
    "light": 2470,
    "human": 1,
    "temperature": 27.5,
    "humidity": 55.0
  },
  "current_time": "14:30"
}
```

**返回**：同 /api/report 的 actions 结构。

---

## 3. 服务状态

### GET /api/status

**返回**：
```json
{
  "msg": "AI后端运行中",
  "mode": "doubao",
  "db_connected": true,
  "ai_connected": true
}
```

---

## 4. 实时传感器数据

### GET /api/current

前端获取最新传感器缓存数据。

**返回**：
```json
{
  "device_id": "dormmate_01",
  "sensors": {
    "light": 2470,
    "human": 1,
    "temperature": 27.5,
    "humidity": 55.0,
    "current": 150,
    "relay": 0,
    "servo": -1
  }
}
```

---

## 5. 预测状态查询

### GET /api/predict/status?device_id=dormmate_01

**返回**：
```json
{
  "device_id": "dormmate_01",
  "today": "2026-07-09",
  "predict_time": "21:00",
  "preheat_triggered": false,
  "preference": {
    "pref_temp": 24,
    "preheat_enable": 1,
    "custom_return_time": "21:00"
  },
  "history": [
    {"date": "2026-07-08", "time": "21:15"},
    {"date": "2026-07-07", "time": "20:50"}
  ]
}
```

---

## 6. 偏好设置

### POST /api/preference/return

手机端修改用户偏好（目标温度/预处理开关/回寝时间）。

**请求体**：
```json
{
  "device_id": "dormmate_01",
  "pref_temp": 24,
  "preheat_enable": 1,
  "custom_return_time": "21:00"
}
```

**返回**：
```json
{
  "status": "ok",
  "device_id": "dormmate_01",
  "actions": {
    "voice_notify": 11
  }
}
```

> **优化**：设置保存后直接返回 voice_notify=11，不再依赖 pending_notify 标记等待下次轮询。

---

## 7. 手动触发预处理

### POST /api/preheat/trigger

手机端点"立即预处理"按钮。

**请求体**：
```json
{
  "device_id": "dormmate_01"
}
```

**返回**：
```json
{
  "status": "ok",
  "device_id": "dormmate_01",
  "msg": "已触发，设备下次上报时执行预处理"
}
```

---

## 8. 回寝历史

### GET /api/history/return?device_id=dormmate_01&days=30

**返回**：
```json
{
  "device_id": "dormmate_01",
  "history": [
    {
      "date": "2026-07-08",
      "first_seen_time": "21:15",
      "temp_at_arrival": 27.5,
      "hum_at_arrival": 55.0
    }
  ]
}
```

---

## 9. 手动控制

### POST /api/control

手机端下发控制指令（ESP32 下次上报时带走执行）。

**请求体**：
```json
{
  "device_id": "dormmate_01",
  "relay": 1
}
```

**返回**：
```json
{
  "status": "ok",
  "device_id": "dormmate_01",
  "pending": {"relay": 1}
}
```

---

## 10. 服务器时间

### GET /api/time

前端同步服务器时间。

**返回**：
```json
{
  "year": 2026,
  "month": 7,
  "day": 9,
  "hour": 14,
  "minute": 30,
  "second": 0
}
```

---

## 错误响应

所有端点统一格式：
```json
{
  "error": "错误描述信息"
}
```

常见 HTTP 状态码：
- `200`：成功
- `400`：请求数据缺失
- `500`：服务器内部错误
