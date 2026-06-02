/*
 * Ping Bracelet - ILY Bracelet Firmware
 * ESP32-C6 BLE-enabled wearable with haptic feedback
 * 
 * Features:
 * - BLE UART service for remote control
 * - Battery monitoring and reporting
 * - Touch-based ping feature
 * - Simple buzzer control
 * 
 * TODO: Change battery update interval back to 10 seconds (currently 0.5s for debugging)
 */

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

// Pin definitions
#define BUZZER_PIN D6
#define TOUCH_PIN A0  // D0
#define BATTERY_PIN A1  // D1

// BLE Service UUIDs
#define SERVICE_UUID_UART "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHAR_UUID_RX "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHAR_UUID_TX "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"
#define SERVICE_UUID_BATTERY "180F"
#define CHAR_UUID_BATTERY_LEVEL "2A19"
#define SERVICE_UUID_DEVICE_INFO "180A"
#define CHAR_UUID_MANUFACTURER "2A29"

// BLE objects
BLEServer* pServer = NULL;
BLECharacteristic* pTxCharacteristic = NULL;
BLECharacteristic* pBatteryCharacteristic = NULL;
bool deviceConnected = false;
bool oldDeviceConnected = false;

// Buzzer control
unsigned long buzzStartTime = 0;
unsigned long buzzDuration = 0;
float buzzPower = 0.0;
bool isRamping = false;
unsigned long rampStartTime = 0;
unsigned long rampDuration = 0;
float rampMaxPower = 0.0;

// Touch sensor (capacitive - floating wire with metal plate)
// Uses RC timing: charge pin, then measure discharge time
// Higher capacitance = longer discharge time
unsigned long touchBaseline = 0;
const unsigned long TOUCH_THRESHOLD = 100;  // Difference in microseconds from baseline to detect touch
const int TOUCH_SAMPLES = 5;  // Number of samples to average for stability
bool touchDetected = false;
unsigned long touchStartTime = 0;
const unsigned long TOUCH_HOLD_TIME = 300;  // ms to hold before ping starts
const unsigned long PING_RAMP_TIME = 800;   // ms to ramp up
const unsigned long PING_WAIT_TIME = 200;   // ms to wait after ramp
const unsigned long PING_FINAL_BUZZ = 50;   // ms final buzz
const float PING_RAMP_POWER = 0.6;          // 60% power for ramp
const float PING_FINAL_POWER = 1.0;         // 100% power for final buzz
const unsigned long PING_COOLDOWN = 10000;  // 10 seconds cooldown between pings
unsigned long lastPingTime = 0;  // Timestamp of last ping completion

// Touch ping state machine
enum TouchPingState {
  TOUCH_IDLE,
  TOUCH_HOLDING,
  TOUCH_RAMPING,
  TOUCH_WAITING,
  TOUCH_FINAL_BUZZ
};
TouchPingState touchPingState = TOUCH_IDLE;
unsigned long touchPingStartTime = 0;

// Battery monitoring
// Measured: A1 voltage = 0.815V, Battery voltage = 3.951V
const float BATTERY_VOLTAGE_DIVIDER = 2;  // Measured divider ratio
const float BATTERY_FULL_VOLTAGE = 4.2;     // Full charge voltage
const float BATTERY_EMPTY_VOLTAGE = 3.0;    // Empty voltage
const int ADC_MAX = 4095;                   // ESP32-C6 12-bit ADC
const float ADC_REF_VOLTAGE = 3.3;          // Reference voltage

// Forward declaration
void processCommand(String command);

// Callback for BLE connection
class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
      deviceConnected = true;
    }

    void onDisconnect(BLEServer* pServer) {
      deviceConnected = false;
    }
};

// Callback for RX characteristic (receiving commands)
class MyCallbacks: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) {
      String rxValue = pCharacteristic->getValue();

      if (rxValue.length() > 0) {
        String command = rxValue;
        command.trim();
        command.toLowerCase();
        
        processCommand(command);
      }
    }
};

