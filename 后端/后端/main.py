"""
DormMate 智能宿舍管家 — Flask 后端主程序
架构：ESP32 → Flask 后端 → 豆包 AI
六大场景：语音直控、环境异常AI询问、回寝检测、预处理预测、手机远程控制、数据看板
"""
import os
import re
import json
import sqlite3
import requests
from datetime import datetime, timedelta
from flask import Flask, request, jsonify, send_from_directory
from flask_cors import CORS
from dotenv import load_dotenv

load_dotenv()

app = Flask(__name__)
CORS(app)

# ========== 全局状态缓存 ==========

# 实时传感器缓存（ESP32 每次上报时更新）
latest_sensors = {
    "light": 0, "human": 0, "temperature": 26.5,
    "humidity": 60, "current": 0, "relay": 0, "servo": -1
}

# 手机端 pending 控制指令（ESP32 下次上报时带走执行）
pending_control = {"relay": None, "servo": None, "buzzer": None}

# 强制预处理标记（手机端点"立即预处理"后设置，下次上报时触发）
pending_preheat = {"trigger": False, "reason": ""}

# 数据库路径
DB_PATH = os.path.join(os.path.dirname(os.path.abspath(__file__)), "dormmate.db")

# AI 配置（从环境变量读取密钥）
ARK_API_KEY = os.environ.get("ARK_API_KEY")
if not ARK_API_KEY:
    raise ValueError("ARK_API_KEY 未设置，请在 .env 文件中配置")

ARK_API_URL = "https://ark.cn-beijing.volces.com/api/v3/responses"
MODEL_NAME = os.environ.get("MODEL_NAME", "doubao-seed-2-0-mini-260428")

# AI 连接状态追踪
_ai_online = True
_last_ai_error = ""


# ========== 数据库初始化 ==========

def init_db():
    """初始化数据库表结构（三表：回寝历史、偏好设置、预测记录）"""
    conn = sqlite3.connect(DB_PATH)
    c = conn.cursor()

    c.execute('''
        CREATE TABLE IF NOT EXISTS return_history (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            device_id TEXT NOT NULL,
            date TEXT NOT NULL,
            first_seen_time TEXT,
            weekday INTEGER,
            temp_at_arrival REAL,
            hum_at_arrival REAL,
            created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
        )
    ''')

    c.execute('''
        CREATE TABLE IF NOT EXISTS preferences (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            device_id TEXT NOT NULL,
            pref_temp INTEGER DEFAULT 24,
            preheat_enable INTEGER DEFAULT 1,
            custom_return_time TEXT,
            pending_notify INTEGER DEFAULT 0,
            created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
            UNIQUE(device_id)
        )
    ''')

    c.execute('''
        CREATE TABLE IF NOT EXISTS predictions (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            device_id TEXT NOT NULL,
            predict_date TEXT,
            predict_time TEXT,
            preheat_triggered INTEGER DEFAULT 0,
            reason TEXT,
            created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
        )
    ''')

    # 新增：preheat_done_today 标记（防止同一天重复触发预处理）
    try:
        c.execute("ALTER TABLE predictions ADD COLUMN preheat_done_today INTEGER DEFAULT 0")
    except sqlite3.OperationalError:
        pass  # 字段已存在则跳过

    conn.commit()
    conn.close()


def get_db():
    """获取数据库连接（行模式）"""
    conn = sqlite3.connect(DB_PATH)
    conn.row_factory = sqlite3.Row
    return conn


def get_custom_return_time(device_id="dormmate_01"):
    """读取用户手动设定的回寝时间"""
    try:
        conn = sqlite3.connect(DB_PATH)
        conn.row_factory = sqlite3.Row
        c = conn.cursor()
        c.execute("SELECT custom_return_time FROM preferences WHERE device_id=?", (device_id,))
        row = c.fetchone()
        conn.close()
        return row["custom_return_time"] if row else None
    except Exception:
        return None


def check_db_connection():
    """检查数据库连接状态"""
    try:
        conn = sqlite3.connect(DB_PATH)
        conn.close()
        return True
    except Exception:
        return False


# ========== AI 调用核心 ==========

