/*
 * Ping Bracelet - BLE Buzzer Control for Seeed Studio XIAO ESP32-C6
 * 
 * Commands via BLE UART:
 *   buzz <ms> <power>  - Buzz for ms milliseconds at power (0.0-1.0)
 *   ramp <ms> <maxpower>  - Ramp up to maxpower over ms
 *   battery - Read battery voltage
 *   recalibrate - Recalibrate touch sensor
 */

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <driver/gpio.h>  // For gpio_pullup_dis() and gpio_pulldown_dis()

// Pin definitions - Seeed Studio XIAO ESP32-C6
#define BUZZER_PIN 6     // Pin D6 for buzzer (PWM capable)
#define TOUCH_PIN 0       // Pin D0 (A0) for touch sensor (analog input)
#define BATTERY_PIN A1    // Battery voltage monitoring (A1/D1) - moved from A0 to avoid conflict with touch sensor

// Constants
#define PWM_FREQ 1000           // PWM frequency in Hz
#define PWM_RESOLUTION 8        // 8-bit resolution (0-255)
int pwmChannel = -1;            // LEDC channel (set by ledcAttach)
#define TOUCH_THRESHOLD_PERCENT 0.8  // Touch threshold (20% increase = touch, value > baseline * 1.2)

// BLE UUIDs
#define SERVICE_UUID "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"  // UART Service
#define CHAR_UUID_RX "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"  // RX Characteristic
#define CHAR_UUID_TX "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"  // TX Characteristic
#define BATTERY_SERVICE_UUID "180F"
#define BATTERY_CHAR_UUID "2A19"

// BLE objects
BLEServer* pServer = nullptr;
BLECharacteristic* pTxCharacteristic = nullptr;
BLECharacteristic* pBatteryCharacteristic = nullptr;
bool deviceConnected = false;
bool oldDeviceConnected = false;

// Buzzer state
bool buzzerActive = false;
unsigned long buzzerStartTime = 0;
unsigned long buzzerDuration = 0;
float buzzerPower = 1.0;

// Ramp state
bool rampActive = false;
unsigned long rampStartTime = 0;
unsigned long rampDuration = 0;
float rampMaxPower = 0.0;

// Touch sensor state (using analog input since ESP32-C6 doesn't have capacitive touch)
uint16_t touchBaseline = 0;
bool isTouched = false;
unsigned long lastTouchRead = 0;
const unsigned long TOUCH_READ_INTERVAL = 200;

// Ping feature state
enum PingState {
  PING_IDLE,
  PING_TOUCH_HOLDING,
  PING_RAMPING,
  PING_WAITING_AFTER_RAMP,
  PING_BUZZING
};

PingState pingState = PING_IDLE;
unsigned long touchStartTime = 0;
unsigned long pingWaitStartTime = 0;
unsigned long touchReleaseStartTime = 0;

const unsigned long TOUCH_HOLD_TIME = 300;
const unsigned long PING_RAMP_TIME = 800;
const float PING_RAMP_POWER = 0.6;
const unsigned long PING_WAIT_AFTER_RAMP = 400;
const unsigned long PING_BUZZ_TIME = 50;
const float PING_BUZZ_POWER = 1.0;
const unsigned long TOUCH_RELEASE_DEBOUNCE = 100;

// Battery monitoring
float voltageHistory[10] = {0};
int voltageHistoryIndex = 0;
bool voltageHistoryFilled = false;
unsigned long lastVoltageUpdate = 0;
const float VOLTAGE_DIVIDER_RATIO = 2.0;  // Adjust based on your voltage divider circuit (2x for 2:1 divider)

// Forward declarations
void processCommand(String command);

// BLE Server callbacks
class ServerCallbacks: public BLEServerCallbacks {
  void onConnect(BLEServer* pServer) {
    deviceConnected = true;
    Serial.println("Client connected");
  }

  void onDisconnect(BLEServer* pServer) {
    deviceConnected = false;
    Serial.println("Client disconnected");
  }
};

// BLE Characteristic callbacks for RX
class CharacteristicCallbacks: public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* pCharacteristic) {
    // Use getData() and getLength() to avoid String/std::string conversion issues
    uint8_t* data = pCharacteristic->getData();
    size_t length = pCharacteristic->getLength();
    
    if (length > 0) {
      // Convert to Arduino String (null-terminated)
      String command = String((char*)data, length);
      command.trim();
      if (command.length() > 0) {
        processCommand(command);
      }
    }
  }
};

