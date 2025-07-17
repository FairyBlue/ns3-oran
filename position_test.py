#!/usr/bin/env python3
"""
测试位置报告器是否正确工作
"""

import sqlite3
import os

def check_position_data():
    db_path = "oran-forwarding-repository.db"
    
    if not os.path.exists(db_path):
        print("❌ 数据库文件不存在")
        return
    
    conn = sqlite3.connect(db_path)
    
    print("🔍 === 位置数据分析 ===")
    
    # 检查位置数据
    cursor = conn.execute("""
        SELECT DISTINCT nodeid, x, y, z 
        FROM nodelocation 
        ORDER BY nodeid
    """)
    
    expected_positions = {
        1: (-250.0, -50.0, 100.0),  # DU-1
        2: (-250.0, 50.0, 100.0),   # DU-2  
        3: (-150.0, 50.0, 100.0),   # DU-3
        4: (-150.0, -50.0, 100.0),  # CU-1
        5: (150.0, -50.0, 100.0),   # DU-4
        6: (150.0, 50.0, 100.0),    # DU-5
        7: (250.0, 50.0, 100.0),    # DU-6
        8: (250.0, -50.0, 100.0),   # CU-2
    }
    
    print("节点ID | 实际位置              | 期望位置              | 匹配")
    print("-" * 70)
    
    all_correct = True
    for row in cursor.fetchall():
        nodeid, x, y, z = row
        actual = (x, y, z)
        
        if nodeid in expected_positions:
            expected = expected_positions[nodeid]
            match = "✅" if actual == expected else "❌"
            if actual != expected:
                all_correct = False
            print(f"{nodeid:6d} | {str(actual):20} | {str(expected):20} | {match}")
        else:
            print(f"{nodeid:6d} | {str(actual):20} | {'未知':20} | ❓")
    
    print()
    if all_correct:
        print("🎉 所有位置数据都是正确的！")
    else:
        print("⚠️ 位置数据不匹配 - 位置报告器可能没有正常工作")
    
    conn.close()

if __name__ == "__main__":
    check_position_data() 