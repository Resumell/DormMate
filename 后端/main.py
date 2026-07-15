"""
DormMate 智能宿舍管家 — Flask 后端主程序
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

latest_sensors = {
    "light": 0, "human": 0, "temperature": 26.5,
    "humidity": 60, "current": 0, "relay": 0, "ac_relay": 0, "servo": -1
}

pending_control = {"relay": None, "servo": None, "buzzer": None}
pending_preheat = {"trigger": False, "reason": ""}

BASE_DIR = os.path.dirname(os.path.abspath(__file__))
DATA_DIR = os.path.join(BASE_DIR, "data")
DB_PATH = os.path.join(DATA_DIR, "dormmate.db")
os.makedirs(DATA_DIR, exist_ok=True)

ARK_API_KEY = os.environ.get("ARK_API_KEY")
if not ARK_API_KEY:
    raise ValueError("ARK_API_KEY 未设置，请复制 .env.example 为 .env 并填入新 Key")

ARK_API_URL = "https://ark.cn-beijing.volces.com/api/v3/chat/completions"
MODEL_NAME = os.environ.get("MODEL_NAME", "doubao-seed-2-0-code-preview-260215")

_ai_online = True
_last_ai_error = ""


def init_db():
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

    try:
        c.execute("ALTER TABLE predictions ADD COLUMN preheat_done_today INTEGER DEFAULT 0")
    except sqlite3.OperationalError:
        pass

    conn.commit()
    conn.close()


def get_db():
    conn = sqlite3.connect(DB_PATH)
    conn.row_factory = sqlite3.Row
    return conn


def get_custom_return_time(device_id="dormmate_01"):
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


def get_preheat_enable(device_id="dormmate_01"):
    try:
        conn = sqlite3.connect(DB_PATH)
        conn.row_factory = sqlite3.Row
        c = conn.cursor()
        c.execute("SELECT preheat_enable FROM preferences WHERE device_id=?", (device_id,))
        row = c.fetchone()
        conn.close()
        return int(row["preheat_enable"]) if row else 1
    except Exception:
        return 1


def calc_avg_return_time(history):
    if not history:
        return None
    evening_mins = []
    for h in history:
        t = h if isinstance(h, str) else h.get("time", "")
        if t and isinstance(t, str):
            try:
                hour, minute = map(int, t.split(':'))
                if hour >= 18:
                    evening_mins.append(hour * 60 + minute)
            except Exception:
                continue
    if len(evening_mins) >= 3:
        avg = sum(evening_mins) // len(evening_mins)
        return f"{avg // 60:02d}:{avg % 60:02d}"
    return None


def check_db_connection():
    try:
        conn = sqlite3.connect(DB_PATH)
        conn.close()
        return True
    except Exception:
        return False


def ask_ai(sensors, context=None, device_id="dormmate_01"):
    global _ai_online, _last_ai_error
    print(f"[DEBUG] URL={ARK_API_URL}, MODEL={MODEL_NAME}")

    headers = {
        "Authorization": f"Bearer {ARK_API_KEY}",
        "Content-Type": "application/json"
    }

    if context and context.get("query_type") == "predict":
        preheat_enable = get_preheat_enable(device_id)
        if preheat_enable == 0:
            return {
                "preheat": 0, "relay": -1, "ac_relay": -1,
                "predict_return_time": "", "target_temp": 24,
                "reason": "用户已关闭自动预处理",
                "tts": "", "oled_line1": "Auto Preheat OFF", "oled_line2": "",
                "voice_notify": 0
            }

        history = context.get("history", [])
        current_time = context.get("current_time", "20:00")
        custom_time = get_custom_return_time(device_id)
        avg_time = calc_avg_return_time(history)

        # ===== 精简 prompt，省 token =====
        prompt = f"""预测预处理(空调)。
历史(18:00后):{history}
平均:{avg_time if avg_time else '不足'}
自定义:{custom_time if custom_time else '无'}
当前:{current_time}

规则:
1. 自定义时间距现在<=15分钟→preheat:1
2. 历史>=3天且平均时间距现在<=15分钟→preheat:1
3. 只开空调(relay:-1)
4. predict_return_time基于历史数据

JSON:{{"preheat":0/1,"relay":-1,"ac_relay":0/1,"predict_return_time":"HH:MM","target_temp":24,"reason":"...","tts":"...","oled_line1":"...","oled_line2":"...","voice_notify":0/10}}"""
    else:
        light = sensors.get('light', 0)
        temp = sensors.get('temperature', 0)
        hum = sensors.get('humidity', 0)
        human = sensors.get('human', 0)

        # ===== 精简 prompt，省 token =====
        prompt = f"""环境决策。
光线:{light}(>3500暗,<2000亮) 人体:{'有人' if human else '无人'} 温度:{temp}℃ 湿度:{hum}%

