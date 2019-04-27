/*
 * * ESP8266 template with phone config web page
 * based on BVB_WebConfig_OTA_V7 from Andreas Spiess https://github.com/SensorsIot/Internet-of-Things-with-ESP8266
 *
 */
#include <FS.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPUpdateServer.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <Ticker.h>
#include <EEPROM.h>
#include <DNSServer.h>
#include <NeoPixelBus.h>
#include <NeoPixelBrightnessBus.h>
#include <NeoPixelAnimator.h>
#include <Wire.h>
#include <RtcDS3231.h>
#include <ESP8266SSDP.h>
#include <ESP8266NetBIOS.h>
#include <ESP8266LLMNR.h>

#include "global.h"
#include "list.h"
#include "RTC.h"
#include "NTP.h"
#include "LightSensor.h"
#include "LedStrip.h"

// Include the HTML, STYLE and Script "Pages"

#include "Page_ico.h"
#include "Page_index.h"
#include "Page_ntp.h"
#include "Page_information.h"
#include "Page_general.h"
#include "Page_network.h"
#include "Page_script.js.h"
#include "Page_style.css.h"



extern "C" {
#include "user_interface.h"
}

IPAddress _apIP(192, 168, 1, 1);
IPAddress _apNetMsk(255, 255, 255, 0);


os_timer_t _disableWifiTimer;

void disableWifiCallback(void *pArg)
{
  WiFi.forceSleepBegin();
}



