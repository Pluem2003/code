import asyncio
from bleak import BleakClient, BleakScanner
import keyboard  # ไลบรารีสำหรับตรวจจับการกดปุ่ม

# UUID ของ Service และ Characteristic ที่ต้องใช้
SERVICE_UUID = "12345678-1234-1234-1234-123456789012"
CHARACTERISTIC_UUID = "87654321-4321-4321-4321-210987654321"

# ฟังก์ชัน callback เมื่อได้รับข้อมูลจาก GATT Server
def notification_handler(sender, data):
    csv_data = data.decode("utf-8")  # แปลงข้อมูลจาก bytes เป็น string
    print(f"Received CSV data: {csv_data}")

    # บันทึกข้อมูลลงไฟล์ CSV
    with open("received_data.csv", "a") as file:
        file.write(csv_data + "\n")

async def main():
    # สแกนหาอุปกรณ์ BLE
    devices = await BleakScanner.discover()
    esp32_address = None

    # หา ESP32-C3 จาก UUID ของ Service
    for device in devices:
        print(f"Found device: {device.name}, Address: {device.address}")  # แสดงชื่อและที่อยู่ของอุปกรณ์
        if "ESP32-C3 Sensor Server" in device.name:  # ใช้ชื่ออุปกรณ์
            esp32_address = device.address
            break

    if esp32_address is None:
        print("ESP32-C3 not found!")
        return

    # เชื่อมต่อกับ ESP32-C3
    async with BleakClient(esp32_address) as client:
        print(f"Connected to {esp32_address}")

        # ตรวจสอบว่า Characteristic มีอยู่หรือไม่
        services = await client.get_services()
        print(f"Services: {services}")

        # สมัครรับการแจ้งเตือนจาก Characteristic
        await client.start_notify(CHARACTERISTIC_UUID, notification_handler)

        print("Press 'q' to stop receiving data...")
        
        # รอจนกว่าจะกดปุ่ม 'q'
        while True:
            if keyboard.is_pressed('q'):  # ตรวจสอบว่ามีการกดปุ่ม 'q'
                print("Stopping data reception...")
                break
            await asyncio.sleep(0.1)  # หยุดชั่วคราวเพื่อไม่ให้ใช้ CPU มากเกินไป

        # ยกเลิกการแจ้งเตือนเมื่อเสร็จ
        await client.stop_notify(CHARACTERISTIC_UUID)

# รันโปรแกรมหลัก
asyncio.run(main())
