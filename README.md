# Weather Dashboard with LVGL on ESP32

A real-time weather monitoring dashboard for the WT32-SC01 ESP32 touchscreen display, featuring BMP280 sensor integration and an elegant pastel-themed UI built with LVGL v8.3.

## Features

- **Real-time Sensor Monitoring**: Temperature and pressure readings from BMP280 sensor (I2C)
- **Interactive Weather Dashboard**: Three-card layout displaying temperature, humidity, and air pressure
- **Custom Drawn Icons**: Thermometer and pressure gauge icons rendered with LVGL canvas
- **Temperature Unit Toggle**: Switch between Celsius and Fahrenheit with a button press
- **Brightness Control**: Three-level brightness adjustment (10%, 50%, 95%)
- **Pastel Color Scheme**: Visually appealing coordinated color palette
- **Touchscreen Support**: FT6336 capacitive touch controller
- **Thread-safe I2C**: Mutex-based coordination between touch controller and sensor

## Hardware Requirements

### Main Board
- **WT32-SC01** - ESP32-based development board with integrated display
  - Display: 3.5" TFT LCD, 480x320 resolution
  - Controller: ST7796 (SPI interface)
  - Touch: FT6336 capacitive touch controller (I2C)
  - MCU: ESP32-WROOM-32 (dual-core, WiFi, Bluetooth)

### Sensor
- **BMP280** - Barometric pressure and temperature sensor
  - Interface: I2C (shared bus with touch controller)
  - I2C Address: 0x76 (primary) or 0x77 (secondary, configurable via SDO pin)
  - Power: 3.3V

## Pin Connections

### I2C Bus (Shared between Touch Controller and BMP280)
```
ESP32 GPIO 18 (SDA) ──┬── FT6336 SDA (Touch)
                      └── BMP280 SDA (Sensor)

ESP32 GPIO 19 (SCL) ──┬── FT6336 SCL (Touch)
                      └── BMP280 SCL (Sensor)
```

### BMP280 Wiring
| BMP280 Pin | ESP32 Pin | Description |
|------------|-----------|-------------|
| VCC        | 3.3V      | Power supply |
| GND        | GND       | Ground |
| SDA        | GPIO 18   | I2C data (shared) |
| SCL        | GPIO 19   | I2C clock (shared) |
| SDO        | GND or 3.3V | Address select: GND=0x76, 3.3V=0x77 |

**Note**: The WT32-SC01 board already has the touch controller connected to GPIO 18/19, so simply connect the BMP280 sensor to the same I2C bus.

## Software Architecture

### Components
1. **main** - Main application with LVGL dashboard and sensor integration
2. **BMP280** - Custom driver component for BMP280 sensor with I2C mutex support
3. **LovyanGFX** - Hardware abstraction library for display management
4. **lvgl** - Light and Versatile Graphics Library (v8.3)
5. **bsp_wt32_sc01** - Board support package for WT32-SC01

### Key Features Implementation

#### I2C Bus Coordination
- FreeRTOS mutex prevents conflicts between touch reads and sensor operations
- Touch controller: 50ms mutex timeout
- BMP280 sensor: 200ms mutex timeout for reliable readings
- Entire sensor initialization sequence protected by mutex

#### Temperature Conversion
- Internal storage: Always in Celsius
- Display: Toggles between °C and °F
- Formula: °F = (°C × 9/5) + 32

#### Sensor Reading Task
- FreeRTOS task running at priority 3
- 2-second polling interval
- Automatic retry with fallback to secondary I2C address
- Updates LVGL labels with live data

## Project Structure

```
Weather-lvgl/
├── CMakeLists.txt                 # Top-level CMake configuration
├── README.md                      # This file
├── components/
│   ├── BMP280/                    # BMP280 sensor driver component
│   │   ├── BMP280.c               # Driver implementation with mutex support
│   │   ├── BMP280.h               # Driver API and definitions
│   │   └── CMakeLists.txt         # Component build configuration
│   ├── LovyanGFX/                 # Display hardware abstraction
│   ├── lvgl/                      # LVGL graphics library v8.3
│   └── bsp_wt32_sc01/             # WT32-SC01 board support package
├── main/
│   ├── main.cpp                   # Main application and dashboard UI
│   └── CMakeLists.txt             # Main component configuration
└── sdkconfig                      # ESP-IDF configuration
```

## Build and Flash Instructions

### Prerequisites
- ESP-IDF v4.4 or later
- Python 3.7+
- USB driver for ESP32 (CP210x or similar)

