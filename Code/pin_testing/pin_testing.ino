/*
 * Analog Pin Scanner - Adafruit Feather nRF52840 Express
 * Reads analog values from all available pins to identify battery voltage divider
 * Battery divider has 2:1 ratio (actual battery voltage = pin voltage * 2)
 */

#include <bluefruit.h>

// All analog-capable pins on nRF52840 Feather
const int pinsToMonitor[] = {
  0, 1, 2, 3, 4, 5, 6, 7, 8, 9,
  10, 11, 12, 13, 14, 15, 16, 17, 18, 19,
  20, 21, 22, 23, 24, 25, 26, 27, 28, 29,
  30, 31
};
const int numPins = sizeof(pinsToMonitor) / sizeof(pinsToMonitor[0]);

void setup() {
  // Initialize Serial
  Serial.begin(115200);
  while (!Serial && millis() < 5000) delay(10);
  
  // Initialize Bluefruit (required for Serial on nRF52840)
  Bluefruit.begin();
  
  delay(1000);
  
  Serial.println("Analog Pin Scanner - Looking for 2:1 battery voltage divider");
  Serial.println("Battery voltage = pin voltage * 2");
  Serial.println();
  
  // Initialize pins as inputs
  for (int i = 0; i < numPins; i++) {
    pinMode(pinsToMonitor[i], INPUT);
  }
  
  delay(100);
}

void loop() {
  // Read and print each pin on its own line
  for (int i = 0; i < numPins; i++) {
    int pin = pinsToMonitor[i];
    
    // Read analog value
    // nRF52840 ADC is 10-bit (0-1023) with 3.6V reference
    int rawValue = analogRead(pin);
    
    // Convert to voltage at pin
    // nRF52840: 10-bit ADC, 3.6V reference
    // Voltage = (rawValue / 1023.0) * 3.6
    float pinVoltage = (rawValue / 1023.0) * 3.6;
    
    // Calculate battery voltage assuming 2:1 divider
    float batteryVoltage = pinVoltage * 2.0;
    
    // Print each pin on its own line
    Serial.print("pin ");
    Serial.print(pin);
    Serial.print(" : ");
    Serial.print(rawValue);
    Serial.print(", ");
    Serial.print(pinVoltage, 3);
    Serial.print(", ");
    Serial.print(batteryVoltage, 3);
    Serial.println();
  }
  
  Serial.println();  // Blank line between readings
  
  delay(1000);  // Wait 1 second between readings
}