def ask_ai(sensors, context=None, device_id="dormmate_01"):
    """调用豆包 AI 进行决策

    参数：
        sensors: 传感器数据字典 {light, human, temperature, humidity}
        context: 上下文字典 {query_type, history, current_time, event}
        device_id: 设备标识

    返回：
        actions 字典，包含 relay/servo/buzzer/tts/oled_line1/oled_line2/preheat/voice_notify 等
    """
    global _ai_online, _last_ai_error

    headers = {
        "Authorization": f"Bearer {ARK_API_KEY}",
        "Content-Type": "application/json"
    }

    # 预测模式 Prompt（回寝预处理）
    if context and context.get("query_type") == "predict":
        history = context.get("history", [])
        current_time = context.get("current_time", "20:00")
        custom_time = get_custom_return_time(device_id)

        prompt = f"""你是宿舍AI管家DormMate的预测模块。
根据以下信息判断是否需要提前开启环境预处理（空调+灯光）：

【历史回寝时间（最近7天）】
{history}

【当前时间】{current_time}
【用户手动设定回寝时间】{custom_time if custom_time else '未设定'}

【决策规则】
1. 如果用户手动设定了回寝时间，当前时间距设定时间<=15分钟则触发预处理
2. 如果历史数据>=3天，计算平均回寝时间，当前时间距平均时间<=15分钟则触发预处理
3. 预处理只基于时间判断，不基于温度、光线等环境数据
4. 只返回JSON，不要其他文字

返回格式：
{{"preheat":0或1,"relay":0或1,"predict_return_time":"21:00","target_temp":24,"reason":"...","tts":"预计您...","oled_line1":"Predict:21:00","oled_line2":"Preheat ON"}}"""
    else:
        # 普通上报模式 Prompt（环境异常 AI 询问）
        light = sensors.get('light', 0)
        temp = sensors.get('temperature', 0)
        hum = sensors.get('humidity', 0)
        human = sensors.get('human', 0)

        prompt = f"""你是宿舍AI管家DormMate，根据环境数据做决策。
传感器数据：
- 光线：{light} (0-4095，越大越暗，>3500算暗，<2000算亮)
- 人体：{'有人' if human else '无人'}
- 温度：{temp}℃
- 湿度：{hum}%

【决策规则】
1. 光线>3500且有人 → 返回 voice_notify:1（让设备语音询问用户"是否开灯"），relay必须为0，禁止直接开灯
2. 温度>30℃且有人 → 返回 voice_notify:4（让设备语音询问用户"是否开空调"），relay必须为0
3. 温度<18℃且有人 → 返回 voice_notify:4（询问是否开空调），relay必须为0
4. 环境舒适且有人 → 不操作（relay:0, voice_notify:0）
5. 必须说中文，语气亲切像室友
6. oled_line1和oled_line2只能用英文字母、数字、空格、点和冒号，不要中文
7. 你是"询问者"不是"执行者"，只发语音询问，绝不直接控制硬件
8. 【强制】voice_notify=1时，tts固定为："当前光线太暗，是否要为您开灯"
9. 【强制】voice_notify=4时，tts固定为："当前温度过高，是否要为您开空调"

只返回JSON，不要任何其他文字：
{{"relay":0,"servo":-1,"buzzer":0,"tts":"...","oled_line1":"第一行","oled_line2":"第二行","voice_notify":0}}"""

    payload = {
        "model": MODEL_NAME,
        "input": [{
            "role": "user",
            "content": [{"type": "input_text", "text": prompt}]
        }]
    }

    try:
        proxies = {"http": None, "https": None}
        resp = requests.post(ARK_API_URL, headers=headers, json=payload, timeout=15, proxies=proxies)
        result = resp.json()

        # 提取 AI 回复文本
        ai_text = ""
        if 'output' in result and isinstance(result['output'], list):
            for item in result['output']:
                if item.get('type') == 'message' and item.get('role') == 'assistant':
                    for c in item.get('content', []):
                        if c.get('type') == 'output_text' and 'text' in c:
                            ai_text = c['text']
                            break
                if ai_text:
                    break

        if not ai_text:
            raise Exception("API 返回中未找到 assistant 的 output_text")

        # 提取 JSON
        match = re.search(r'\{.*\}', ai_text, re.DOTALL)
        if match:
            actions = json.loads(match.group())
            actions.setdefault("relay", 0)
            actions.setdefault("servo", -1)
            actions.setdefault("buzzer", 0)
            actions.setdefault("tts", "已执行")
            actions.setdefault("oled_line1", "")
            actions.setdefault("oled_line2", "")
            actions.setdefault("preheat", 0)
            actions.setdefault("predict_return_time", "")
            actions.setdefault("target_temp", 24)
            actions.setdefault("reason", "")
            actions.setdefault("voice_notify", 0)
            # 强制覆盖话术
            if actions.get("voice_notify") == 1:
                actions["tts"] = "当前光线太暗，是否要为您开灯"
            elif actions.get("voice_notify") == 4:
                actions["tts"] = "当前温度过高，是否要为您开空调"
            _ai_online = True
            _last_ai_error = ""
            return actions
        else:
            raise Exception("返回内容中未找到合法 JSON")

    except Exception as e:
        _ai_online = False
        _last_ai_error = str(e)
        print(f"[AI调用失败] {e}")

    # ===== 本地 Fallback =====
    if context and context.get("query_type") == "predict":
        custom_time = get_custom_return_time(device_id)
        now = datetime.now().strftime("%H:%M")
        if custom_time:
            try:
                h1, m1 = map(int, now.split(':'))
                h2, m2 = map(int, custom_time.split(':'))
                diff = (h2 - h1) * 60 + (m2 - m1)
                if 0 <= diff <= 15:
                    return {
                        "preheat": 1, "relay": 1, "predict_return_time": custom_time, "target_temp": 24,
                        "reason": f"本地fallback：用户设定回寝时间{custom_time}，当前{now}，提前开启预处理",
                        "tts": "已开启环境预处理",
                        "oled_line1": "Preheat ON", "oled_line2": "",
                        "voice_notify": 10
                    }
            except Exception:
                pass
        return {
            "preheat": 0, "relay": 0, "predict_return_time": "", "target_temp": 24,
            "reason": "AI调用失败且不满足本地时间条件，跳过预处理",
            "tts": "", "oled_line1": "Local Mode", "oled_line2": "No Preheat",
            "voice_notify": 0
        }
    else:
        light = sensors.get('light', 500)
        return {
            "relay": 0, "servo": -1, "buzzer": 0,
            "tts": "", "oled_line1": "", "oled_line2": "",
            "voice_notify": 1 if light > 3500 else 0
        }


