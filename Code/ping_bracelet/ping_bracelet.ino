/*
 * Ping Bracelet - BLE Buzzer Control
 * Pin 23: Buzzer
 * 
 * Commands via BLE UART:
 *   buzz <ms> <power>  - Buzz for ms milliseconds at power (0.0-1.0)
 */

 #include <bluefruit.h>

 BLEUart bleuart;  // UART service for commands
 BLEService batteryService = BLEService(0x180F);  // Battery Service UUID
 BLECharacteristic batteryLevelChar = BLECharacteristic(0x2A19);  // Battery Level Characteristic UUID
 
#define BUZZER_PIN 23
#define BUTTON_PIN 18  // Button pin with INPUT_PULLUP (LOW when pressed)
#define BATTERY_PIN 20  // Battery voltage monitoring pin
#define PWM_PERIOD 10  // PWM period in 60milliseconds (10ms = 100Hz)

// Device role toggle: changes the name of the device
#define DEVICE_ROLE_HIS false

// Battery optimization: Set to true for debugging, false for production
#define ENABLE_SERIAL_DEBUG false

// Button interrupt flag (set by interrupt handler, checked in loop)
volatile bool buttonInterruptFlag = false;
 
 // Buzzer state
 bool buzzerActive = false;
 unsigned long buzzerStartTime = 0;
 unsigned long buzzerDuration = 0;
 float buzzerPower = 1.0;  // Power level 0.0-1.0
 unsigned long lastPWMUpdate = 0;
 bool pwmState = false;
 
 // Ramp state
 bool rampActive = false;
 unsigned long rampStartTime = 0;
 unsigned long rampDuration = 0;
 float rampMaxPower = 0.0;
 
// Button state (INPUT_PULLUP: HIGH when not pressed, LOW when pressed)
bool isButtonPressed = false;
bool wasButtonPressed = false;
bool buttonReleased = true;  // Track if button has been released (start as true to allow first press)

// Ping feature state
enum PingState {
  PING_IDLE,
  PING_WAITING,     // Wait 150ms after button press
  PING_RAMPING      // Ramp in progress
};

PingState pingState = PING_IDLE;
unsigned long pingStartTime = 0;
const unsigned long PING_WAIT_TIME = 150;      // Wait 150ms after button press
const unsigned long PING_RAMP_TIME = 200;      // Ramp duration: 200ms
const float PING_RAMP_POWER = 0.6;             // Ramp to 60% power

// Device identification
String deviceName = "";
uint8_t deviceUUID[16] = {0};

// Generate unique device name based on DEVICE_ROLE_HIS constant
String generateDeviceName() {
  return DEVICE_ROLE_HIS ? "HIS Couple Bracelet" : "HERS Couple Bracelet";
}

// Button interrupt handler - called when button is pressed
void buttonInterruptHandler() {
  buttonInterruptFlag = true;  // Volatile flag, checked in loop
}

