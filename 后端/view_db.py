import sqlite3
import os

BASE_DIR = os.path.dirname(os.path.abspath(__file__))
DB_PATH = os.path.join(BASE_DIR, "data", "dormmate.db")

def show_table(name, query):
    conn = sqlite3.connect(DB_PATH)
    conn.row_factory = sqlite3.Row
    c = conn.cursor()
    c.execute(query)
    rows = c.fetchall()
    conn.close()
    
    print(f"\n{'='*60}")
    print(f"📋 表: {name}  (共 {len(rows)} 条记录)")
    print('='*60)
    
    if not rows:
        print("  (空)")
        return
    
    # 打印表头
    keys = rows[0].keys()
    header = " | ".join(f"{k:<18}" for k in keys)
    print(header)
    print("-" * len(header))
    
    # 打印数据
    for row in rows:
        line = " | ".join(f"{str(row[k]):<18}" for k in keys)
        print(line)

# 1. 回寝历史
show_table("return_history", "SELECT * FROM return_history ORDER BY date DESC")

# 2. 偏好设置
show_table("preferences", "SELECT * FROM preferences")

# 3. 预测记录
show_table("predictions", "SELECT * FROM predictions ORDER BY id DESC")

# 4. 实时传感器缓存（这个在内存里，不在数据库里，但可以通过后端API看）
print("\n" + "="*60)
print("💡 提示: 最新传感器数据在内存中，访问 http://10.147.81.32:5000/api/current 查看")
print("="*60)