# ========== API 路由 ==========

@app.route('/api/report', methods=['POST'])
def report():
    """ESP32 上报传感器数据 & 获取 AI 决策

    请求 JSON：
        {sensors: {light, human, temperature, humidity, relay},
         context: {query_type, event, history, current_time, ...},
         device_id: "dormmate_01"}

    返回 JSON：
        {actions: {relay, servo, buzzer, tts, oled_line1, oled_line2, preheat, voice_notify, reason}}

    核心优化：预测模式下直接返回 voice_notify=10（预处理通知），不等下一轮
    """
    data = request.get_json()
    if not data:
        return jsonify({"error": "no data"}), 400

    sensors = data.get('sensors', {})
    latest_sensors.update(sensors)
    context = data.get('context', {})
    device_id = data.get('device_id', 'dormmate_01')

    # ---- 事件：首次检测到人（human_first） ----
    if context.get("event") == "human_first":
        date = context.get("date", datetime.now().strftime("%Y-%m-%d"))
        time_str = context.get("first_seen_time", "")
        weekday = context.get("weekday", datetime.now().weekday())
        temp = sensors.get("temperature", 0)
        hum = sensors.get("humidity", 0)

        conn = get_db()
        c = conn.cursor()
        c.execute("SELECT id FROM return_history WHERE device_id=? AND date=?", (device_id, date))
        if not c.fetchone():
            c.execute('''
                INSERT INTO return_history (device_id, date, first_seen_time, weekday, temp_at_arrival, hum_at_arrival)
                VALUES (?, ?, ?, ?, ?, ?)
            ''', (device_id, date, time_str, weekday, temp, hum))
            conn.commit()
        conn.close()

        return jsonify({"actions": {
            "relay": 0, "servo": -1, "buzzer": 0,
            "tts": "", "oled_line1": "", "oled_line2": "",
            "preheat": 0, "voice_notify": 9  # 欢迎回家
        }})

    # ---- 调用 AI 决策 ----
    actions = ask_ai(sensors, context, device_id)

    # ---- 安全限制：普通上报不更改硬件状态 ----
    event_type = context.get("event", "")
    query_type = context.get("query_type", "")
    if event_type != "human_return" and query_type != "predict":
        actions["relay"] = latest_sensors.get("relay", 0)
        actions["servo"] = -1
        actions["buzzer"] = 0
        if actions.get("voice_notify", 0) == 0:
            actions["tts"] = ""
            actions["oled_line1"] = ""
            actions["oled_line2"] = ""

    # ---- 手机端强制预处理 ----
    global pending_preheat
    if pending_preheat["trigger"]:
        actions["preheat"] = 1
        actions["relay"] = 1
        actions["reason"] = pending_preheat["reason"]
        actions["voice_notify"] = 10  # 预处理通知
        pending_preheat = {"trigger": False, "reason": ""}

    # ---- pending_notify 处理（偏好设置变更通知） ----
    conn = get_db()
    c = conn.cursor()
    c.execute("SELECT pending_notify FROM preferences WHERE device_id=?", (device_id,))
    pref_row = c.fetchone()
    if pref_row and pref_row["pending_notify"] == 1:
        actions["voice_notify"] = 11  # 设置已修改
        c.execute("UPDATE preferences SET pending_notify=0 WHERE device_id=?", (device_id,))
        conn.commit()
    conn.close()

    # ---- 预测模式下直接返回 voice_notify=10（预处理通知）----
    if query_type == "predict" and actions.get("preheat") == 1:
        if actions.get("voice_notify") != 11:
            actions["voice_notify"] = 10  # 预处理通知，立即播报

    # ---- 记录预测 ----
    if actions.get("preheat") == 1:
        today = datetime.now().strftime("%Y-%m-%d")
        conn = get_db()
        c = conn.cursor()
        c.execute("SELECT id FROM predictions WHERE device_id=? AND predict_date=? AND preheat_triggered=1 LIMIT 1",
                  (device_id, today))
        if not c.fetchone():
            c.execute('''
                INSERT INTO predictions (device_id, predict_date, predict_time, preheat_triggered, reason)
                VALUES (?, ?, ?, 1, ?)
            ''', (device_id, today, actions.get("predict_return_time", ""), actions.get("reason", "")))
            conn.commit()
        conn.close()

    # ---- 手机端远程控制（pending 机制） ----
    if pending_control["relay"] is not None:
        actions["relay"] = pending_control["relay"]
        pending_control["relay"] = None
    if pending_control["servo"] is not None:
        actions["servo"] = pending_control["servo"]
        pending_control["servo"] = None
    if pending_control["buzzer"] is not None:
        actions["buzzer"] = pending_control["buzzer"]
        pending_control["buzzer"] = None

    return jsonify({"actions": actions})