void setup() {
  Serial.begin(115200);
  
  // Initialize pins
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(TOUCH_PIN, INPUT);
  pinMode(BATTERY_PIN, INPUT);
  
  // Calibrate touch sensor
  calibrateTouchSensor();
  
  // Initialize BLE
  BLEDevice::init("Ping Bracelet");
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  // UART Service
  BLEService *pUartService = pServer->createService(SERVICE_UUID_UART);
  
  pTxCharacteristic = pUartService->createCharacteristic(
    CHAR_UUID_TX,
    BLECharacteristic::PROPERTY_NOTIFY
  );
  pTxCharacteristic->addDescriptor(new BLE2902());
  
  BLECharacteristic *pRxCharacteristic = pUartService->createCharacteristic(
    CHAR_UUID_RX,
    BLECharacteristic::PROPERTY_WRITE
  );
  pRxCharacteristic->setCallbacks(new MyCallbacks());
  
  pUartService->start();

  // Battery Service
  BLEService *pBatteryService = pServer->createService(SERVICE_UUID_BATTERY);
  
  pBatteryCharacteristic = pBatteryService->createCharacteristic(
    CHAR_UUID_BATTERY_LEVEL,
    BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY
  );
  pBatteryCharacteristic->addDescriptor(new BLE2902());
  
  pBatteryService->start();

  // Device Information Service
  BLEService *pDeviceInfoService = pServer->createService(SERVICE_UUID_DEVICE_INFO);
  
  BLECharacteristic *pManufacturerCharacteristic = pDeviceInfoService->createCharacteristic(
    CHAR_UUID_MANUFACTURER,
    BLECharacteristic::PROPERTY_READ
  );
  pManufacturerCharacteristic->setValue("Ping Bracelet");
  
  pDeviceInfoService->start();

  // Start advertising
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID_UART);
  pAdvertising->addServiceUUID(SERVICE_UUID_BATTERY);
  pAdvertising->addServiceUUID(SERVICE_UUID_DEVICE_INFO);
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06);
  pAdvertising->setMinPreferred(0x12);
  BLEDevice::startAdvertising();
  
  Serial.println("Ping Bracelet ready!");
  sendBLEMessage("Ping Bracelet ready!");
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
  }
  
  // Update buzzer
  updateBuzzer();
  
  // Update touch sensor and ping feature
  updateTouchSensor();
  updateTouchPing();
  
  // Update battery level periodically
  static unsigned long lastBatteryUpdate = 0;
  if (millis() - lastBatteryUpdate > 500) {  // Update every 0.5 seconds (TODO: change back to 10000 for 10 seconds)
    // Always read battery voltage for debug output
    float voltage = readBatteryVoltage();
    // Update BLE characteristic if connected
    updateBatteryLevel(voltage);
    lastBatteryUpdate = millis();
  }
  
  delay(10);  // Small delay to prevent tight loop
}

void processCommand(String command) {
  if (command.startsWith("buzz ")) {
    // Parse: buzz <ms> <power>
    int firstSpace = command.indexOf(' ', 5);  // Find space after "buzz "
    
    if (firstSpace > 0) {
      // Extract ms value (between position 5 and firstSpace)
      unsigned long ms = command.substring(5, firstSpace).toInt();
      // Extract power value (everything after firstSpace)
      float power = command.substring(firstSpace + 1).toFloat();
      
      ms = constrain(ms, 1, 10000);
      power = constrain(power, 0.0, 1.0);
      
      startBuzz(ms, power);
      sendBLEMessage("Buzz: " + String(ms) + "ms at " + String(power * 100) + "%");
    }
  }
  else if (command.startsWith("ramp ")) {
    // Parse: ramp <ms> <maxpower>
    int firstSpace = command.indexOf(' ', 5);  // Find space after "ramp "
    
    if (firstSpace > 0) {
      // Extract ms value (between position 5 and firstSpace)
      unsigned long ms = command.substring(5, firstSpace).toInt();
      // Extract maxPower value (everything after firstSpace)
      float maxPower = command.substring(firstSpace + 1).toFloat();
      
      ms = constrain(ms, 1, 10000);
      maxPower = constrain(maxPower, 0.0, 1.0);
      
      startRamp(ms, maxPower);
      sendBLEMessage("Ramp: " + String(ms) + "ms to " + String(maxPower * 100) + "%");
    }
  }
  else if (command == "battery" || command == "bat") {
    float voltage = readBatteryVoltage();
    int percentage = calculateBatteryPercentage(voltage);
    sendBLEMessage("Battery: " + String(voltage, 2) + "V (" + String(percentage) + "%)");
  }
  else if (command == "recalibrate" || command == "calibrate") {
    calibrateTouchSensor();
    sendBLEMessage("Touch sensor recalibrated");
  }
  else {
    sendBLEMessage("Unknown command: " + command);
  }
}

void startBuzz(unsigned long duration, float power) {
  buzzStartTime = millis();
  buzzDuration = duration;
  buzzPower = power;
  isRamping = false;
}

