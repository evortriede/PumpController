#PumpController
London Water Co-op Pump Controller

![Pump Controller](/assets/PumpController.jpg)

In the simplest of terms, the Pump Controller monitors the output of the Hach CL17 chlorine meter and adjusts the chlorine pump to keep the chlorine level within configured values. The CL17 speaks 4-20mA Current Loop and the pump speaks 0-5V. The Pump Controller uses an ESP32 development board, a current to voltage converter and a digital to analog converter (DAC) to perform this task.

The circuit board was designed with Tiny CAD and Vee CAD. The CAD files are in the /hardware directory.

The firmware joins the WTP WiFi network and acts as a web server for configuration, monitoring and control. The WiFi connection is also used to communicate with the Current Monitor. The Pump Controller sends chlorine readings from the CL17 and pump settings to the Current Monitor which sends them to the Current Recorder and LWC Monitor instances via LoRa. The Current Monitor also sends pump setting commands to the Pump Controller which it receives over LoRa.

## Main Page


![Pump Controller Main Page](/assets/PumpControllerMainPage.png)
Pump Controller Main Page while WTP is running and controller is in automatic mode


![Pump Controller Manual Mode](/assets/PumpControllerManualMode.png)
Pump Controller Main Page in manual mode


By default, and assuming that the Pump Controller is powered on and has joined the local WiFi network, the main page can be displayed by navigating to http://PumpController.local on the WTP computer's browser. 

The meter across the top of the display gives a visual representation of the chlorine concentration as measured by the CL17. If the chlorine concentration is within the "sweet spot" (see Configuration below) the color will be green. If it is above the sweet spot, it will be red and if below it will be yellow.

The calculated chlorine concentration in parts per million (ppm) is displayed inside the meter. This value is calculated by dividing the raw CL17 reading (displayed below the meter) by the configured conversion factor (see Configuration below). The conversion factor should be adjusted to make the calculated number and the CL17 display agree with each other.

The Raw CL17 Reading is the value measured by the ESP32 from the 4-20mA to Voltage converter. The CL17 is a current source and is connected to the converter. The voltage output of the converter is connected to a ADC pin on the ESP32. This value divided by the configured conversion factor is what gives the ppm value that is displayed in the meter. 

The Pump Setting shows the current setting of the pump. The value is unitless and represents the raw value given to either the ESP32 DAC or the MCP4725 DAC, depending on how the firmware was compiled (see Building Firmware below). The maximum value is 255 when using the ESP32 DAC or 4095 when using the MCP4725. The output from either DAC is given to the Kamoer DIPump550 pump which expects 0-5 volts to affect between 0 and 400 RPM. The ESP32 DAC can output a maximum of ~3 volts whereas the MCP4725 can output up to ~4.5 volts. Since the highest speed that we ever expect to need to run the pump is ~50 RPM, either method is capable; however, the MCP4725 has a larger range and is more precise. One may enter a number and click the Change button to affect a new value. 

The Automatic button may be clicked to start the automatic adjustment of the pump. When clicked the text in the button will change to "Manual" and clicking again will stop automatic adjustment and change the text back to "Automatic". When in automatic mode, pump adjustments are made by comparing the calculated ppm to the sweet spot. If the ppm value is within the configured tolerance of the midpoint between the configured low and high sweet spot values, no change will take place. If the ppm value is below that range, the pump speed will be increased. If it is above the range, it will be decreased. In automatic mode, manual pump changes can still be made and the adjustments will be made to the manually entered value. The interval between adjustments is controlled by the Adjust Frequency configuration field (see below). Pump changes take some time to reach the CL17 and the CL17 takes 1.75 minutes between readings, so the minimum effective interval is probably about five minutes in order to give the CL17 time to stabilize at the new chlorine concentration.

The Config link brings up the configuration page. 

## Configuration

The Pump Controller is configured via a webpage that is reached by clicking the Config link on the main page:

![Pump Controller Config](/assets/PumpControllerConfig.png)

The webpage can be accessed through the local WiFi network if the Pump Controller has joined the network. If the Pump Controller has joined the local WiFi network, the DNS name for the Pump Controller will be its Captive Net SSID with ".local" appended. (For example: PumpController.local.) If the Pump Controller is not connected to the local WiFi network, one can join the Pump Controller's Captive Net SSID, and then setting a browser to http://192.168.4.1/.

The configuration fields are as follows:

- IP for Current Monitor: This is the IP address for the Current Monitor
- Max Pump Value: The maximum value that the pump output will be set to. When using the ESP32 built in DAC it should be set no higher than 255 and when using the MCP4725 it should be set no higher than 4095. 
- Max Chlorine Value: This value should be set to the "Raw CL17 Reading" value when the CL17 is set to maximum recorder output in maintenance mode. This calibrates how the meter will be shown on the main webpage.
- Conversion factor: This is the factor by which the raw CL17 reading is divided to get parts per million (ppm). It is determined by observing the PPM display on the CL17 and the raw reading on the main page.
- Update Frequency (sec): How often in seconds the display is updated.
- Sweet Spot LOW (ppm): The midpoint between Sweet Spot Low and Sweet Spot High is the target that the Pump Controller will aim for when performing automatic adjustments.
- Sweet Spot HIGH (ppm): See Sweet Spot Low, above.
- Tolerance (ppm): The number of ppm above or below the sweet spot the chlorine reading can be without the pump being adjusted. For example: if low is .3 and high is .6 the sweet spot is .45. If tolerance is .02 the pump will not adjust for readings between .43 and .47. Readings at or below .42 will adjust up and readings above .48 will adjust down. 
- Initial Pump Setting: The initial pump setting when automatic adjustment is enabled.
- Adjust Frequency (minutes): The number of minutes between pump adjustments. Note that the CL17 requires 1.75 minutes per reading.
- SSID to join: The SSID for the local WiFi network. If left blank a preconfigured list of SSIDs will be used.
- Password for SSID to join: Password for local WiFi SSID
- SSID for Captive Net: SSID name for the captive portal (used for initial configuration e.g., to set the SSID if it is not in the pre-configured list.) If mDNS is available, <this name>.local can be used to access the web pages through the local WiFi network.
- Password for Captive Net SSID: Self explanatory.

## Calibration

There are two configuration settings that need to be calibrated for the Pump Controller to work properly: Max Chlorine Value and Conversion Factor.

### Max Chlorine Calibration

To calibrate the Max Chlorine value, which is the value that the ESP32 will read from the CL17 when it is sending 20mA to the current to voltage converter. Set the CL17 to maximum recorder output as follows:

1. Open the  cover of the CL17 so the keypad is accessible
2. Press the MENU key
3. Press the UP ARROW key until SETUP is displayed
4. Press the ENTER key
5. Press the DOWN ARROW key repeatedly until the RECMAX option is displayed
6. Press the ENTER key

The CL17 will output 20mA until its next analysis is complete. While in this state, the Raw CL17 Reading on the Pump Controller main webpage will have the maximum value that should ever be read from the CL17. Enter this value in the Max Chlorine Value field on the configuration webpage.

### Conversion Factor Calibration

It is best to calibrate Conversion Factor during a WTP run when the CL17 reading is within .10 ppm of the sweet spot. Set the Conversion Factor to the Raw CL17 Reading value divided by the reading displayed by the CL17. For example: if the Raw CL17 Reading is 800 and the CL17 display is 0.50, set the Conversion Factor to 1600.0 (800/.5=1600).

## Construction

![Pump Controller Open](/assets/PumpControllerOpen.jpg)

While it is unlikely that LWC will ever have to build another copy of the Pump Controller, maybe another small water system with a CL17 may want to replace their existing chlorine pump with a Kamoer DIPump550 (or similar) and control the pump with this. So, here's what one would need to know.

The /hardware directory contains a bill of materials. The enclosure that LWC used was a repurposed plastic box that a couple of Arduino microcontrollers had been shipped in. Use whatever is on hand or use a disposable food storage container. The enclosure is not important. The circuit is built on a strip board. The LWC version is 19x33 to fit in our enclosure but any dimensions that accommodate the components will work. VeeCAD was used to compose the strip board art. Both a jpg of the layout and the VeeCAD file  are in the /hardware directory along with the TinyCAD schematic should any modifications be desired.

![VeeCad for Pump Controller](/hardware/PumpControllerBoardFront.jpg)

If any of the foregoing is not perfectly obvious, seek assistance from an IoT geek.

## Building the Firmware

There are instructions for setting up the Arduino IDE in the london-water-co-op repository on GitHub. Once the IDE is set up, the firmware can be built by setting the board to the ESP32 Dev Board.

Again, if any of the foregoing is not perfectly obvious, seek assistance from an IoT geek.