// Generate unique UUID from MAC address (128-bit UUID)
void generateDeviceUUID() {
  ble_gap_addr_t addr = Bluefruit.getAddr();
  // Create UUID v4-like structure from MAC address
  // Format: xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx
  // where 4 indicates version 4, y is 8, 9, A, or B
  deviceUUID[0] = addr.addr[5];
  deviceUUID[1] = addr.addr[4];
  deviceUUID[2] = addr.addr[3];
  deviceUUID[3] = addr.addr[2];
  deviceUUID[4] = addr.addr[1];
  deviceUUID[5] = addr.addr[0];
  deviceUUID[6] = (addr.addr[1] << 4) | (addr.addr[0] & 0x0F);
  deviceUUID[7] = addr.addr[2];
  deviceUUID[8] = 0x40 | ((addr.addr[3] >> 4) & 0x0F);  // Version 4
  deviceUUID[9] = (addr.addr[3] & 0x0F) | 0x80;  // Variant bits
  deviceUUID[10] = addr.addr[4];
  deviceUUID[11] = addr.addr[5];
  deviceUUID[12] = addr.addr[0];
  deviceUUID[13] = addr.addr[1];
  deviceUUID[14] = addr.addr[2];
  deviceUUID[15] = addr.addr[3];
}
 
 void setup() {
   // Initialize Serial
   Serial.begin(115200);
   while (!Serial && millis() < 5000) delay(10);
   
   // Initialize Bluefruit (required for Serial on this board)
  // Retry initialization if it fails
  int retryCount = 0;
  while (!Bluefruit.begin() && retryCount < 10) {
    Serial.print("ERROR: Bluefruit.begin() failed! Retry ");
    Serial.print(retryCount + 1);
    Serial.println("/10");
    delay(1000);
    retryCount++;
  }
  
  if (retryCount >= 10) {
    Serial.println("WARNING: BLE initialization failed after 10 retries. Continuing anyway...");
  } else {
    #if ENABLE_SERIAL_DEBUG
    Serial.println("BLE initialized successfully");
    #endif
  }
  
  Bluefruit.setTxPower(0);  // Reduced from 4 for better battery life
  
  // Generate unique device name and UUID based on MAC address
  deviceName = generateDeviceName();
  generateDeviceUUID();
  Bluefruit.setName(deviceName.c_str());
  
  #if ENABLE_SERIAL_DEBUG
  Serial.print("Device Name: ");
  Serial.println(deviceName);
  Serial.print("Device UUID: ");
  for (int i = 0; i < 16; i++) {
    if (deviceUUID[i] < 0x10) Serial.print("0");
    Serial.print(deviceUUID[i], HEX);
    if (i == 3 || i == 5 || i == 7 || i == 9) Serial.print("-");
  }
  Serial.println();
  #endif
   
   #if ENABLE_SERIAL_DEBUG
   Serial.println("Ping Bracelet Starting");
   #endif
  
  delay(1000);
   #if ENABLE_SERIAL_DEBUG
   Serial.print("Buzzer Pin: ");
   Serial.println(BUZZER_PIN);
   Serial.print("Button Pin: ");
   Serial.println(BUTTON_PIN);
   #endif
   
   // Set up buzzer pin
   pinMode(BUZZER_PIN, OUTPUT);
   digitalWrite(BUZZER_PIN, LOW);
   
  // Set up button pin with internal pull-up (LOW when pressed)
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  
  // Attach interrupt for button - ensures button works even during delays/sleep
  attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), buttonInterruptHandler, FALLING);
   
   // Set connection callbacks
   Bluefruit.Periph.setConnectCallback(connect_callback);
   Bluefruit.Periph.setDisconnectCallback(disconnect_callback);
   
   // Configure and Start BLE UART Service
   bleuart.begin();
   
   // Configure and Start BLE Battery Service
   batteryService.begin();
   
   // Configure Battery Level Characteristic
   // Properties: Read, Notify
   batteryLevelChar.setProperties(CHR_PROPS_READ | CHR_PROPS_NOTIFY);
   batteryLevelChar.setPermission(SECMODE_OPEN, SECMODE_NO_ACCESS);
   batteryLevelChar.setFixedLen(1);  // Battery level is 1 byte (0-100)
   batteryLevelChar.begin();
   
  // Give services time to fully initialize
  delay(100);
   
  // Set up and start advertising (must be before updateBatteryLevel)
   startAdv();
  
  // Set initial battery level after advertising starts
  delay(100);
  updateBatteryLevel();
   
   #if ENABLE_SERIAL_DEBUG
   Serial.println("Bracelet ready!");
   Serial.println("Send commands via nRF Connect UART:");
   Serial.println("  buzz <ms> <power>  - Buzz for ms at power (0.0-1.0)");
   Serial.println("  ramp <ms> <maxpower>  - Ramp up to maxpower over ms");
   #endif
 }
 
 void startAdv(void) {
  // Start with absolute basics - minimal configuration
  // Bluefruit.Advertising should already be initialized, just configure it
   
  // Stop any existing advertising first
  Bluefruit.Advertising.stop();
   
  // Clear all data
  Bluefruit.Advertising.clearData();
  Bluefruit.ScanResponse.clearData();
   
  // Add only essential flags
  Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
  
  // Add device name (this is critical for discovery)
   Bluefruit.Advertising.addName();
   
  // Add UART service to advertising (this is what nRF Connect looks for)
  Bluefruit.Advertising.addService(bleuart);
  
  // Configure advertising behavior
   Bluefruit.Advertising.restartOnDisconnect(true);
  
  // Set advertising intervals (in units of 0.625ms)
  // 160 = 100ms, 960 = 600ms (slower for better battery life when not connected)
   Bluefruit.Advertising.setInterval(160, 960);
   Bluefruit.Advertising.setFastTimeout(30);
  
  // Try to start advertising
  bool started = Bluefruit.Advertising.start(0);
  
  if (!started) {
    Serial.println("ERROR: Advertising failed to start!");
    
    // Try absolute minimum - just name and flags
    Serial.println("Trying minimal configuration (name only)...");
    Bluefruit.Advertising.clearData();
    Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
    Bluefruit.Advertising.addName();
    
    if (Bluefruit.Advertising.start(0)) {
      Serial.println("Minimal advertising (name only) started successfully");
    } else {
      Serial.println("ERROR: Even minimal advertising failed!");
      Serial.println("Possible causes:");
      Serial.println("  - BLE stack not properly initialized");
      Serial.println("  - Hardware issue");
      Serial.println("  - Insufficient memory");
    }
  } else {
    #if ENABLE_SERIAL_DEBUG
    Serial.println("Advertising started successfully!");
    Serial.print("Device name: ");
    Serial.println(deviceName);
    Serial.print("Scan for '");
    Serial.print(deviceName);
    Serial.println("' in nRF Connect");
    
    // Print BLE address
    ble_gap_addr_t addr = Bluefruit.getAddr();
    Serial.print("BLE Address: ");
    for (int i = 5; i >= 0; i--) {
      if (addr.addr[i] < 0x10) Serial.print("0");
      Serial.print(addr.addr[i], HEX);
      if (i > 0) Serial.print(":");
    }
    Serial.println();
    #endif
  }
 }
 
 void connect_callback(uint16_t conn_handle) {
   BLEConnection* connection = Bluefruit.Connection(conn_handle);
   
   char central_name[32] = { 0 };
   connection->getPeerName(central_name, sizeof(central_name));
   
   #if ENABLE_SERIAL_DEBUG
   Serial.print("Connected to: ");
   Serial.println(central_name);
   #endif
   
   // Send welcome message via BLE
   bleuart.println("Ping Bracelet connected!");
   bleuart.println("Commands: buzz <ms> <power>, ramp <ms> <maxpower>, battery");
 }
 
 void disconnect_callback(uint16_t conn_handle, uint8_t reason) {
   #if ENABLE_SERIAL_DEBUG
   Serial.print("Disconnected, reason = 0x");
   Serial.println(reason, HEX);
   #endif
 }
 
 void processCommand(String command) {
   command.trim();
   command.toLowerCase();
   
   #if ENABLE_SERIAL_DEBUG
   Serial.print("Command received: ");
   Serial.println(command);
   #endif
   
   // Parse "buzz <ms> <power>"
   if (command.startsWith("buzz ")) {
     int firstSpace = command.indexOf(' ', 5);
     if (firstSpace > 0) {
       // Get power value (may be at end of string or have trailing space)
       String powerStr = command.substring(firstSpace + 1);
       powerStr.trim();  // Remove any trailing whitespace
       
       unsigned long buzzTime = command.substring(5, firstSpace).toInt();
       float power = powerStr.toFloat();
       
       #if ENABLE_SERIAL_DEBUG
       Serial.print("Parsed: time=");
       Serial.print(buzzTime);
       Serial.print(", power=");
       Serial.println(power);
       #endif
       
       // Validate power range (0.0-1.0)
       if (power < 0.0) power = 0.0;
       if (power > 1.0) power = 1.0;
       
       if (buzzTime > 0 && buzzTime <= 10000) {
         #if ENABLE_SERIAL_DEBUG
         Serial.print("Buzzing for ");
         Serial.print(buzzTime);
         Serial.print("ms at power ");
         Serial.println(power);
         #endif
         
         bleuart.print("Buzzing for ");
         bleuart.print(buzzTime);
         bleuart.print("ms at power ");
         bleuart.println(power);
         
         // Start buzzer
         buzzerActive = true;
         buzzerStartTime = millis();
         buzzerDuration = buzzTime;
         buzzerPower = power;
         
         // Set initial state based on power
         #if ENABLE_SERIAL_DEBUG
         Serial.print("Setting buzzer pin ");
         Serial.print(BUZZER_PIN);
         if (power >= 1.0) {
           Serial.println(" HIGH");
         } else if (power > 0.0) {
           Serial.println(" LOW (PWM mode start)");
         } else {
           Serial.println(" LOW (off)");
         }
         #endif
         
         if (power >= 1.0) {
           digitalWrite(BUZZER_PIN, HIGH);
           pwmState = true;
         } else if (power > 0.0) {
           // For PWM mode, always start LOW and let PWM logic handle the first cycle
           // This prevents any initial spike
           digitalWrite(BUZZER_PIN, LOW);
           pwmState = false;
         } else {
           digitalWrite(BUZZER_PIN, LOW);
           pwmState = false;
         }
       } else {
         bleuart.println("Error: Time must be 1-10000ms");
       }
     } else {
       bleuart.println("Error: Invalid format. Use: buzz <ms> <power>");
     }
   }
   // Parse "ramp <ms> <maxpower>"
   else if (command.startsWith("ramp ")) {
     int firstSpace = command.indexOf(' ', 5);
     if (firstSpace > 0) {
       // Get power value (may be at end of string or have trailing space)
       String powerStr = command.substring(firstSpace + 1);
       powerStr.trim();  // Remove any trailing whitespace
       
       unsigned long rampTime = command.substring(5, firstSpace).toInt();
       float maxPower = powerStr.toFloat();
       
       #if ENABLE_SERIAL_DEBUG
       Serial.print("Parsed: time=");
       Serial.print(rampTime);
       Serial.print(", maxpower=");
       Serial.println(maxPower);
       #endif
       
       // Validate power range (0.0-1.0)
       if (maxPower < 0.0) maxPower = 0.0;
       if (maxPower > 1.0) maxPower = 1.0;
       
       if (rampTime > 0 && rampTime <= 10000) {
         #if ENABLE_SERIAL_DEBUG
         Serial.print("Ramping to power ");
         Serial.print(maxPower);
         Serial.print(" over ");
         Serial.print(rampTime);
         Serial.println("ms");
         #endif
         
         bleuart.print("Ramping to power ");
         bleuart.print(maxPower);
         bleuart.print(" over ");
         bleuart.print(rampTime);
         bleuart.println("ms");
         
         // Start ramp
         rampActive = true;
         rampStartTime = millis();
         rampDuration = rampTime;
         rampMaxPower = maxPower;
       } else {
         bleuart.println("Error: Time must be 1-10000ms");
       }
     } else {
       bleuart.println("Error: Invalid format. Use: ramp <ms> <maxpower>");
     }
   }
   // Parse "battery" or "bat" command
   else if (command == "battery" || command == "bat") {
     float batteryVoltage = readBatteryVoltage();
    float restingVoltage = getRestingBatteryVoltage();
    uint8_t percentage = voltageToPercentage(restingVoltage);
    
    // Send detailed battery info
     bleuart.print("Battery: ");
     bleuart.print(batteryVoltage, 3);
    bleuart.print("V (current), ");
    bleuart.print(restingVoltage, 3);
    bleuart.print("V (resting), ");
    bleuart.print(percentage);
    bleuart.println("%");
    
    #if ENABLE_SERIAL_DEBUG
    Serial.print("Battery: ");
     Serial.print(batteryVoltage, 3);
    Serial.print("V (current), ");
    Serial.print(restingVoltage, 3);
    Serial.print("V (resting), ");
    Serial.print(percentage);
    Serial.println("%");
    #endif
   }
     else {
       #if ENABLE_SERIAL_DEBUG
       Serial.println("Unknown command");
       #endif
       bleuart.println("Unknown command!");
       bleuart.println("Available: buzz <ms> <power>, ramp <ms> <maxpower>, battery");
     }
 }
 