void startRamp(unsigned long duration, float maxPower) {
  rampStartTime = millis();
  rampDuration = duration;
  rampMaxPower = maxPower;
  isRamping = true;
  buzzStartTime = 0;  // Clear any active buzz
}

void updateBuzzer() {
  unsigned long currentTime = millis();
  
  // Handle ramping
  if (isRamping) {
    unsigned long elapsed = currentTime - rampStartTime;
    if (elapsed < rampDuration) {
      // Calculate power based on elapsed time (0.0 to rampMaxPower)
      float progress = (float)elapsed / (float)rampDuration;
      float currentPower = progress * rampMaxPower;
      setBuzzerPower(currentPower);
      
    } else {
      // Ramp complete, set to max power
      setBuzzerPower(rampMaxPower);
      isRamping = false;
    }
    return;
  }
  
  // Handle regular buzz
  if (buzzStartTime > 0) {
    unsigned long elapsed = currentTime - buzzStartTime;
    if (elapsed < buzzDuration) {
      setBuzzerPower(buzzPower);
    } else {
      setBuzzerPower(0.0);
      buzzStartTime = 0;
    }
  } else {
    // No active buzz, but check touch ping state
    if (touchPingState == TOUCH_IDLE) {
      setBuzzerPower(0.0);
    }
  }
}

void setBuzzerPower(float power) {
  power = constrain(power, 0.0, 1.0);
  int pwmValue = (int)(power * 255);
  analogWrite(BUZZER_PIN, pwmValue);
}

// Measure capacitive touch using RC timing with analog threshold
// Method: Charge pin, then measure discharge time using analog threshold
// Returns discharge time in microseconds (higher = more capacitance = touched)
unsigned long readCapacitiveTouch() {
  unsigned long totalTime = 0;
  
  // Take multiple samples and average for stability
  for (int i = 0; i < TOUCH_SAMPLES; i++) {
    // Charge the capacitor by setting pin HIGH as output
    pinMode(TOUCH_PIN, OUTPUT);
    digitalWrite(TOUCH_PIN, HIGH);
    delayMicroseconds(100);  // Charge time
    
    // Switch to input (floating) and measure discharge time
    // Use analogRead to detect when voltage drops below threshold
    pinMode(TOUCH_PIN, INPUT);
    
    unsigned long startTime = micros();
    unsigned long timeout = startTime + 100000;  // 100ms timeout
    int threshold = 512;  // Mid-point of ADC (1.65V)
    
    // Measure how long it takes to discharge below threshold
    // Higher capacitance = longer discharge time
    unsigned long dischargeTime = 0;
    while (micros() < timeout) {
      int adcValue = analogRead(TOUCH_PIN);
      if (adcValue < threshold) {
        dischargeTime = micros() - startTime;
        break;
      }
      delayMicroseconds(10);  // Sample every 10us
    }
    
    // If timeout, use max value
    if (dischargeTime == 0) {
      dischargeTime = 100000;
    }
    
    totalTime += dischargeTime;
    
    // Small delay between samples
    delayMicroseconds(50);
  }
  
  return totalTime / TOUCH_SAMPLES;  // Return average
}

void calibrateTouchSensor() {
  // Take multiple readings and average for capacitive touch baseline
  unsigned long sum = 0;
  for (int i = 0; i < 50; i++) {
    sum += readCapacitiveTouch();
    delay(10);
  }
  touchBaseline = sum / 50;
  Serial.print("[TOUCH] Baseline calibrated: ");
  Serial.print(touchBaseline);
  Serial.println(" microseconds");
}

void updateTouchSensor() {
  unsigned long touchValue = readCapacitiveTouch();
  long touchDiff = (long)touchValue - (long)touchBaseline;
  
  // Capacitive touch: discharge time increases when touched (more capacitance)
  // Compare signed difference to signed threshold
  bool currentlyTouched = (touchDiff > (long)TOUCH_THRESHOLD);
  
  // Debug output - only show when touch state changes
  static bool lastTouched = false;
  if (currentlyTouched != lastTouched) {
    lastTouched = currentlyTouched;
  }
  
  if (currentlyTouched && !touchDetected) {
    // Touch just started
    touchDetected = true;
    touchStartTime = millis();
  } else if (!currentlyTouched && touchDetected) {
    // Touch released
    touchDetected = false;
    if (touchPingState == TOUCH_HOLDING || touchPingState == TOUCH_RAMPING) {
      // Cancel ping if released during hold or ramp
      touchPingState = TOUCH_IDLE;
      setBuzzerPower(0.0);
    }
  }
}

