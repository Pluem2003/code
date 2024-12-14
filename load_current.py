import serial
import csv
import time

# กำหนดพอร์ตและบอดเรตของ Arduino
serial_port = "COM7"  # เปลี่ยนเป็นพอร์ตของ Arduino
baud_rate = 115200
output_file = "mcp3221_data.csv"

# เปิดการเชื่อมต่อ Serial
with serial.Serial(serial_port, baud_rate, timeout=1) as ser:
    with open(output_file, mode='w', newline='') as file:
        writer = csv.writer(file)
        writer.writerow(["Time (ms)", "ADC Value", "Voltage"])  # เขียนหัวข้อ
        
        print("เริ่มบันทึกข้อมูลลงไฟล์...")
        try:
            while True:
                line = ser.readline().decode('utf-8').strip()
                if line:
                    print(line)  # แสดงข้อมูลบนคอนโซล
                    data = line.split(",")  # แยกข้อมูลด้วยเครื่องหมาย ,
                    writer.writerow(data)  # บันทึกข้อมูลลงไฟล์
        except KeyboardInterrupt:
            print("\nหยุดการบันทึกข้อมูล")