// Read battery voltage from pin 20
// Calibrated: 2x multiplier gave 3.901V reading, actual was 4.201V
// Correct multiplier = 4.201 / (3.901 / 2.0) = 2.154
 float readBatteryVoltage() {
  // Average multiple readings to reduce noise
  uint32_t sum = 0;
  const int numReadings = 16;  // More readings for better accuracy
  
  for (int i = 0; i < numReadings; i++) {
    sum += analogRead(BATTERY_PIN);
  }
  
  int rawValue = sum / numReadings;
   
   // Convert to voltage at the pin (after voltage divider)
  // nRF52840: 10-bit ADC (0-1023) with 3.6V reference
   // Voltage = (rawValue / 1023.0) * 3.6
   float pinVoltage = (rawValue / 1023.0) * 3.6;
   
  // Pin 20 is connected through a voltage divider
  // Calibrated multiplier: 2.154 (measured: 2x gave 3.901V, actual 4.201V)
  const float DIVIDER_RATIO = 2.154;
   float batteryVoltage = pinVoltage * DIVIDER_RATIO;
   
   return batteryVoltage;
 }
 
 // Convert battery voltage to percentage (0-100)
// LiPo battery voltage curve (more accurate than linear)
// Uses a more accurate curve based on LiPo discharge characteristics
 uint8_t voltageToPercentage(float voltage) {
   // LiPo typical range: 4.2V (full) to 3.0V (empty)
   // These values are for RESTING voltage (no load)
   // Under load, voltage will be lower due to voltage sag
  
  // Clamp to valid range
  if (voltage >= 4.2) return 100;
  if (voltage <= 3.0) return 0;
  
  // More accurate voltage-to-percentage curve for LiPo batteries
  // Based on typical LiPo discharge curve (not perfectly linear)
  float percentage;
  
  if (voltage >= 4.15) {
    // 100-90%: Very flat region (4.15V - 4.2V)
    percentage = 90.0 + ((voltage - 4.15) / 0.05) * 10.0;
  } else if (voltage >= 3.95) {
    // 90-20%: Steeper drop (3.95V - 4.15V)
    percentage = 20.0 + ((voltage - 3.95) / 0.20) * 70.0;
  } else if (voltage >= 3.7) {
    // 20-5%: Moderate drop (3.7V - 3.95V)
    percentage = 5.0 + ((voltage - 3.7) / 0.25) * 15.0;
  } else {
    // 5-0%: Steep drop near empty (3.0V - 3.7V)
    percentage = ((voltage - 3.0) / 0.7) * 5.0;
  }
  
  // Clamp to 0-100
  if (percentage > 100.0) percentage = 100.0;
  if (percentage < 0.0) percentage = 0.0;
  
  return (uint8_t)percentage;
 }
 
 // Track battery voltage over time to get resting voltage
 float getRestingBatteryVoltage() {
   static float voltageHistory[10] = {0};
   static int historyIndex = 0;
   static unsigned long lastUpdate = 0;
   static bool historyFilled = false;
   
   float currentVoltage = readBatteryVoltage();
   unsigned long now = millis();
   
   // Only update history when not buzzing (to get resting voltage)
   if (!buzzerActive && (now - lastUpdate > 1000)) {  // Update every 1 second when idle
     voltageHistory[historyIndex] = currentVoltage;
     historyIndex = (historyIndex + 1) % 10;
     if (historyIndex == 0) historyFilled = true;
     lastUpdate = now;
   }
   
   // Calculate average of recent readings (resting voltage)
   float sum = 0;
   int count = historyFilled ? 10 : historyIndex;
   if (count == 0) return currentVoltage;  // No history yet, return current
   
   for (int i = 0; i < count; i++) {
     sum += voltageHistory[i];
   }
   
   return sum / count;  // Return average (resting voltage)
 }
 
 // Update BLE Battery Level characteristic
 void updateBatteryLevel() {
   // Use resting voltage (average of recent idle readings) for more accurate percentage
   float restingVoltage = getRestingBatteryVoltage();
   uint8_t percentage = voltageToPercentage(restingVoltage);
   
   // Output to Serial (debug only)
   #if ENABLE_SERIAL_DEBUG
   static unsigned long lastDebug = 0;
   if (millis() - lastDebug > 30000) {  // Every 30 seconds when debug enabled
     lastDebug = millis();
     Serial.print("Battery: ");
     Serial.print(restingVoltage, 3);
     Serial.print("V (");
     Serial.print(percentage);
     Serial.println("%)");
   }
   #endif
   
   // Update BLE if connected
   if (Bluefruit.connected()) {
     // Write battery level (0-100) to characteristic
     batteryLevelChar.write8(percentage);
     
     // Notify subscribers of battery level change
    // Send notification every time (BLE clients can filter if needed)
     batteryLevelChar.notify8(percentage);
    
    // Also send voltage over BLE UART for debugging
    static unsigned long lastBLEVoltage = 0;
    if (millis() - lastBLEVoltage > 30000) {  // Every 30 seconds
      lastBLEVoltage = millis();
      bleuart.print("Battery: ");
      bleuart.print(restingVoltage, 3);
      bleuart.print("V (");
      bleuart.print(percentage);
      bleuart.println("%)");
    }
   }
 }
 
 void updateBuzzer() {
   if (buzzerActive) {
     unsigned long elapsed = millis() - buzzerStartTime;
     
     if (elapsed >= buzzerDuration) {
       // Stop buzzer
       buzzerActive = false;
       digitalWrite(BUZZER_PIN, LOW);
       pwmState = false;
       #if ENABLE_SERIAL_DEBUG
       Serial.println("Buzzer stopped");
       #endif
       bleuart.println("Buzzer done!");
     } else {
       // Software PWM: toggle pin based on power level
       unsigned long currentTime = millis();
       
       if (buzzerPower <= 0.0) {
         digitalWrite(BUZZER_PIN, LOW);
       } else if (buzzerPower >= 1.0) {
         digitalWrite(BUZZER_PIN, HIGH);
       } else {
         // PWM mode: calculate duty cycle
         // Use continuous timer (not relative to buzzerStartTime) for smooth transitions
         // This ensures PWM cycles continuously even when power changes during ramp
         unsigned long periodTime = currentTime % PWM_PERIOD;
         unsigned long highTime = (unsigned long)(buzzerPower * PWM_PERIOD);
         
         if (periodTime < highTime) {
           if (!pwmState) {
             digitalWrite(BUZZER_PIN, HIGH);
             pwmState = true;
           }
         } else {
           if (pwmState) {
             digitalWrite(BUZZER_PIN, LOW);
             pwmState = false;
           }
         }
       }
     }
   }
 }
 
 // Helper function to trigger buzz internally (used by ramp)
 void triggerBuzz(unsigned long duration, float power) {
   // Start buzzer
   buzzerActive = true;
   buzzerStartTime = millis();
   buzzerDuration = duration;
   buzzerPower = power;
   pwmState = false;  // Initialize PWM state
   
   // Set initial state based on power
   if (power >= 1.0) {
     digitalWrite(BUZZER_PIN, HIGH);
     pwmState = true;
   } else if (power > 0.0) {
     digitalWrite(BUZZER_PIN, LOW);
     pwmState = false;
   } else {
     digitalWrite(BUZZER_PIN, LOW);
     pwmState = false;
   }
 }
 
