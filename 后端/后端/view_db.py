"""
DormMate 数据库查看工具
用于查看 dormmate.db 中的回寝历史、偏好设置、预测记录
用法：python view_db.py
"""
import sqlite3
from datetime import datetime

DB_PATH = "dormmate.db"

def view_all_tables():
    conn = sqlite3.connect(DB_PATH)
    conn.row_factory = sqlite3.Row
    c = conn.cursor()

    print("=" * 60)
    print(f"DormMate 数据库查看 - {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}")
    print("=" * 60)

    # 回寝历史
    print("\n📋 回寝历史 (return_history)：")
    c.execute("SELECT * FROM return_history ORDER BY date DESC LIMIT 10")
    for row in c.fetchall():
        print(f"  {row['date']} | 首次回寝: {row['first_seen_time']} | "
              f"温度: {row['temp_at_arrival']}℃ | 湿度: {row['hum_at_arrival']}%")

    # 偏好设置
    print("\n⚙️ 偏好设置 (preferences)：")
    c.execute("SELECT * FROM preferences")
    for row in c.fetchall():
        print(f"  设备: {row['device_id']} | 目标温度: {row['pref_temp']}℃ | "
              f"预处理开关: {row['preheat_enable']} | 回寝时间: {row['custom_return_time']}")

    # 预测记录
    print("\n🔮 预测记录 (predictions)：")
    c.execute("SELECT * FROM predictions ORDER BY id DESC LIMIT 10")
    for row in c.fetchall():
        print(f"  {row['predict_date']} | 预测时间: {row['predict_time']} | "
              f"已触发: {'是' if row['preheat_triggered'] else '否'} | 原因: {row['reason']}")

    conn.close()

if __name__ == "__main__":
    view_all_tables()
