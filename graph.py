import serial
import matplotlib.pyplot as plt
from matplotlib.animation import FuncAnimation
import time

# ตั้งค่าพอร์ต Serial
port = 'COM7'  # เปลี่ยนเป็นพอร์ตของคุณ
baud_rate = 115200  # อัตรา Baud Rate
serial_data = serial.Serial(port, baud_rate)
time.sleep(2)  # รอให้พอร์ตพร้อมใช้งาน

# ตัวแปรเก็บข้อมูล
x_data = []
y_data = []

# ฟังก์ชันอัปเดตข้อมูลในกราฟ
def update(frame):
    if serial_data.in_waiting > 0:
        try:
            data = serial_data.readline().decode('utf-8').strip()  # อ่านและแปลงข้อมูล
            y_value = float(data)  # สมมติว่า Arduino ส่งค่าเป็นตัวเลข
            x_data.append(len(x_data))  # เพิ่มลำดับเวลา
            y_data.append(y_value)  # เพิ่มค่าที่อ่านได้
            
            # จำกัดจำนวนจุดข้อมูลในกราฟ
            if len(x_data) > 100:
                x_data.pop(0)
                y_data.pop(0)
            
            # เคลียร์และวาดกราฟใหม่
            plt.cla()
            plt.plot(x_data, y_data, label="Sensor Data")
            plt.legend(loc='upper left')
            plt.xlabel("Time (samples)")
            plt.ylabel("Value")
            plt.title("Real-Time Serial Data")
        except ValueError:
            print("Error: Received non-numeric data.")

# ตั้งค่า Animation
ani = FuncAnimation(plt.gcf(), update)

# แสดงกราฟ
plt.tight_layout()
plt.show()

# ปิด Serial เมื่อปิดโปรแกรม
serial_data.close()