void updateTouchPing() {
  unsigned long currentTime = millis();
  
  switch (touchPingState) {
    case TOUCH_IDLE:
      // Check cooldown period before allowing new ping
      if (touchDetected && 
          (currentTime - touchStartTime >= TOUCH_HOLD_TIME) &&
          (currentTime - lastPingTime >= PING_COOLDOWN)) {
        // Start ping sequence
        touchPingState = TOUCH_RAMPING;
        touchPingStartTime = currentTime;
      }
      break;
      
    case TOUCH_RAMPING:
      {
        unsigned long elapsed = currentTime - touchPingStartTime;
        if (elapsed < PING_RAMP_TIME) {
          // Ramp from 0 to PING_RAMP_POWER over PING_RAMP_TIME
          float progress = (float)elapsed / (float)PING_RAMP_TIME;
          float currentPower = progress * PING_RAMP_POWER;
          setBuzzerPower(currentPower);
        } else {
          // Ramp complete, move to wait state
          touchPingState = TOUCH_WAITING;
          touchPingStartTime = currentTime;
          setBuzzerPower(0.0);
        }
        
        // Check if touch released (cancel)
        if (!touchDetected) {
          touchPingState = TOUCH_IDLE;
          setBuzzerPower(0.0);
        }
      }
      break;
      
    case TOUCH_WAITING:
      {
        unsigned long elapsed = currentTime - touchPingStartTime;
        if (elapsed >= PING_WAIT_TIME) {
          // Wait complete, do final buzz
          touchPingState = TOUCH_FINAL_BUZZ;
          touchPingStartTime = currentTime;
          setBuzzerPower(PING_FINAL_POWER);
        }
      }
      break;
      
    case TOUCH_FINAL_BUZZ:
      {
        unsigned long elapsed = currentTime - touchPingStartTime;
        if (elapsed >= PING_FINAL_BUZZ) {
          // Final buzz complete - record ping time for cooldown
          touchPingState = TOUCH_IDLE;
          setBuzzerPower(0.0);
          lastPingTime = currentTime;
          sendBLEMessage("ping sent!");
        }
      }
      break;
  }
}

float readBatteryVoltage() {
  int adcValue = analogRead(BATTERY_PIN);
  // ADC reads the divided voltage (battery voltage / 2)
  // Convert ADC reading to voltage at the ADC pin
  float adcVoltage = (float)adcValue / (float)ADC_MAX * ADC_REF_VOLTAGE;
  // Multiply by divider ratio to get actual battery voltage
  float batteryVoltage = adcVoltage * BATTERY_VOLTAGE_DIVIDER;
  
  // Detailed debug output
  Serial.print("[BATTERY] ADC raw: ");
  Serial.print(adcValue);
  Serial.print(" | ADC max: ");
  Serial.print(ADC_MAX);
  Serial.print(" | ADC ref voltage: ");
  Serial.print(ADC_REF_VOLTAGE, 3);
  Serial.print("V | ADC voltage: ");
  Serial.print(adcVoltage, 3);
  Serial.print("V | Divider ratio: ");
  Serial.print(BATTERY_VOLTAGE_DIVIDER, 2);
  Serial.print(" | Battery voltage: ");
  Serial.print(batteryVoltage, 3);
  Serial.println("V");
  
  return batteryVoltage;
}

int calculateBatteryPercentage(float voltage) {
  if (voltage >= BATTERY_FULL_VOLTAGE) return 100;
  if (voltage <= BATTERY_EMPTY_VOLTAGE) return 0;
  
  float percentage = ((voltage - BATTERY_EMPTY_VOLTAGE) / 
                     (BATTERY_FULL_VOLTAGE - BATTERY_EMPTY_VOLTAGE)) * 100.0;
  return constrain((int)percentage, 0, 100);
}

void updateBatteryLevel(float voltage) {
  if (deviceConnected) {
    int percentage = calculateBatteryPercentage(voltage);
    
    uint8_t batteryLevel = (uint8_t)percentage;
    pBatteryCharacteristic->setValue(&batteryLevel, 1);
    pBatteryCharacteristic->notify();
  }
}

void sendBLEMessage(String message) {
  if (deviceConnected && pTxCharacteristic != NULL) {
    pTxCharacteristic->setValue(message.c_str());
    pTxCharacteristic->notify();
  }
  Serial.println(message);
}
