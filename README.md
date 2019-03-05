# ESP32_AccessControl
RFID and WiFi Access Control system based on ESP32.
ESP32 acts as a WiFi AP and all configuration is done via the WEB based interface.

### Capabilities
- Access control using RFID 13.56 Mhz
- WiFI Access Point WPA2 security
- Access control using WiFi probes (access control by receiving WiFi prob messages from WiFI clients)
- Access control using WiFi connect/attach from authorised clients
- Date/time clock with backup battery and synchronization from the WiFi clients (via WEB interface). Possibility of date/time based access control (future planned feature.)
- Logging of the access events
- Adaptive (small & big screens) WEB interface for configuration and access management
- Inverted Relay mode operation (no restriction of access if device is not powered)
- Operation Mode leds & heartbeat led 
- OTA firmware update
- Reset switch
- 220V AC input power

## Build with
- [Arduino 1.8.7](https://www.arduino.cc/)  
- [Arduino core for the ESP32](https://github.com/espressif/arduino-esp32)

#### Libraries
- DS1302 library from Krodal (arduino.cc)  
- [LinkedList](https://github.com/ivanseidel/LinkedList) library v1.2.3 by Ivan Seidel  
- [MFRC522](https://github.com/miguelbalboa/rfid) library v1.4.3 by GithubCommunity

### Hardware
The corresponding schematics and PCB can be found at [EasyEDA project](https://easyeda.com/wonderer643/esp32_access)

### License
This project is licensed under the GNU General Public License v3.0 - see details in [License](LICENSE)