// Update ramp state and continuously update power
// Note: For ping feature ramps, updatePingFeature() controls completion timing
void updateRamp() {
  if (rampActive) {
    unsigned long elapsed = millis() - rampStartTime;
    
    // Only auto-complete if this is NOT a ping feature ramp
    // Ping feature ramps are controlled by updatePingFeature()
    // We can detect ping ramps by checking if we're in PING_RAMPING state
    bool isPingRamp = (pingState == PING_RAMPING);
    
    if (elapsed >= rampDuration && !isPingRamp) {
      // Ramp complete - stop the buzzer (only for non-ping ramps)
      rampActive = false;
      buzzerActive = false;
      digitalWrite(BUZZER_PIN, LOW);
      pwmState = false;
      #if ENABLE_SERIAL_DEBUG
      Serial.println("Ramp complete");
      #endif
      bleuart.println("Ramp complete!");
    } else {
      // Calculate current power (linear ramp from 0 to maxPower)
      float progress = (float)elapsed / (float)rampDuration;
      // Clamp progress to 1.0 to prevent overshooting
      if (progress > 1.0) progress = 1.0;
      float currentPower = progress * rampMaxPower;
      
      // Continuously update buzzer power without restarting
      // This prevents PWM cycle resets that cause pauses
      if (!buzzerActive) {
        // Start buzzer if not already active
        // Set duration longer than ramp to ensure ramp controls timing
        buzzerActive = true;
        buzzerStartTime = rampStartTime;  // Use ramp start time for PWM calculation
        buzzerDuration = rampDuration + 1000;  // Slightly longer than ramp to ensure ramp completes first
      }
      buzzerPower = currentPower;
    }
  }
}
 
 void loop() {
   // Check for incoming UART data from nRF Connect
   if (bleuart.available()) {
     String command = bleuart.readStringUntil('\n');
     processCommand(command);
   }
   
   // Update buzzer state (call frequently for PWM) - do this FIRST for smooth PWM
   updateBuzzer();
   
   // Update ramp state
   updateRamp();
   
   // Update button state
   updateButton();
   
   // Update ping feature (button-triggered ramp)
   updatePingFeature();
   
   // Update battery level periodically (every 30 seconds when idle, skip during active operations)
   static unsigned long lastBatteryUpdate = 0;
   if (!buzzerActive && !rampActive && pingState == PING_IDLE) {
     if (millis() - lastBatteryUpdate > 30000) {  // Every 30 seconds when idle
       lastBatteryUpdate = millis();
       updateBatteryLevel();
     }
   }
   
   // Add small idle delay when nothing is active to reduce CPU power consumption
   // Button interrupt will still work during this delay
   if (!buzzerActive && !rampActive && pingState == PING_IDLE && !bleuart.available()) {
     static unsigned long lastIdleCheck = 0;
     if (millis() - lastIdleCheck > 10) {  // Check every 10ms when idle
       lastIdleCheck = millis();
       // Small delay to reduce CPU usage, but check button interrupt flag first
       if (!buttonInterruptFlag) {
         delay(1);  // 1ms delay - button interrupt will still fire
       }
     }
   }
 }
 