@app.route('/api/run_once', methods=['POST'])
def run_once():
    """单次执行模式：采集传感器数据 → AI决策 → 返回完整结果

    ESP32 调用此端点执行一次完整流程后进入深度休眠，
    不再持续轮询，仅在需要时唤醒。Token 仅在需要时消耗。

    请求 JSON：
        {sensors: {light, human, temperature, humidity, relay},
         context: {query_type, event, ...},
         device_id: "dormmate_01"}
    """
    data = request.get_json()
    if not data:
        return jsonify({"error": "no data"}), 400

    sensors = data.get('sensors', {})
    latest_sensors.update(sensors)
    context = data.get('context', {})
    device_id = data.get('device_id', 'dormmate_01')

    # 调用 AI 决策
    actions = ask_ai(sensors, context, device_id)

    # 安全限制
    event_type = context.get("event", "")
    query_type = context.get("query_type", "")
    if event_type != "human_return" and query_type != "predict":
        actions["relay"] = latest_sensors.get("relay", 0)
        actions["servo"] = -1
        actions["buzzer"] = 0

    # 单次执行完成后自动进入休眠
    actions["sleep_after"] = True  # ESP32 收到此标记后进入深度休眠

    return jsonify({
        "actions": actions,
        "mode": "run_once",
        "device_id": device_id
    })


@app.route('/api/status', methods=['GET'])
def status():
    """健康检查：返回后端运行状态、数据库连接、AI 连接状态"""
    db_ok = check_db_connection()
    return jsonify({
        "msg": "DormMate AI后端运行中",
        "mode": "doubao",
        "db_connected": db_ok,
        "ai_online": _ai_online,
        "ai_last_error": _last_ai_error if not _ai_online else "",
        "model": MODEL_NAME,
        "device_id": "dormmate_01"
    })


@app.route('/api/predict/status', methods=['GET'])
def predict_status():
    """前端获取预测状态：今日预测时间、是否已触发、偏好设置、历史回寝记录"""
    device_id = request.args.get('device_id', 'dormmate_01')
    today = datetime.now().strftime("%Y-%m-%d")

    conn = get_db()
    c = conn.cursor()

    c.execute("SELECT * FROM predictions WHERE device_id=? AND predict_date=? ORDER BY id DESC LIMIT 1",
              (device_id, today))
    pred = c.fetchone()

    c.execute("SELECT * FROM preferences WHERE device_id=?", (device_id,))
    pref = c.fetchone()

    c.execute('''
        SELECT date, first_seen_time, temp_at_arrival, hum_at_arrival
        FROM return_history
        WHERE device_id=? ORDER BY date DESC LIMIT 7
    ''', (device_id,))
    history = [{
        "date": row["date"],
        "time": row["first_seen_time"],
        "temp": row["temp_at_arrival"],
        "hum": row["hum_at_arrival"]
    } for row in c.fetchall()]

    conn.close()

    return jsonify({
        "device_id": device_id,
        "today": today,
        "predict_time": pred["predict_time"] if pred else None,
        "preheat_triggered": bool(pred["preheat_triggered"]) if pred else False,
        "preference": dict(pref) if pref else {"pref_temp": 24, "preheat_enable": 1, "custom_return_time": None},
        "history": history
    })


