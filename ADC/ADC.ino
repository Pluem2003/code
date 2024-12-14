#define ADC_PIN 0  // กำหนด ADC_PIN ให้เป็น GPIO0 (ADC0)

void setup() {
  Serial.begin(115200);  // เริ่มต้น Serial Communication
  pinMode(ADC_PIN, INPUT);  // กำหนด PIN ADC เป็น Input
  Serial.println("Starting ADC Example...");
}

void loop() {
  int rawValue = analogRead(ADC_PIN);  // อ่านค่าดิบจาก ADC
  float voltage = rawValue * (3.3 / 4095.0);  // คำนวณแรงดันไฟ (ESP32-C3 ใช้ 12-bit ADC)

  Serial.print("Raw Value: ");
  Serial.print(rawValue);
  Serial.print(" | Voltage: ");
  Serial.print(voltage);
  Serial.println(" V");

  delay(1000);  // อ่านค่า ADC ทุก 1 วินาที
}
