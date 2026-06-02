# BLE Communication Guide for React Native App

This guide explains how your React Native app can communicate with the Ping Bracelet device. The bracelet uses Bluetooth Low Energy (BLE) to send and receive data.

## Table of Contents

1. [Understanding BLE Basics](#understanding-ble-basics)
2. [Device Information](#device-information)
3. [Connecting to the Bracelet](#connecting-to-the-bracelet)
4. [Communication Protocol](#communication-protocol)
5. [Sending Commands to the Bracelet](#sending-commands-to-the-bracelet)
6. [Receiving Data from the Bracelet](#receiving-data-from-the-bracelet)
7. [Complete Example Flow](#complete-example-flow)
8. [React Native Libraries](#react-native-libraries)

---

## Understanding BLE Basics

Think of BLE like a walkie-talkie system:

- **Central Device**: Your phone (the app) - it initiates connections
- **Peripheral Device**: The Ping Bracelet - it advertises and waits for connections
- **Service**: Like a channel on a walkie-talkie - defines what the device can do
- **Characteristic**: Like a specific function on that channel - the actual data you send/receive

The Ping Bracelet uses:
- **UART Service**: For sending commands and receiving responses (like text messages)
- **Battery Service**: For reading battery level (standard BLE service)

---

## Device Information

### Device Name
The bracelet advertises as: **"Ping Bracelet"**

### Services and Characteristics

#### 1. UART Service (Nordic UART Service - NUS)
- **Service UUID**: `6E400001-B5A3-F393-E0A9-E50E24DCCA9E`
- **TX Characteristic** (Device → App): `6E400003-B5A3-F393-E0A9-E50E24DCCA9E`
  - Used to receive data FROM the bracelet
  - Subscribe to notifications on this characteristic
- **RX Characteristic** (App → Device): `6E400002-B5A3-F393-E0A9-E50E24DCCA9E`
  - Used to send commands TO the bracelet
  - Write data to this characteristic

#### 2. Battery Service (Standard BLE)
- **Service UUID**: `180F` (standard BLE battery service)
- **Battery Level Characteristic**: `2A19`
  - Read this to get battery percentage (0-100)
  - Can also subscribe to notifications for automatic updates

---

## Connecting to the Bracelet

### Step 1: Scan for Devices

Scan for BLE devices and look for a device with the name "Ping Bracelet".

```javascript
// Pseudo-code example
const devices = await BLE.scanForDevices();
const bracelet = devices.find(device => device.name === "Ping Bracelet");
```

### Step 2: Connect

Connect to the bracelet device.

```javascript
await BLE.connect(bracelet.id);
```

### Step 3: Discover Services

After connecting, discover the available services.

```javascript
const services = await BLE.discoverServices(bracelet.id);
const uartService = services.find(s => s.uuid === "6E400001-B5A3-F393-E0A9-E50E24DCCA9E");
const batteryService = services.find(s => s.uuid === "180F");
```

### Step 4: Get Characteristics

Get the characteristics you need to communicate.

```javascript
// UART characteristics
const txCharacteristic = await BLE.getCharacteristic(
  uartService.uuid, 
  "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"  // TX (receive from device)
);
const rxCharacteristic = await BLE.getCharacteristic(
  uartService.uuid, 
  "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"  // RX (send to device)
);

// Battery characteristic
const batteryCharacteristic = await BLE.getCharacteristic(
  batteryService.uuid,
  "2A19"
);
```

### Step 5: Subscribe to Notifications

Subscribe to the TX characteristic to receive data from the bracelet.

```javascript
await BLE.subscribeToNotifications(txCharacteristic, (data) => {
  // Handle incoming data from bracelet
  handleBraceletMessage(data);
});
```

---

## Communication Protocol

All communication is done via **text strings** sent over the UART service. Think of it like sending text messages back and forth.

### Message Format

- Messages are sent as **UTF-8 strings**
- Each message should end with a **newline character** (`\n`)
- Messages are case-insensitive (the bracelet converts to lowercase)

---

## Sending Commands to the Bracelet

Send commands by writing text strings to the RX characteristic.

### Command 1: Buzz

Make the buzzer vibrate at a specific power for a specific duration.

**Format**: `buzz <milliseconds> <power>`

**Parameters**:
- `milliseconds`: Duration in milliseconds (1-5000)
- `power`: Power level as a float (0.0-1.0, where 0.0 is off and 1.0 is maximum)

**Example**:
```
buzz 500 0.8
```
This makes the buzzer vibrate at 80% power for 500 milliseconds.

**More Examples**:
- `buzz 200 0.5` - 200ms at 50% power
- `buzz 1000 1.0` - 1 second at maximum power
- `buzz 100 0.1` - 100ms at 10% power (gentle vibration)

**Response**: The bracelet will send `OK` when complete, or `ERROR: Invalid parameters` if something is wrong.

### Command 2: Riser

Create a rising vibration effect that gradually increases from 0 to max power.

**Format**: `riser <milliseconds> <maxpower>`

**Parameters**:
- `milliseconds`: Duration of the rise effect (1-5000)
- `maxpower`: Maximum power level at the end as a float (0.0-1.0, where 0.0 is off and 1.0 is maximum)

**Example**:
```
riser 1000 1.0
```
This creates a 1-second rising effect that goes from 0 to maximum power.

**More Examples**:
- `riser 500 0.5` - 500ms rising to 50% power
- `riser 2000 0.8` - 2 seconds rising to 80% power
- `riser 300 0.3` - 300ms rising to 30% power (gentle rise)

**Response**: The bracelet will send `OK` when complete, or `ERROR: Invalid parameters` if something is wrong.

### Command 3: Battery

Request the current battery level.

**Format**: `battery`

**Example**:
```
battery
```

**Response**: The bracelet will send `BATTERY:85` (where 85 is the percentage).

---

## Receiving Data from the Bracelet

The bracelet sends messages to your app via the TX characteristic. You should subscribe to notifications to receive these messages.

### Message Types

#### 1. Connection Messages

When the app first connects, the bracelet sends:
```
Ping Bracelet connected!
Commands: buzz <ms> <power>, riser <ms> <maxpower>, battery
Power values: 0.0 (off) to 1.0 (max)
```

#### 2. Touch Input Notification

When the user touches the capacitive sensor for the required duration:
```
TOUCH_INPUT
```

**When this happens**: The bracelet detects a touch held for >0.5 seconds, runs a riser effect, then sends this message.

**What to do**: Update your app UI to show that a touch was received, or trigger any action you want.

#### 3. Battery Status

When you request battery level:
```
BATTERY:85
```

The number after the colon is the battery percentage (0-100).

#### 4. Low Battery Warning

When battery drops below 10%:
```
LOW_BATTERY:9
```

**Important**: This is sent automatically by the bracelet. You should:
- Show a warning to the user
- Maybe reduce app functionality
- Prompt user to charge the bracelet

#### 5. Command Responses

After sending a command:
- `OK` - Command executed successfully
- `ERROR: <message>` - Command failed with error message

---

## Complete Example Flow

Here's a complete example of how your app might interact with the bracelet:

### 1. App Startup

```javascript
// Scan for bracelet
const bracelet = await scanForBracelet();

// Connect
await connectToBracelet(bracelet);

// Subscribe to messages
subscribeToMessages((message) => {
  if (message === "TOUCH_INPUT") {
    showNotification("Touch detected!");
  } else if (message.startsWith("BATTERY:")) {
    const level = parseInt(message.split(":")[1]);
    updateBatteryUI(level);
  } else if (message.startsWith("LOW_BATTERY:")) {
    const level = parseInt(message.split(":")[1]);
    showLowBatteryWarning(level);
  }
});
```

### 2. User Wants to Test Buzzer

```javascript
// Send buzz command (500ms at 80% power)
await sendCommand("buzz 500 0.8\n");

// Wait for "OK" response
// Update UI to show command was sent
```

### 3. User Wants to Check Battery

```javascript
// Request battery level
await sendCommand("battery\n");

// Wait for "BATTERY:85" response
// Parse and display in UI
```

### 4. Bracelet Detects Touch

```javascript
// Automatically receives "TOUCH_INPUT" message
// Show notification or trigger action in app
```

---

## React Native Libraries

You'll need a BLE library for React Native. Here are the recommended options:

### Option 1: react-native-ble-plx (Recommended)

**Installation**:
```bash
npm install react-native-ble-plx
# or
yarn add react-native-ble-plx
```

**iOS Setup**: Add to `Info.plist`:
```xml
<key>NSBluetoothAlwaysUsageDescription</key>
<string>This app needs Bluetooth to communicate with Ping Bracelet</string>
<key>NSBluetoothPeripheralUsageDescription</key>
<string>This app needs Bluetooth to communicate with Ping Bracelet</string>
```

**Android Setup**: Add to `AndroidManifest.xml`:
```xml
<uses-permission android:name="android.permission.BLUETOOTH" />
<uses-permission android:name="android.permission.BLUETOOTH_ADMIN" />
<uses-permission android:name="android.permission.BLUETOOTH_SCAN" />
<uses-permission android:name="android.permission.BLUETOOTH_CONNECT" />
```

### Option 2: react-native-ble-manager

Alternative library with similar functionality.

---

## Example React Native Code Structure

Here's a basic structure for your BLE communication module:

```javascript
import { BleManager } from 'react-native-ble-plx';

class BraceletManager {
  constructor() {
    this.manager = new BleManager();
    this.device = null;
    this.txCharacteristic = null;
    this.rxCharacteristic = null;
  }

  async scanAndConnect() {
    // Scan for devices
    this.manager.startDeviceScan(null, null, (error, device) => {
      if (error) return;
      if (device.name === 'Ping Bracelet') {
        this.manager.stopDeviceScan();
        this.connect(device);
      }
    });
  }

  async connect(device) {
    const connectedDevice = await device.connect();
    const services = await connectedDevice.discoverAllServicesAndCharacteristics();
    
    // Find UART service
    const uartService = services.find(s => 
      s.uuid.toLowerCase() === '6e400001-b5a3-f393-e0a9-e50e24dcca9e'
    );
    
    // Get characteristics
    const characteristics = await uartService.characteristics();
    this.txCharacteristic = characteristics.find(c => 
      c.uuid.toLowerCase() === '6e400003-b5a3-f393-e0a9-e50e24dcca9e'
    );
    this.rxCharacteristic = characteristics.find(c => 
      c.uuid.toLowerCase() === '6e400002-b5a3-f393-e0a9-e50e24dcca9e'
    );
    
    // Subscribe to notifications
    this.txCharacteristic.monitor((error, characteristic) => {
      if (error) return;
      const message = characteristic.value; // Base64 encoded
      this.handleMessage(message);
    });
  }

  async sendCommand(command) {
    if (!this.rxCharacteristic) return;
    await this.rxCharacteristic.writeWithResponse(
      Buffer.from(command + '\n').toString('base64')
    );
  }

  handleMessage(base64Message) {
    const message = Buffer.from(base64Message, 'base64').toString('utf-8');
    // Process message
    if (message.includes('TOUCH_INPUT')) {
      // Handle touch input
    } else if (message.includes('BATTERY:')) {
      // Handle battery update
    } else if (message.includes('LOW_BATTERY:')) {
      // Handle low battery warning
    }
  }

  async buzz(duration, power) {
    // Power should be 0.0-1.0 (0.0 = off, 1.0 = max)
    await this.sendCommand(`buzz ${duration} ${power}`);
  }

  async riser(duration, maxPower) {
    // maxPower should be 0.0-1.0 (0.0 = off, 1.0 = max)
    await this.sendCommand(`riser ${duration} ${maxPower}`);
  }

  async getBattery() {
    await this.sendCommand('battery');
  }
}

export default new BraceletManager();
```

---

## Important Notes

1. **Permissions**: Make sure your app requests Bluetooth permissions on both iOS and Android
2. **Connection State**: Monitor connection state and handle disconnections gracefully
3. **Message Parsing**: All messages from the bracelet end with `\n`, so you may need to trim them
4. **Base64 Encoding**: Some libraries require Base64 encoding for data - check your library's documentation
5. **Threading**: BLE operations should be done on background threads to avoid blocking the UI
6. **Error Handling**: Always handle errors - BLE connections can be unreliable

---

## Testing Without the App

You can test the bracelet using the **nRF Connect** app (available on iOS and Android):

1. Download nRF Connect from the App Store/Play Store
2. Scan for "Ping Bracelet"
3. Connect to it
4. Go to the UART service
5. You can send commands like `buzz 500 0.8` (500ms at 80% power) and see responses

This is useful for debugging and verifying the bracelet works before integrating with your app.

---

## Summary

- The bracelet uses BLE UART service for text-based communication
- Send commands: `buzz <ms> <power>`, `riser <ms> <maxpower>`, `battery`
  - Power values: **0.0-1.0** (0.0 = off, 1.0 = maximum)
- Receive messages: `TOUCH_INPUT`, `BATTERY:X`, `LOW_BATTERY:X`, `OK`, `ERROR:...`
- Use `react-native-ble-plx` or similar library
- Subscribe to TX characteristic to receive messages
- Write to RX characteristic to send commands

Good luck with your app development!