@app.route('/api/preference/return', methods=['POST'])
def set_preference():
    """手机端保存偏好设置

    优化：保存后直接返回 voice_notify=11 + TTS 文本，
    不再依赖 pending_notify 标记等待下次上报"""
    data = request.get_json()
    device_id = data.get('device_id', 'dormmate_01')
    pref_temp = data.get('pref_temp', 24)
    preheat_enable = data.get('preheat_enable', 1)
    custom_time = data.get('custom_return_time', None)

    conn = get_db()
    c = conn.cursor()
    c.execute('''
        INSERT INTO preferences (device_id, pref_temp, preheat_enable, custom_return_time, pending_notify)
        VALUES (?, ?, ?, ?, 1)
        ON CONFLICT(device_id) DO UPDATE SET
            pref_temp=excluded.pref_temp,
            preheat_enable=excluded.preheat_enable,
            custom_return_time=excluded.custom_return_time,
            pending_notify=1
    ''', (device_id, pref_temp, preheat_enable, custom_time))
    conn.commit()
    conn.close()

    return jsonify({
        "status": "ok",
        "device_id": device_id,
        "actions": {
            "voice_notify": 11,
            "tts": "已为您修改设置"
        }
    })


@app.route('/api/preheat/trigger', methods=['POST'])
def trigger_preheat():
    """手机端立即触发预处理（强制）"""
    data = request.get_json()
    device_id = data.get('device_id', 'dormmate_01')

    global pending_preheat
    pending_preheat = {"trigger": True, "reason": "手机端手动触发预处理"}

    return jsonify({
        "status": "ok",
        "device_id": device_id,
        "msg": "已触发，设备下次上报时执行预处理"
    })


@app.route('/api/history/return', methods=['GET'])
def get_history():
    """前端获取回寝历史记录"""
    device_id = request.args.get('device_id', 'dormmate_01')
    days = request.args.get('days', 30, type=int)

    conn = get_db()
    c = conn.cursor()
    c.execute('''
        SELECT date, first_seen_time, weekday, temp_at_arrival, hum_at_arrival
        FROM return_history
        WHERE device_id=? ORDER BY date DESC LIMIT ?
    ''', (device_id, days))
    rows = [dict(row) for row in c.fetchall()]
    conn.close()

    return jsonify({"device_id": device_id, "history": rows})


@app.route('/api/current', methods=['GET'])
def current():
    """前端获取最新传感器数据"""
    return jsonify({
        "device_id": "dormmate_01",
        "sensors": latest_sensors
    })


@app.route('/api/control', methods=['POST'])
def control():
    """手机端直接下发控制指令（ESP32 下次上报时带走执行）"""
    data = request.get_json()
    if not data:
        return jsonify({"error": "no data"}), 400

    device_id = data.get('device_id', 'dormmate_01')

    if "relay" in data:
        pending_control["relay"] = 1 if data["relay"] else 0
    if "servo" in data:
        pending_control["servo"] = data["servo"]
    if "buzzer" in data:
        pending_control["buzzer"] = 1 if data["buzzer"] else 0

    return jsonify({
        "status": "ok",
        "device_id": device_id,
        "pending": {k: v for k, v in pending_control.items() if v is not None}
    })


@app.route('/api/time', methods=['GET'])
def get_server_time():
    """前端获取服务器时间（用于初始化实时时钟）"""
    now = datetime.now()
    return jsonify({
        "year": now.year, "month": now.month, "day": now.day,
        "hour": now.hour, "minute": now.minute, "second": now.second
    })


@app.route('/')
def index():
    """前端页面入口"""
    return send_from_directory('.', 'index.html')


# ========== 启动入口 ==========

if __name__ == '__main__':
    init_db()
    print("DormMate 后端启动，数据库初始化完成...")
    app.run(host='0.0.0.0', port=5000, debug=False, threaded=True)