void setup() {
  // CRITICAL: ESP32-C6 GPIO6 must be properly reset to clear all default configurations
  // Step 1: Reset pin to clear all default settings (including internal pull-ups)
  gpio_reset_pin((gpio_num_t)BUZZER_PIN);
  
  // Step 2: Configure pin using low-level GPIO API for full control
  gpio_config_t io_conf = {};
  io_conf.pin_bit_mask = (1ULL << BUZZER_PIN);    // Select GPIO6
  io_conf.mode = GPIO_MODE_OUTPUT;                 // Set as output
  io_conf.pull_up_en = GPIO_PULLUP_DISABLE;        // Disable pull-up
  io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;    // Disable pull-down
  io_conf.intr_type = GPIO_INTR_DISABLE;           // No interrupts
  gpio_config(&io_conf);
  
  // Step 3: Set to LOW immediately
  gpio_set_level((gpio_num_t)BUZZER_PIN, 0);
  delay(10);  // Give pin time to settle
  
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("Ping Bracelet ESP32-C6 Starting");
  Serial.println("=== GPIO6 Configuration ===");
  Serial.print("Pin state (should be 0): ");
  Serial.println(gpio_get_level((gpio_num_t)BUZZER_PIN));
  Serial.println("Pin configured as OUTPUT with pull-up/down disabled");
  Serial.println("Measure voltage on D6 now - should be 0V");
  Serial.println("===========================");
  
  // Keep pin as digital output LOW - we'll attach PWM only when needed
  // This ensures the pin is truly LOW (0V) when buzzer is off
  // PWM will be attached dynamically when buzzing is needed
  Serial.println("Buzzer pin set to LOW (digital output mode)");
  
  // Calibrate touch sensor
  Serial.println("Calibrating touch sensor...");
  Serial.println("Please do NOT touch the sensor during calibration...");
  delay(1000);
  calibrateTouchSensor();
  
  // Initialize BLE with device name for iPhone visibility
  BLEDevice::init("Ping Bracelet");
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new ServerCallbacks());
  
  // Device Information Service (helps with iPhone visibility)
  BLEService* pDeviceInfoService = pServer->createService(BLEUUID((uint16_t)0x180A));
  BLECharacteristic* pManufacturerChar = pDeviceInfoService->createCharacteristic(
    BLEUUID((uint16_t)0x2A29), 
    BLECharacteristic::PROPERTY_READ
  );
  pManufacturerChar->setValue("Ping Bracelet");
  pDeviceInfoService->start();
  
  // UART Service
  BLEService* pService = pServer->createService(SERVICE_UUID);
  
  // TX Characteristic (notify)
  pTxCharacteristic = pService->createCharacteristic(CHAR_UUID_TX, BLECharacteristic::PROPERTY_NOTIFY);
  pTxCharacteristic->addDescriptor(new BLE2902());
  
  // RX Characteristic (write)
  BLECharacteristic* pRxCharacteristic = pService->createCharacteristic(CHAR_UUID_RX, BLECharacteristic::PROPERTY_WRITE);
  pRxCharacteristic->setCallbacks(new CharacteristicCallbacks());
  
  pService->start();
  
  // Battery Service
  BLEService* pBatteryService = pServer->createService(BATTERY_SERVICE_UUID);
  pBatteryCharacteristic = pBatteryService->createCharacteristic(BATTERY_CHAR_UUID, BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
  pBatteryCharacteristic->addDescriptor(new BLE2902());
  pBatteryService->start();
  
  // Start advertising with proper settings for iPhone visibility
  BLEAdvertising* pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->addServiceUUID(BATTERY_SERVICE_UUID);
  pAdvertising->addServiceUUID(BLEUUID((uint16_t)0x180A));  // Device Information Service
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06);  // Functions that help with iPhone connections
  pAdvertising->setMinPreferred(0x12);
  BLEDevice::startAdvertising();
  
  // Set initial battery level
  updateBatteryLevel();
  
  Serial.println("Bracelet ready!");
  Serial.println("Send commands via BLE UART:");
  Serial.println("  buzz <ms> <power>  - Buzz for ms at power (0.0-1.0)");
  Serial.println("  ramp <ms> <maxpower>  - Ramp up to maxpower over ms");
  Serial.println("  battery - Read battery voltage");
  Serial.println("  recalibrate - Recalibrate touch sensor");
}

