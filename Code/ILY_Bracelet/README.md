# Ping Bracelet - Project Documentation

## Overview

The **Ping Bracelet** is a wearable Bluetooth Low Energy (BLE) device that provides haptic feedback through a buzzer motor. The device can be controlled remotely via BLE commands and features a touch sensor for local interaction. The name "Ping" refers to the device's ability to send notification-like haptic signals to the wearer.

## Hardware Platform

- **Board**: Seeed Studio XIAO ESP32-C6
- **Chip**: ESP32-C6 (RISC-V based, dual-core)
- **Bluetooth**: Bluetooth 5.0 LE (Low Energy)
- **Power**: 3.7V LiPo battery

## Pin Connections

| Pin | Function | Description |
|-----|----------|-------------|
| **D6** | Buzzer Control | PWM output to transistor base |
| **D0 (A0)** | Touch Sensor | Analog input for touch detection |
| **A1 (D1)** | Battery Monitoring | Analog input for battery voltage |
| **BAT+** | Battery Power | Positive terminal of 3.7V LiPo battery |
| **GND** | Ground | Common ground reference |

## Hardware Circuit

### Buzzer Circuit
The buzzer is controlled via an NPN transistor connected to D6. The circuit includes a flyback diode for protection.

### Battery Monitoring
Battery voltage is monitored through a voltage divider circuit connected to A1 (D1).

### Touch Sensor
Touch sensor is connected directly to D0 (A0) and uses analog reading for detection.

## Bluetooth Low Energy (BLE) Configuration

### Device Information
- **Device Name**: "Ping Bracelet"
- **Visibility**: Configured to appear in iPhone Bluetooth device list
- **Advertising**: Continuous advertising when not connected

### BLE Services

#### 1. UART Service (Nordic UART Service)
- **UUID**: `6E400001-B5A3-F393-E0A9-E50E24DCCA9E`
- **Purpose**: Bidirectional communication for commands and responses
- **RX Characteristic** (`6E400002-B5A3-F393-E0A9-E50E24DCCA9E`): Write-only, receives commands
- **TX Characteristic** (`6E400003-B5A3-F393-E0A9-E50E24DCCA9E`): Notify-only, sends responses

#### 2. Battery Service
- **UUID**: `180F` (Standard BLE Battery Service)
- **Purpose**: Reports battery level percentage
- **Battery Level** (`2A19`): Read and Notify, sends 0-100% battery level

#### 3. Device Information Service
- **UUID**: `180A` (Standard BLE Device Information Service)
- **Purpose**: Helps with iPhone visibility and device identification
- **Manufacturer Name** (`2A29`): Read-only, value "Ping Bracelet"

## BLE Commands

Commands are sent via the UART RX characteristic and responses come via the TX characteristic.

### Available Commands

#### `buzz <ms> <power>`
Triggers the buzzer for a specified duration at a given power level.

- **Parameters**:
  - `<ms>`: Duration in milliseconds (1-10000)
  - `<power>`: Power level from 0.0 to 1.0 (0.0 = off, 1.0 = full power)
- **Example**: `buzz 500 0.5` - Buzz for 500ms at 50% power

#### `ramp <ms> <maxpower>`
Ramps the buzzer power from 0% to maximum power over a specified duration.

- **Parameters**:
  - `<ms>`: Ramp duration in milliseconds (1-10000)
  - `<maxpower>`: Maximum power level (0.0-1.0)
- **Example**: `ramp 1000 0.8` - Ramp to 80% power over 1 second

#### `battery` or `bat`
Reads and reports the current battery voltage.

#### `recalibrate` or `calibrate`
Recalibrates the touch sensor baseline. Do not touch the sensor during calibration.

## Touch-Based Ping Feature

The device includes an automatic "ping" feature triggered by the touch sensor.

### How It Works

1. **Touch Detection**: User touches the sensor pad
2. **Hold Period**: User must hold touch for 300ms
3. **Ramp Up**: Buzzer ramps to 60% power over 800ms
4. **Wait Period**: 400ms pause after ramp completes
5. **Final Buzz**: Short 50ms buzz at 100% power
6. **Notification**: Sends "ping sent!" message via BLE

The touch sensor automatically calibrates on startup. If touch is released during the ramp phase, the ping is cancelled.

## Software Architecture

The main loop continuously manages:
- BLE connection and communication
- Buzzer PWM control and timing
- Power ramping functionality
- Touch-based ping state machine
- Touch sensor reading and processing
- Battery level monitoring

## Development Environment

- **Board**: XIAO_ESP32C6
- **Libraries**: ESP32 BLE Arduino Library (included with ESP32 board package)
- **Serial Monitor**: 115200 baud

## Usage Workflow

1. **Power On**: Connect 3.7V LiPo battery to BAT+ and GND
2. **Calibration**: Device automatically calibrates touch sensor on startup
3. **BLE Advertising**: Device starts advertising as "Ping Bracelet"
4. **Connect**: Use BLE app (nRF Connect, LightBlue, or iPhone Bluetooth settings)
5. **Send Commands**: Write commands to RX characteristic
6. **Receive Responses**: Read notifications from TX characteristic
7. **Touch Interaction**: Hold touch sensor for 300ms+ to trigger ping feature

## Project Structure

```
ping_bracelet_esp32c6/
├── ping_bracelet_esp32c6.ino  # Main firmware file
└── README.md                   # This documentation
```

---