规则:
1. 光线>3500且有人→voice_notify:1,relay:0,tts:"当前光线太暗，是否要为您开灯"
2. 温度>30且有人→voice_notify:4,relay:0,tts:"当前温度过高，是否要为您开空调"
3. 温度<18且有人→voice_notify:4,relay:0
4. 舒适→relay:-1,ac_relay:-1,voice_notify:0
5. oled只用英文数字空格点冒号
6. 只问不执行

JSON:{{"relay":-1/0/1,"ac_relay":-1/0/1,"servo":-1,"buzzer":0,"tts":"...","oled_line1":"...","oled_line2":"...","voice_notify":0/1/4}}"""

    payload = {
        "model": MODEL_NAME,
        "messages": [{
            "role": "user",
            "content": prompt
        }]
    }

    try:
        proxies = {"http": None, "https": None}
        resp = requests.post(ARK_API_URL, headers=headers, json=payload, timeout=30, proxies=proxies)
        result = resp.json()

        ai_text = ""
        if 'choices' in result and isinstance(result['choices'], list):
            for choice in result['choices']:
                if 'message' in choice and 'content' in choice['message']:
                    ai_text = choice['message']['content']
                    break

        if not ai_text:
            raise Exception("API 返回中未找到 assistant 的 output_text")

        match = re.search(r'\{.*\}', ai_text, re.DOTALL)
        if match:
            actions = json.loads(match.group())
            actions.setdefault("relay", -1)
            actions.setdefault("ac_relay", -1)
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
        preheat_enable = get_preheat_enable(device_id)
        if preheat_enable == 0:
            return {
                "preheat": 0, "relay": -1, "ac_relay": -1,
                "predict_return_time": "", "target_temp": 24,
                "reason": "用户已关闭自动预处理",
                "tts": "", "oled_line1": "Auto Preheat OFF", "oled_line2": "",
                "voice_notify": 0
            }

        custom_time = get_custom_return_time(device_id)
        avg_time = calc_avg_return_time(context.get("history", []))
        now = datetime.now().strftime("%H:%M")
        
        custom_trigger = False
        if custom_time:
            try:
                h1, m1 = map(int, now.split(':'))
                h2, m2 = map(int, custom_time.split(':'))
                diff = (h2 - h1) * 60 + (m2 - m1)
                if 0 <= diff <= 15:
                    custom_trigger = True
            except Exception:
                pass
        
        history_trigger = False
        if avg_time:
            try:
                h1, m1 = map(int, now.split(':'))
                h2, m2 = map(int, avg_time.split(':'))
                diff = (h2 - h1) * 60 + (m2 - m1)
                if 0 <= diff <= 15:
                    history_trigger = True
            except Exception:
                pass
        
        if custom_trigger or history_trigger:
            reason_parts = []
            if custom_trigger:
                reason_parts.append(f"用户设定时间{custom_time}")
            if history_trigger:
                reason_parts.append(f"红外历史平均时间{avg_time}")
            
            if custom_trigger:
                predict_time = custom_time
            elif history_trigger:
                predict_time = avg_time or ""
            else:
                predict_time = ""
            
            return {
                "preheat": 1, "relay": -1, "ac_relay": 1,
                "predict_return_time": predict_time,
                "target_temp": 24,
                "reason": f"本地fallback：{' & '.join(reason_parts)}，当前{now}，提前开启预处理",
                "tts": "已开启环境预处理",
                "oled_line1": "Preheat ON", "oled_line2": "",
                "voice_notify": 10
            }
        return {
            "preheat": 0, "relay": -1, "ac_relay": -1,
            "predict_return_time": avg_time or "",
            "target_temp": 24,
            "reason": "AI调用失败且不满足本地时间条件，跳过预处理",
            "tts": "", "oled_line1": "Local Mode", "oled_line2": "No Preheat",
            "voice_notify": 0
        }
    else:
        light = sensors.get('light', 500)
        return {
            "relay": -1, "ac_relay": -1,
            "servo": -1, "buzzer": 0,
            "tts": "", "oled_line1": "", "oled_line2": "",
            "voice_notify": 1 if light > 3500 else 0
        }


@app.route('/api/report', methods=['POST'])
def report():
    data = request.get_json()
    if not data:
        return jsonify({"error": "no data"}), 400

    sensors = data.get('sensors', {})
    latest_sensors.update(sensors)
    
    status = data.get('status', {})
    if isinstance(status, dict):
        latest_sensors.update(status)

    context = data.get('context', {})
    device_id = data.get('device_id', 'dormmate_01')

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
            "relay": -1, "ac_relay": -1,
            "servo": -1, "buzzer": 0,
            "tts": "", "oled_line1": "", "oled_line2": "",
            "preheat": 0, "voice_notify": 9
        }})

    actions = ask_ai(sensors, context, device_id)
    
    # ===== 强制兜底：AI 不守规则时后端直接校正（省 token，不依赖 AI 自觉）=====
    if context.get("query_type") != "predict" and context.get("event") != "human_return":
        light = sensors.get('light', 0)
        temp = sensors.get('temperature', 0)
        human = sensors.get('human', 0)
        if human:
            if light > 3500:
                actions["voice_notify"] = 1
                actions["tts"] = "当前光线太暗，是否要为您开灯"
                actions["relay"] = 0
            elif temp > 30:
                actions["voice_notify"] = 4
                actions["tts"] = "当前温度过高，是否要为您开空调"
                actions["relay"] = 0
            elif temp < 18:
                actions["voice_notify"] = 4
                actions["tts"] = "当前温度过低，是否要为您开空调"
                actions["relay"] = 0

    # ===== 修正 predict_return_time：如果预处理是基于自定义时间触发的，优先显示自定义时间 =====
    if actions.get("preheat") == 1:
        custom_time = get_custom_return_time(device_id)
        if custom_time:
            now_str = datetime.now().strftime("%H:%M")
            try:
                h1, m1 = map(int, now_str.split(':'))
                h2, m2 = map(int, custom_time.split(':'))
                diff = (h2 - h1) * 60 + (m2 - m1)
                if 0 <= diff <= 15:
                    actions["predict_return_time"] = custom_time
            except Exception:
                pass

    event_type = context.get("event", "")
    query_type = context.get("query_type", "")
    if event_type != "human_return" and query_type != "predict":
        actions["relay"] = -1
        actions["ac_relay"] = -1
        actions["servo"] = -1
        actions["buzzer"] = 0
        if actions.get("voice_notify", 0) == 0:
            actions["tts"] = ""
            actions["oled_line1"] = ""
            actions["oled_line2"] = ""

    global pending_preheat
    if pending_preheat["trigger"]:
        actions["preheat"] = 1
        actions["relay"] = -1
        actions["ac_relay"] = 1
        actions["reason"] = pending_preheat["reason"]
        actions["voice_notify"] = 10
        pending_preheat = {"trigger": False, "reason": ""}

    conn = get_db()
    c = conn.cursor()
    c.execute("SELECT pending_notify FROM preferences WHERE device_id=?", (device_id,))
    pref_row = c.fetchone()
    if pref_row and pref_row["pending_notify"] == 1:
        actions["voice_notify"] = 11
        c.execute("UPDATE preferences SET pending_notify=0 WHERE device_id=?", (device_id,))
        conn.commit()
    conn.close()

    if query_type == "predict" and actions.get("preheat") == 1:
        if actions.get("voice_notify") != 11:
            actions["voice_notify"] = 10

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
            print(f"[预处理] 今日首次触发，记录入库")
        else:
            c.execute('''
                UPDATE predictions SET predict_time=?, reason=? 
                WHERE device_id=? AND predict_date=? AND preheat_triggered=1
            ''', (actions.get("predict_return_time", ""), actions.get("reason", ""), device_id, today))
            conn.commit()
            print(f"[预处理] 更新今日预处理记录")
        conn.close()

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
    data = request.get_json()
    if not data:
        return jsonify({"error": "no data"}), 400

    sensors = data.get('sensors', {})
    latest_sensors.update(sensors)
    status = data.get('status', {})
    if isinstance(status, dict):
        latest_sensors.update(status)

    context = data.get('context', {})
    device_id = data.get('device_id', 'dormmate_01')

    actions = ask_ai(sensors, context, device_id)

    event_type = context.get("event", "")
    query_type = context.get("query_type", "")
    if event_type != "human_return" and query_type != "predict":
        actions["relay"] = -1
        actions["ac_relay"] = -1
        actions["servo"] = -1
        actions["buzzer"] = 0

    actions["sleep_after"] = True

    return jsonify({
        "actions": actions,
        "mode": "run_once",
        "device_id": device_id
    })


@app.route('/api/status', methods=['GET'])
def status():
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

    avg_time = calc_avg_return_time(history)

    c.execute('SELECT first_seen_time FROM return_history WHERE device_id=? AND date=? LIMIT 1',
              (device_id, today))
    return_row = c.fetchone()
    actual_return_time = return_row["first_seen_time"] if return_row else None

    conn.close()

    return jsonify({
        "device_id": device_id,
        "today": today,
        "predict_time": pred["predict_time"] if pred else None,
        "avg_return_time": avg_time,
        "preheat_triggered": bool(pred["preheat_triggered"]) if pred else False,
        "actual_return_time": actual_return_time,
        "preference": dict(pref) if pref else {"pref_temp": 24, "preheat_enable": 1, "custom_return_time": None},
        "history": history
    })


@app.route('/api/preference/return', methods=['POST'])
def set_preference():
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
    return jsonify({
        "device_id": "dormmate_01",
        "sensors": latest_sensors
    })


@app.route('/api/control', methods=['POST'])
def control():
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
    now = datetime.now()
    return jsonify({
        "year": now.year, "month": now.month, "day": now.day,
        "hour": now.hour, "minute": now.minute, "second": now.second,
        "epoch": int(now.timestamp())
    })


@app.route('/')
def index():
    return send_from_directory('.', 'index.html')


if __name__ == '__main__':
    init_db()
    print("DormMate 后端启动，数据库初始化完成...")
    app.run(host='0.0.0.0', port=5000, debug=False, threaded=True)