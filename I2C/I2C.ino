#include <Wire.h>
#include <math.h>

#define SDA_PIN 8   // SDA ของ ESP32-C3 Supermini
#define SCL_PIN 9   // SCL ของ ESP32-C3 Supermini
#define MCP3221_ADDRESS 0x4D // I2C Address ของ MCP3221
#define clockFrequency 400000
#define offset 126

unsigned long long sum_all = 0;
unsigned long long check_overflow = 0;
uint16_t count_Value = 0;
double current = 0;
double voltage = 0;

void setup() {
  Wire.begin();
  Wire.setClock(clockFrequency);
  Serial.begin(115200);
  Serial.println("MCP3221 I2C Reader Initialized");
}

void loop() {
  check_overflow = sum_all;
  uint16_t adcValue = readMCP3221(); // อ่านค่า ADC
  // Serial.println(adcValue);
  sum_all += (adcValue - offset) * (adcValue - offset);
  count_Value++;
  if(sum_all < check_overflow) {
    sum_all = 0;
    count_Value = 0;
    sum_all += (adcValue - offset) * (adcValue - offset);
    count_Value++;
  }
  if(count_Value == 2000){
    current = sqrt(sum_all / count_Value);
    voltage = (current * 3.3) / 4096.0; // คำนวณแรงดันไฟฟ้า (VREF = 3.3V)
    sum_all = 0;
    count_Value = 0;
  }
  // Serial.print("ADC Value: ");
  Serial.println(current);

  // Serial.print(" | Voltage: ");
  // Serial.print(voltage, 3);
  // Serial.println(" V");

}

uint16_t readMCP3221() {
  unsigned int rawData = 0;
  Wire.beginTransmission(MCP3221_ADDRESS);
  Wire.requestFrom(MCP3221_ADDRESS, 2);
  if (Wire.available() == 2)
  {
    rawData = (Wire.read() << 8) | (Wire.read());
  }
  else
  {
    Wire.beginTransmission(MCP3221_ADDRESS);
    Wire.endTransmission();
  }
  return rawData;
}