### Setup ESP-IDF Environment
```bash
# Linux/macOS
. $HOME/esp/esp-idf/export.sh

# Windows (Command Prompt)
%userprofile%\esp\esp-idf\export.bat

# Windows (PowerShell)
$env:IDF_PATH\export.ps1
```

### Build Project
```bash
cd Weather-lvgl
idf.py build
```

### Flash to Device
```bash
# Auto-detect port and flash
idf.py -p PORT flash

# Example (Windows)
idf.py -p COM3 flash

# Example (Linux)
idf.py -p /dev/ttyUSB0 flash
```

### Monitor Serial Output
```bash
idf.py -p PORT monitor

# Or combine flash and monitor
idf.py -p PORT flash monitor
```

### Full Clean and Rebuild
```bash
idf.py fullclean
idf.py build
```

## Usage

### Dashboard Layout
The dashboard displays three cards in a horizontal grid:

1. **Temperature Card** (Pastel Peach)
   - Custom thermometer icon
   - Live temperature reading from BMP280
   - Updates every 2 seconds

2. **Humidity Card** (Pastel Teal)
   - Droplet icon
   - Displays 65% (dummy value - BMP280 doesn't measure humidity)
   - For real humidity, consider upgrading to BME280 sensor

3. **Pressure Card** (Pastel Purple)
   - Custom pressure gauge icon
   - Live pressure reading in hPa
   - Updates every 2 seconds

### Control Buttons

#### Temperature Mode Button (Left, Bottom)
- **Label**: "Temp Mode: C" or "Temp Mode: F"
- **Color**: Pastel orange (#FFAB91)
- **Function**: Toggles between Celsius and Fahrenheit
- **Position**: Bottom left (90, 235)

#### Brightness Button (Right, Bottom)
- **Label**: "Brightness: X%"
- **Color**: Pastel yellow (#FFD54F)
- **Function**: Cycles through 10% → 50% → 95% → 10%
- **Position**: Bottom right (250, 235)

### Touch Coordinates Display
Top-right corner shows current touch coordinates for debugging purposes.

## Technical Details

### LVGL Configuration
- Version: 8.3
- Color depth: 16-bit (RGB565)
- Buffer: Double buffering for smooth rendering
- Font: Montserrat (12pt, 14pt, 22pt, 28pt)

### BMP280 Configuration
- Operating mode: Normal (continuous measurement)
- Temperature oversampling: 16x
- Pressure oversampling: 16x
- IIR filter: Coefficient 16
- Standby time: 500ms

### I2C Configuration
- Port: I2C_NUM_0
- Frequency: 400 kHz (Fast Mode)
- Pull-ups: Enabled (internal)
- SDA: GPIO 18
- SCL: GPIO 19

### FreeRTOS Tasks
1. **Main Task**: LVGL timer handler (continuous)
2. **Sensor Task**: BMP280 reading (2s interval, priority 3)
3. **LVGL Tick Task**: Periodic timer for LVGL (1ms)

## Troubleshooting

### BMP280 Not Detected
1. Check I2C connections (SDA=GPIO18, SCL=GPIO19)
2. Verify power supply (3.3V to BMP280)
3. Check I2C address (SDO pin determines 0x76 or 0x77)
4. Monitor serial output for initialization errors

### Touch Not Working
- Ensure I2C mutex is not being held too long by sensor
- Check if touch controller is properly initialized
- Verify LovyanGFX configuration matches hardware

### Display Issues
- Confirm display orientation is set correctly (landscape mode)
- Check SPI connections between ESP32 and ST7796
- Verify backlight control is working

### Build Errors
```bash
# Clean and rebuild
idf.py fullclean
idf.py build

# Check ESP-IDF version
idf.py --version
```

## Future Enhancements

- WiFi connectivity for weather API integration
- BME280 sensor for real humidity readings
- Altitude calculation from pressure
- Historical data graphs
- Weather forecast display
- NTP time synchronization
- SD card logging

## Dependencies

- **ESP-IDF**: v4.4+
- **LVGL**: v8.3 (included in components)
- **LovyanGFX**: Latest (included in components)
- **FreeRTOS**: Part of ESP-IDF
- **Board Support**: WT32-SC01 BSP (included in components)

## License

This project is provided as-is for educational and development purposes.

## Credits

- **LVGL**: https://lvgl.io/
- **LovyanGFX**: https://github.com/lovyan03/LovyanGFX
- **ESP-IDF**: https://github.com/espressif/esp-idf
- **BMP280 Datasheet**: Bosch Sensortec

## Author

Developed for ESP32 Weather Dashboard demonstration with WT32-SC01 touchscreen display.