//*** Normal code definition here ...
void setup() {
  String chipID;
  bool CFG_saved = false;
  int WIFI_connected = false;

  // No need to set this until option is set in arduino/visualmicro
  //system_update_cpu_freq(SYS_CPU_160MHZ);

  randomSeed(analogRead(ALS_PIN));

  pinMode(LED_BUILTIN, OUTPUT);     // Initialize the LED_BUILTIN pin as an output

  Serial.begin(115200);
  Serial.println("Booting");

  // Config load 
  EEPROM.begin(512); // define an EEPROM space of 512Bytes to store data
  CFG_saved = ReadConfig();
  if (!CFG_saved)
  {
    // DEFAULT CONFIG
    Serial.println("Set Default config");

    _config.ssid = "";       // SSID of access point
    _config.password = "";   // password of access point
    _config.dhcp = true;
    _config.IP[0] = 192; _config.IP[1] = 168; _config.IP[2] = 1; _config.IP[3] = 100;
    _config.Netmask[0] = 255; _config.Netmask[1] = 255; _config.Netmask[2] = 255; _config.Netmask[3] = 0;
    _config.Gateway[0] = 192; _config.Gateway[1] = 168; _config.Gateway[2] = 1; _config.Gateway[3] = 1;
    _config.DNS[0] = 192; _config.DNS[1] = 168; _config.DNS[2] = 1; _config.DNS[3] = 1;
    _config.DeviceName = "TexTime";

    _config.ntpServerName = "0.ch.pool.ntp.org"; // to be adjusted to PT ntp.ist.utl.pt
    _config.Update_Time_Via_NTP_Every = 86400;
    _config.timeZone = 10;
    _config.isDayLightSaving = true;

    _config.brightnessAuto = true;
    _config.brightness = 128; // [0:255]
    _config.brightnessAutoMinDay = 30; // [0:255]
    _config.brightnessAutoMinNight = 0; // [0:255]
    _config.color[0] = 255; // R
    _config.color[1] = 255; // G
    _config.color[2] = 255; // B
    _config.color[3] = 255; // W
    _config.mode = 1;
    _config.animation = 0;
    _config.ledConfig = 0;
    _config.luxSensitivity = 40;
  }

  // Start led strip
  QTLed.begin(); // Must be called after Serial.begin() and EEPROM configuration

  // Start RTC
  RTC.Begin();
  RTC.Enable32kHzPin(false);
  RTC.SetSquareWavePin(DS3231SquareWavePin_ModeNone);
  handleTimeFromRTC();
  updateTime();

  //  Connect to WiFi access point or start as Access point
  // Connect the ESP8266 to local WIFI network in Station mode
  //printConfig();

  if (CFG_saved)
  {
    WiFi.mode(WIFI_STA);

    char* devicename = &_config.DeviceName[0];
    wifi_station_set_hostname(devicename); //See more at : http ://www.esp8266.com/viewtopic.php?f=29&t=11124#sthash.458xtq2U.dpuf

    Serial.print("Trying to connect to : ");
    Serial.println(_config.ssid.c_str());
    Serial.print("WiFi key : ");
    Serial.println(_config.password.c_str());

    WiFi.begin(_config.ssid.c_str(), _config.password.c_str());

    WIFI_connected = WiFi.waitForConnectResult();
    if (WIFI_connected != WL_CONNECTED)
    {
      Serial.print("Connection Failed!\nError code : ");
      Serial.println(WIFI_connected);
      Serial.println("Activating to AP mode...");
    }
    else
    {
      if (!_config.dhcp)
      {
        WiFi.config(IPAddress(_config.IP[0], _config.IP[1], _config.IP[2], _config.IP[3]),
          IPAddress(_config.Gateway[0], _config.Gateway[1], _config.Gateway[2], _config.Gateway[3]),
          IPAddress(_config.Netmask[0], _config.Netmask[1], _config.Netmask[2], _config.Netmask[3]),
          IPAddress(_config.DNS[0], _config.DNS[1], _config.DNS[2], _config.DNS[3]));
      }

      WiFi.setAutoReconnect(true);

      Serial.print("Wifi ip:"); Serial.println(WiFi.localIP());
      getNTPtime();
    }
  }

  if ((WIFI_connected != WL_CONNECTED) || !CFG_saved)
  {
    Serial.println("Setting AP mode");

    String defaultSSID = "TexTime-" + String(ESP.getChipId(), HEX);

    // Configure and start AP mode
    WiFi.mode(WIFI_AP);
    WiFi.softAPConfig(_apIP, _apIP, _apNetMsk);
    WiFi.softAP(defaultSSID.c_str());

    // Turn off wifi after 10 minutes
    os_timer_setfn(&_disableWifiTimer, disableWifiCallback, NULL);
    os_timer_arm(&_disableWifiTimer, 1000 * 60 * 10, false);

    // Start DNS catcher
    //_dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
    //_dnsServer.start(53, _config.DeviceName, _apIP);
  }


  WiFi.setSleepMode(WIFI_NONE_SLEEP);

  // Start HTTP Server for configuration
  _server.on("/", []() {
    //Serial.println("index.html");
    _server.send_P(200, "text/html", PAGE_index);
  });

  _server.on("/index.html", []() {
    //Serial.println("index.html");
    _server.send_P(200, "text/html", PAGE_index);
  });

  _server.on("/favicon.ico", []() {
    //Serial.println("favicon.ico");
    _server.send_P(200, "image/png", PAGE_ico, PAGE_ico_size);
  });

  _server.on("/apple-touch-icon.png", []() {
    //Serial.println("favicon.ico");
    _server.send_P(200, "image/png", PAGE_ico, PAGE_ico_size);
  });

  _server.on("/network.html", send_network_configuration_html);

  _server.on("/info.html", []() {
    //Serial.println("info.html");
    _server.send_P(200, "text/html", PAGE_information);
  });

  _server.on("/ntp.html", send_NTP_configuration_html);

  _server.on("/general.html", send_general_html);

  _server.on("/style.css", []() {
    //Serial.println("style.css");
    _server.sendHeader("Cache-Control", "max-age=3600");
    _server.send_P(200, "text/css", PAGE_style_css);
  });

  _server.on("/microajax.js", []() {
    //Serial.println("microajax.js");
    _server.sendHeader("Cache-Control", "max-age=3600");
    _server.send_P(200, "application/javascript", PAGE_microajax_js);
  });

  _server.on("/description.xml", HTTP_GET, [](){
    SSDP.schema(_server.client());
  });

  _server.on("/admin/networkvalues", send_network_configuration_values_html);
  _server.on("/admin/connectionstate", send_connection_state_values_html);
  _server.on("/admin/infovalues", send_information_values_html);
  _server.on("/admin/ntpvalues", send_NTP_configuration_values_html);
  _server.on("/admin/generalvalues", send_general_configuration_values_html);
  _server.on("/admin/modesvalues", send_general_modes_values_html);
  _server.on("/admin/animationsvalues", send_general_animations_values_html);
  _server.on("/admin/ledconfigvalues", send_general_ledconfig_values_html);

  _server.on("/admin/led", send_general_led);


  _server.onNotFound([]() {
    Serial.println("Page Not Found");
    _server.send(400, "text/html", "Page not Found");
  });

  _httpUpdater.setup(&_server);
  _server.begin();
  Serial.println("HTTP server started");

  // ***********  OTA SETUP

  //ArduinoOTA.setHostname(host);
  ArduinoOTA.onStart([]() { // what to do before OTA download insert code here
    Serial.println("Start");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
    ESP.restart();
  });

  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
    ESP.restart();
  });

  /* setup the OTA server */
  ArduinoOTA.begin();

  MDNS.begin(_config.DeviceName.c_str());
  MDNS.addService("http", "tcp", 80);

  NBNS.begin(_config.DeviceName.c_str());

  LLMNR.begin(_config.DeviceName.c_str());

  SSDP.setSchemaURL("description.xml");
  SSDP.setHTTPPort(80);
  SSDP.setName(_config.DeviceName);
  SSDP.setSerialNumber(String(ESP.getFlashChipId()));
  SSDP.setURL("index.html");
  SSDP.setModelName("TexTime");
  SSDP.setModelNumber("Build : " + printDateTime(RtcDateTime(__DATE__, __TIME__)));
  SSDP.setModelURL("http://www.psykokwak.com/blog/index.php/2017/04/04/64");
  SSDP.setManufacturer("Psykokwak");
  SSDP.setManufacturerURL("http://www.psykokwak.com");
  SSDP.setDeviceType("upnp:rootdevice");
  SSDP.begin();

  QTLed.setAutomaticBrightness(_config.brightnessAuto);
  if (!_config.brightnessAuto) QTLed.setBrightness(_config.brightness);
  QTLed.setColor(_config.color[0], _config.color[1], _config.color[2]);
  QTLed.setColorRandom((RandomColorMode)_config.colorRandom);
  QTLed.setMode(_config.mode);
  QTLed.setAnimation(_config.animation);

  Serial.println("Ready");


  //**** Normal Sketch code here...


}


// the loop function runs over and over again forever
void loop() {

  // Update time from RTC
  handleTimeFromRTC();

  // Handle NTP receive packets
  handleNTPRequest();

  // Update time
  handleISRsecondTick();

  // Read current light value
  handleAmbiantLightSensor();

  // OTA request handling
  ArduinoOTA.handle();

  // WebServer requests handling
  _server.handleClient();

  // DNS requests handling 
  //_dnsServer.processNextRequest();

  // Handle led display
  QTLed.handle();

  // For debug purpose only
  toggleLed(_timestamp);
}