void loop() {
  // Handle BLE connection state
  if (!deviceConnected && oldDeviceConnected) {
    delay(500);
    pServer->startAdvertising();
    oldDeviceConnected = deviceConnected;
  }
  if (deviceConnected && !oldDeviceConnected) {
    oldDeviceConnected = deviceConnected;
    sendBLE("Ping Bracelet connected!");
    sendBLE("Commands: buzz <ms> <power>, ramp <ms> <maxpower>, battery, recalibrate");
  }
  
  // Update buzzer (call frequently for smooth PWM)
  updateBuzzer();
  
  // Update ramp
  updateRamp();
  
  // Update ping feature
  updatePingFeature();
  
  // Update touch sensor
  updateTouchSensor();
  
  // Update battery level periodically
  static unsigned long lastBatteryUpdate = 0;
  if (millis() - lastBatteryUpdate > 10000) {
    lastBatteryUpdate = millis();
    updateBatteryLevel();
  }
}

void sendBLE(String message) {
  if (deviceConnected && pTxCharacteristic) {
    pTxCharacteristic->setValue(message.c_str());
    pTxCharacteristic->notify();
  }
  Serial.println(message);
}

void processCommand(String command) {
  command.trim();
  command.toLowerCase();
  
  Serial.print("Command received: ");
  Serial.println(command);
  
  if (command.startsWith("buzz ")) {
    int firstSpace = command.indexOf(' ', 5);
    if (firstSpace > 0) {
      String powerStr = command.substring(firstSpace + 1);
      powerStr.trim();
      
      unsigned long buzzTime = command.substring(5, firstSpace).toInt();
      float power = powerStr.toFloat();
      
      if (power < 0.0) power = 0.0;
      if (power > 1.0) power = 1.0;
      
      if (buzzTime > 0 && buzzTime <= 10000) {
        sendBLE("Buzzing for " + String(buzzTime) + "ms at power " + String(power));
        
        buzzerActive = true;
        buzzerStartTime = millis();
        buzzerDuration = buzzTime;
        buzzerPower = power;
      } else {
        sendBLE("Error: Time must be 1-10000ms");
      }
    } else {
      sendBLE("Error: Invalid format. Use: buzz <ms> <power>");
    }
  }
  else if (command.startsWith("ramp ")) {
    int firstSpace = command.indexOf(' ', 5);
    if (firstSpace > 0) {
      String powerStr = command.substring(firstSpace + 1);
      powerStr.trim();
      
      unsigned long rampTime = command.substring(5, firstSpace).toInt();
      float maxPower = powerStr.toFloat();
      
      if (maxPower < 0.0) maxPower = 0.0;
      if (maxPower > 1.0) maxPower = 1.0;
      
      if (rampTime > 0 && rampTime <= 10000) {
        sendBLE("Ramping to power " + String(maxPower) + " over " + String(rampTime) + "ms");
        
        rampActive = true;
        rampStartTime = millis();
        rampDuration = rampTime;
        rampMaxPower = maxPower;
      } else {
        sendBLE("Error: Time must be 1-10000ms");
      }
    } else {
      sendBLE("Error: Invalid format. Use: ramp <ms> <maxpower>");
    }
  }
  else if (command == "battery" || command == "bat") {
    float batteryVoltage = readBatteryVoltage();
    sendBLE("Battery: " + String(batteryVoltage, 3) + " V");
  }
  else if (command == "recalibrate" || command == "calibrate") {
    sendBLE("Recalibrating touch sensor...");
    calibrateTouchSensor();
    sendBLE("Calibration complete!");
  }
  else {
    sendBLE("Unknown command!");
    sendBLE("Available: buzz <ms> <power>, ramp <ms> <maxpower>, battery, recalibrate");
  }
}

float readBatteryVoltage() {
  // ESP32-C6: Use analogReadMilliVolts() for accurate voltage reading
  // Average multiple readings to reduce noise
  uint32_t sumMilliVolts = 0;
  const int numReadings = 16;
  
  for (int i = 0; i < numReadings; i++) {
    sumMilliVolts += analogReadMilliVolts(BATTERY_PIN);
  }
  
  // Calculate pin voltage in volts
  float pinVoltage = (sumMilliVolts / numReadings) / 1000.0;
  
  // Apply voltage divider ratio (adjust based on your hardware)
  // For a 2:1 divider (two equal resistors), use 2.0
  // For other ratios, adjust accordingly
  float batteryVoltage = pinVoltage * VOLTAGE_DIVIDER_RATIO;
  
  return batteryVoltage;
}

