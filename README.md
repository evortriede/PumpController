#London Water Co-op Pump Controller

![Pump Controller](/assets/PumpController.jpg)

In the simplest of terms, the Pump Controller monitors the output of the Hach CL17 chlorine meter and adjusts the chlorine pump to keep the chlorine level within configured values. The CL17 speaks 4-20mA Current Loop and the pump speaks 0-5V. The Pump Controller uses an ESP32 development board, a current to voltage converter and a digital to analog converter (DAC) to perform this task.

The circuit board was designed with Tiny CAD and Vee CAD. The CAD files are in the /hardware directory.

The firmware joins the WTP WiFi network and acts as a web server for configuration, monitoring and control. The WiFi connection is also used to communicate with the Current Monitor. The Pump Controller sends chlorine readings from the CL17 and pump settings to the Current Monitor which sends them to the Current Recorder and LWC Monitor instances via LoRa. 