#oled_line1 和 oled_line2 只能用英文字母、数字、空格、点和冒号，不要中文。
from flask import Flask, request, jsonify
import requests
import json
import re
import os

app = Flask(__name__)

# ========== 豆包配置 ==========
# 不要在代码里写死密钥！PowerShell 先执行：$env:ARK_API_KEY="你的密钥"
ARK_API_KEY = os.environ.get("ARK_API_KEY", "ark-f580c4ab-414e-4372-b387-945f7cd6adcd-54624")
ARK_API_URL = "https://ark.cn-beijing.volces.com/api/v3/chat/completions"
MODEL_NAME = "doubao-seed-2-0-lite-260428"

def ask_ai(sensors):
    """调用豆包决策，失败自动 fallback 本地阈值"""
    headers = {
        "Authorization": f"Bearer {ARK_API_KEY}",
        "Content-Type": "application/json"
    }
    
    prompt = f"""你是宿舍AI管家DormMate，根据环境数据做决策。
传感器数据：
- 光线：{sensors.get('light', 0)} (0-4095，越小越暗)
- 人体：{'有人' if sensors.get('human', 0) else '无人'}
- 温度：{sensors.get('temperature', 0)}℃
- 湿度：{sensors.get('humidity', 0)}%
- 电流：{sensors.get('current', 0)}A

规则：
1. 光线<100且有人 → 开灯
2. 无人 → 关灯省电
3. 必须说中文，语气亲切
4. oled_line1 和 oled_line2 只能用英文字母、数字、空格、点和冒号，不要中文
只返回JSON，不要任何其他文字：
{{"relay":0或1,"servo":-1,"buzzer":0,"tts":"中文语音","oled_line1":"第一行","oled_line2":"第二行"}}"""

    payload = {
        "model": MODEL_NAME,
        "messages": [{"role": "user", "content": prompt}],
        "temperature": 0.2
    }
    
    try:
        # 禁用系统代理，直连火山引擎
        proxies = {
            "http": None,
            "https": None,
        }

        resp = requests.post(ARK_API_URL, headers=headers, json=payload, timeout=30, proxies=proxies)
        result = resp.json()
        ai_text = result['choices'][0]['message']['content']
        print(f"[AI原始返回] {ai_text}")
        
        # 从 AI 文本里抠 JSON
        match = re.search(r'\{.*\}', ai_text, re.DOTALL)
        if match:
            actions = json.loads(match.group())
            # 补全缺省字段，防止板子解析崩
            actions.setdefault("relay", 0)
            actions.setdefault("servo", -1)
            actions.setdefault("buzzer", 0)
            actions.setdefault("tts", "已执行")
            actions.setdefault("oled_line1", "")
            actions.setdefault("oled_line2", "")
            return actions
    except Exception as e:
        print(f"[AI调用失败] {e}")
    
    # Fallback：本地阈值保底，系统绝不崩
    light = sensors.get('light', 500)
    return {
        "relay": 1 if light < 100 else 0,
        "servo": -1,
        "buzzer": 0,
        "tts": "本地模式：光线较暗，已开灯" if light < 100 else "本地模式：光线充足，已关灯",
        "oled_line1": "本地模式",
        "oled_line2": f"光线:{light}"
    }

@app.route('/api/report', methods=['POST'])
def report():
    data = request.get_json()
    sensors = data.get('sensors', {}) if data else {}
    
    actions = ask_ai(sensors)
    print(f"[AI决策] 光敏={sensors.get('light')} -> relay={actions['relay']}, tts={actions['tts']}")
    
    return jsonify({"actions": actions})

@app.route('/api/status', methods=['GET'])
def status():
    return jsonify({"msg": "AI后端运行中", "mode": "doubao"})

if __name__ == '__main__':
    print("后端启动，等待板子连接...")
    app.run(host='0.0.0.0', port=5000, debug=False)