uint8_t voltageToPercentage(float voltage) {
  const float minVoltage = 3.0;
  const float maxVoltage = 4.2;
  
  if (voltage >= maxVoltage) return 100;
  if (voltage <= minVoltage) return 0;
  
  return (uint8_t)(((voltage - minVoltage) / (maxVoltage - minVoltage)) * 100.0);
}

float getRestingBatteryVoltage() {
  float currentVoltage = readBatteryVoltage();
  unsigned long now = millis();
  
  if (!buzzerActive && (now - lastVoltageUpdate > 1000)) {
    voltageHistory[voltageHistoryIndex] = currentVoltage;
    voltageHistoryIndex = (voltageHistoryIndex + 1) % 10;
    if (voltageHistoryIndex == 0) voltageHistoryFilled = true;
    lastVoltageUpdate = now;
  }
  
  float sum = 0;
  int count = voltageHistoryFilled ? 10 : voltageHistoryIndex;
  if (count == 0) return currentVoltage;
  
  for (int i = 0; i < count; i++) {
    sum += voltageHistory[i];
  }
  
  return sum / count;
}

void updateBatteryLevel() {
  float restingVoltage = getRestingBatteryVoltage();
  uint8_t percentage = voltageToPercentage(restingVoltage);
  
  static unsigned long lastDebug = 0;
  if (millis() - lastDebug > 10000) {
    lastDebug = millis();
    Serial.print("Battery: ");
    Serial.print(restingVoltage, 3);
    Serial.print("V (");
    Serial.print(percentage);
    Serial.println("%)");
  }
  
  if (deviceConnected && pBatteryCharacteristic) {
    pBatteryCharacteristic->setValue(&percentage, 1);
    pBatteryCharacteristic->notify();
  }
}

// Helper function to ensure buzzer pin is OFF (0V)
void turnOffBuzzer() {
  if (pwmChannel >= 0) {
    ledcWrite(pwmChannel, 0);
    ledcDetach(BUZZER_PIN);  // Detach PWM to release pin
    pwmChannel = -1;
  }
  // Reconfigure as output and set LOW using GPIO API
  gpio_set_direction((gpio_num_t)BUZZER_PIN, GPIO_MODE_OUTPUT);
  gpio_pullup_dis((gpio_num_t)BUZZER_PIN);
  gpio_set_level((gpio_num_t)BUZZER_PIN, 0);
}

void updateBuzzer() {
  if (buzzerActive) {
    // Attach PWM if not already attached
    if (pwmChannel < 0) {
      pwmChannel = ledcAttach(BUZZER_PIN, PWM_FREQ, PWM_RESOLUTION);
    }
    
    unsigned long elapsed = millis() - buzzerStartTime;
    
    if (elapsed >= buzzerDuration) {
      buzzerActive = false;
      turnOffBuzzer();  // Use helper to ensure pin is truly LOW
      sendBLE("Buzzer done!");
    } else {
      // Use hardware PWM for smooth control
      uint8_t pwmValue = (uint8_t)(buzzerPower * 255);
      if (pwmChannel >= 0) {
        ledcWrite(pwmChannel, pwmValue);
      }
    }
  } else {
    // Ensure pin is LOW when buzzer is not active
    turnOffBuzzer();
  }
}

void triggerBuzz(unsigned long duration, float power) {
  buzzerActive = true;
  buzzerStartTime = millis();
  buzzerDuration = duration;
  buzzerPower = power;
}

void updateRamp() {
  if (rampActive) {
    unsigned long elapsed = millis() - rampStartTime;
    bool isPingRamp = (pingState == PING_RAMPING);
    
    if (elapsed >= rampDuration && !isPingRamp) {
      rampActive = false;
      buzzerActive = false;
      turnOffBuzzer();  // Ensure pin is LOW
      sendBLE("Ramp complete!");
    } else {
      float progress = (float)elapsed / (float)rampDuration;
      if (progress > 1.0) progress = 1.0;
      float currentPower = progress * rampMaxPower;
      
      if (!buzzerActive) {
        buzzerActive = true;
        buzzerStartTime = rampStartTime;
        buzzerDuration = rampDuration + 1000;
      }
      buzzerPower = currentPower;
    }
  }
}

