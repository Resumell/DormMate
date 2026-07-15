import sqlite3
from datetime import datetime, timedelta
import os

BASE_DIR = os.path.dirname(os.path.abspath(__file__))
DB_PATH = os.path.join(BASE_DIR, "data", "dormmate.db")

conn = sqlite3.connect(DB_PATH)
c = conn.cursor()

# 插入 3 天假数据（连续三天，晚上 21:30 回寝）
base_date = datetime(2026, 7, 7)
fake_data = [
    ("21:30", 26.5, 60.0),
    ("21:30", 27.0, 58.0),
    ("21:30", 25.8, 62.0),
]

for i, (time_str, temp, hum) in enumerate(fake_data):
    date = (base_date + timedelta(days=i)).strftime("%Y-%m-%d")
    c.execute('''
        INSERT OR IGNORE INTO return_history 
        (device_id, date, first_seen_time, weekday, temp_at_arrival, hum_at_arrival)
        VALUES (?, ?, ?, ?, ?, ?)
    ''', ("dormmate_01", date, time_str, (base_date + timedelta(days=i)).weekday(), temp, hum))
    print(f"插入: {date} {time_str}")

conn.commit()
conn.close()
print("假数据插入完成")