// Read button state (INPUT_PULLUP: GPIO->button->GND, LOW when pressed)
// Uses interrupt flag to ensure button is never missed, even during delays
// Detects press (HIGH->LOW) and release (LOW->HIGH) transitions
void updateButton() {
  // Save previous state before updating
  wasButtonPressed = isButtonPressed;
  
  // Check interrupt flag first - this ensures we catch button presses even during delays
  if (buttonInterruptFlag) {
    buttonInterruptFlag = false;  // Clear flag
    // Read actual pin state to confirm (debouncing)
    isButtonPressed = (digitalRead(BUTTON_PIN) == LOW);
  } else {
    // Normal polling when no interrupt
    isButtonPressed = (digitalRead(BUTTON_PIN) == LOW);
  }
  
  // Detect button release (transition from LOW to HIGH)
  if (wasButtonPressed && !isButtonPressed) {
    buttonReleased = true;
    #if ENABLE_SERIAL_DEBUG
    Serial.println("Button released");
    #endif
  }
}
 
// Update ping feature: button-triggered ramp
void updatePingFeature() {
  unsigned long currentMillis = millis();
  
  switch (pingState) {
    case PING_IDLE:
      // Wait for button press (only if button was previously released and device is connected)
      if (buttonReleased && isButtonPressed && !wasButtonPressed && Bluefruit.connected()) {
        // Button just pressed (transition from HIGH to LOW) and device is connected
        pingStartTime = currentMillis;
        pingState = PING_WAITING;
        buttonReleased = false;  // Reset release flag
        #if ENABLE_SERIAL_DEBUG
        Serial.println("Ping: Button pressed, waiting 150ms");
        #endif
      }
      break;
      
    case PING_WAITING:
      // Check if still connected, if not cancel ping
      if (!Bluefruit.connected()) {
        pingState = PING_IDLE;
        buttonReleased = true;  // Allow button to work again
        #if ENABLE_SERIAL_DEBUG
        Serial.println("Ping: Cancelled - device disconnected");
        #endif
        break;
      }
      
      // Wait 150ms after button press
      if (currentMillis - pingStartTime >= PING_WAIT_TIME) {
        // Start ramp (only if still connected)
        if (Bluefruit.connected()) {
          pingState = PING_RAMPING;
          pingStartTime = currentMillis;
          
          // Start ramp: 250ms to 60% power
          rampActive = true;
          rampStartTime = currentMillis;
          rampDuration = PING_RAMP_TIME;
          rampMaxPower = PING_RAMP_POWER;
          
          #if ENABLE_SERIAL_DEBUG
          Serial.println("Ping: Starting ramp (250ms to 60% power)");
          #endif
        } else {
          // Disconnected during wait, cancel
          pingState = PING_IDLE;
          buttonReleased = true;
        }
      }
      break;
      
    case PING_RAMPING: {
      // Check if still connected, if not cancel ping and stop buzzer
      if (!Bluefruit.connected()) {
        pingState = PING_IDLE;
        rampActive = false;
        buzzerActive = false;
        digitalWrite(BUZZER_PIN, LOW);
        pwmState = false;
        buttonReleased = true;
        #if ENABLE_SERIAL_DEBUG
        Serial.println("Ping: Cancelled - device disconnected during ramp");
        #endif
        break;
      }
      
      // Check if ramp is complete
      unsigned long rampElapsed = currentMillis - rampStartTime;
      if (rampElapsed >= rampDuration) {
        // Ramp complete, send ping notification and reset
        pingState = PING_IDLE;
        rampActive = false;
        buzzerActive = false;
        digitalWrite(BUZZER_PIN, LOW);
        pwmState = false;
        
        // Send BLE notification (should still be connected, but check anyway)
        if (Bluefruit.connected()) {
          bleuart.println("ping sent!");
        }
        #if ENABLE_SERIAL_DEBUG
        Serial.println("Ping: Complete! Notification sent.");
        #endif
        // Note: buttonReleased will be set to true when button is actually released
      }
      break;
    }
  }
}
 
 