void calibrateTouchSensor() {
  // ESP32-C6 doesn't have capacitive touch, use analog input instead
  uint32_t sum = 0;
  const int numReadings = 10;
  
  for (int i = 0; i < numReadings; i++) {
    uint16_t reading = analogRead(TOUCH_PIN);
    sum += reading;
    delay(200);
  }
  
  touchBaseline = sum / numReadings;
  
  Serial.print("Touch baseline: ");
  Serial.println(touchBaseline);
  Serial.print("Touch threshold: ");
  Serial.println(calculateTouchThreshold());
}

uint16_t calculateTouchThreshold() {
  // For analog touch sensor, touched = higher value (opposite of capacitive)
  // Threshold is baseline * (1 + threshold_percent)
  return (uint16_t)(touchBaseline * (1.0 + (1.0 - TOUCH_THRESHOLD_PERCENT)));
}

void updateTouchSensor() {
  unsigned long currentMillis = millis();
  
  if (currentMillis - lastTouchRead >= TOUCH_READ_INTERVAL) {
    // ESP32-C6 doesn't have capacitive touch, use analog input
    uint16_t touchValue = analogRead(TOUCH_PIN);
    uint16_t threshold = calculateTouchThreshold();
    
    // For analog touch sensor, touched = higher value
    isTouched = (touchValue > threshold);
    
    lastTouchRead = currentMillis;
    
    // Debug output
    static unsigned long lastDebugOutput = 0;
    if (currentMillis - lastDebugOutput >= 200) {
      lastDebugOutput = currentMillis;
      String touchMsg = "Touch: raw=" + String(touchValue) + 
                       " | baseline=" + String(touchBaseline) + 
                       " | threshold=" + String(threshold) +
                       " | isTouched=" + String(isTouched ? "TRUE" : "FALSE");
      Serial.println(touchMsg);
      if (deviceConnected) {
        sendBLE(touchMsg);
      }
    }
  }
}

void updatePingFeature() {
  unsigned long currentMillis = millis();
  
  switch (pingState) {
    case PING_IDLE:
      if (isTouched) {
        touchStartTime = currentMillis;
        pingState = PING_TOUCH_HOLDING;
      }
      break;
      
    case PING_TOUCH_HOLDING:
      if (!isTouched) {
        pingState = PING_IDLE;
        touchReleaseStartTime = 0;
      } else if (currentMillis - touchStartTime >= TOUCH_HOLD_TIME) {
        pingState = PING_RAMPING;
        touchReleaseStartTime = 0;
        
        rampActive = true;
        rampStartTime = currentMillis;
        rampDuration = PING_RAMP_TIME;
        rampMaxPower = PING_RAMP_POWER;
        
        Serial.println("Ping: Starting ramp (touch held 300ms)");
      }
      break;
      
    case PING_RAMPING:
      if (!isTouched) {
        if (touchReleaseStartTime == 0) {
          touchReleaseStartTime = currentMillis;
        }
        
        if (currentMillis - touchReleaseStartTime >= TOUCH_RELEASE_DEBOUNCE) {
          pingState = PING_IDLE;
          rampActive = false;
          buzzerActive = false;
          turnOffBuzzer();  // Ensure pin is LOW
          touchReleaseStartTime = 0;
          Serial.println("Ping: Cancelled (touch released during ramp)");
        }
      } else {
        touchReleaseStartTime = 0;
        
        unsigned long rampElapsed = currentMillis - rampStartTime;
        if (rampElapsed >= rampDuration) {
          pingState = PING_WAITING_AFTER_RAMP;
          pingWaitStartTime = currentMillis;
          rampActive = false;
          buzzerActive = false;
          turnOffBuzzer();  // Ensure pin is LOW
          touchReleaseStartTime = 0;
          Serial.println("Ping: Ramp complete, waiting 400ms");
        }
      }
      break;
      
    case PING_WAITING_AFTER_RAMP:
      if (currentMillis - pingWaitStartTime >= PING_WAIT_AFTER_RAMP) {
        pingState = PING_BUZZING;
        triggerBuzz(PING_BUZZ_TIME, PING_BUZZ_POWER);
        Serial.println("Ping: Starting final buzz");
      }
      break;
      
    case PING_BUZZING:
      if (!buzzerActive) {
        pingState = PING_IDLE;
        sendBLE("ping sent!");
        Serial.println("Ping: Complete! Notification sent.");
      }
      break;
  }
}

