/*
   ESP8266 Webserver for the Arduino Nixie clock
    - Starts the ESP8266 as an access point and provides a web interface to configure and store WiFi credentials.
    - Allows the time server to be defined and stored

   Program with following settings (status line in IDE):

   Generic ESP8266 Module, 80MHz, 80MHz, DIO, 115200, 1M (64k SPIFFS), ck, Disabled, none

   Go to http://192.168.4.1 in a web browser connected to this access point to see it
*/

#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266mDNS.h>
#include <Wire.h>
#include <EEPROM.h>
#include <time.h>
#include <TimeLib.h>
#include <WiFiUdp.h>
#include <Timezone.h>
#include <ArduinoJson.h>
#include <Adafruit_SHT31.h>   //I2C temp sensor
#include <PubSubClient.h>     // MQTT Publish


// For OLED display
#include "icons.h"
#include "fonts.h"
#define I2C_DISPLAY_ADDRESS 0x3c
#define OLED_SSD1306
#if defined (OLED_SSD1306)
#include <SSD1306Wire.h>
#include <OLEDDisplayUi.h>
#endif
// Number of line to display for devices and Wifi
#define I2C_DISPLAY_DEVICE  4
#define WIFI_DISPLAY_NET    4
#define SDA_PIN D2
#define SDC_PIN D1

// For proximity detection
#define PROXIMITY D5    // Input for proximity detector
#define PROXIMITY_TIMEOUT   3*60*1000      // Period of time in minutes without proximity detection before blanking tubes
#define EEPROM_PROXIMITY 353          // EEPROM memory location for proximity config
boolean proximityStatus = true;   
unsigned long lastproximityTime;      // Timestamp of high to low transition of proximity sensor


#ifdef OLED_SSD1306
SSD1306Wire  display(I2C_DISPLAY_ADDRESS, SDA_PIN, SDC_PIN);
#endif
OLEDDisplayUi ui( &display );

  
#define USE_NTP                         // Use NTP Protocol to fetch time  
#define NTP_POOL_URL "pool.ntp.org"     // URL to find NTP server use pool.ntp.org for worldwide servers
#define LONGUPDATE 1*60*60*1000         // Long period between time updates once per hour
#define SHORTUPDATE 60*1000             // Short period between time updates, once per minute
#define WEATHER_SERVER_URL "http://datapoint.metoffice.gov.uk/public/data/val/wxfcs/all/json/"
#define WEATHER_LOCATION_ID 352546      //Marlow Bucks
#define WEATHER_API_KEY "====My Met Office API Key===="   
unsigned long lastWeatherUpdateTime;    // time of  last weather update
#define WEATHERUPDATEPERIOD 15*60000    // Minutes between weather updates
#define TEMPERATUREUPDATEPERIOD 60000    // Minutes between temperature updates
unsigned long lastTempUpdateTime;       // time of  last temperature update
// NTP stuff
unsigned int localPort = 2390;          // local port to listen for UDP packets


IPAddress timeServerIP; 
const char* ntpServerName = NTP_POOL_URL;
boolean ntp_ok = false; // flag set if last NTP request was successful
const int NTP_PACKET_SIZE = 48; // NTP time stamp is in the first 48 bytes of the message

byte packetBuffer[ NTP_PACKET_SIZE]; //buffer to hold incoming and outgoing packets

// A UDP instance to let us send and receive packets over UDP
WiFiUDP udp;

// SHT30 Temp/Humidity sensor
Adafruit_SHT31 sht31 = Adafruit_SHT31();  // Declare I2C Temp/Humidity chip
float temp = 0;       // room temperature
float humidity = 0;   // room humidity
boolean has_sht30 = false;    // flag set if SHT30/31 fitted
byte sht30_address = 0x44;    // Try 0x44 first, and then 0x45 if not found

//  MQTT defines
IPAddress mqttServerIP;
WiFiClient espClient;
PubSubClient mqttclient(espClient);
long lastMsg = 0;
char msg[50];
int value = 0;
// name of MQTT broker
//const char* mqttServerName = "cloudserver.local";
const char* mqttServerName = "192.168.1.4";

// Timezone rules
//
//United Kingdom (London)
TimeChangeRule BST = {"BST", Last, Sun, Mar, 1, 60};        //British Summer Time
TimeChangeRule GMT = {"GMT", Last, Sun, Oct, 2, 0};         //Standard Time
Timezone UK(BST, GMT);
TimeChangeRule *tcr;        //pointer to the time change rule, use to get TZ abbrev
time_t utc, local;
char* months[]={"null","January","February","March","April","May","June","July","August","September","October","November","December"};

struct Weather {
   char  temp[6];
   char  windspeed[6];
   char winddirection[6]; 
   char  weathertype[6];
   char  rainpercent[6];
   char  minssincemidnight[6];
};


struct Weather weather0, weather3, weather6;        /* Declare  weather of type Weather */


#define SOFTWARE_VERSION "1.0.6ntp"
#define DEFAULT_TIME_SERVER_URL "http://time-zone-server.scapp.io/getTime/Europe/Zurich"

#define DEBUG             // DEBUG or DEBUG_OFF

// Access point credentials, be default it is as open AP
const char *ap_ssid = "NixieTimeModule";
const char *ap_password = "";

const char *serialNumber = "0x0x0x";  // this is to be customized at burn time


// Pin 1 on ESP-01, pin 2 on ESP-12E
#define blueLedPin D4
boolean blueLedState = true;

// used for flashing the blue LED
int blinkOnTime = 1000;
int blinkTopTime = 2000;
unsigned long lastMillis = 0;

// Timer for how often we send the I2C data
unsigned long lastI2CUpdateTime = 0;
unsigned long updatetimer=20000;     // Time in milliseconds between time updates


String timeServerURL = "";

ADC_MODE(ADC_VCC);

// I2C Interface definition
#define I2C_SLAVE_ADDR                0x68
#define I2C_TIME_UPDATE               0x00
#define I2C_GET_OPTIONS               0x01
#define I2C_SET_OPTION_12_24          0x02
#define I2C_SET_OPTION_BLANK_LEAD     0x03
#define I2C_SET_OPTION_SCROLLBACK     0x04
#define I2C_SET_OPTION_SUPPRESS_ACP   0x05
#define I2C_SET_OPTION_DATE_FORMAT    0x06
#define I2C_SET_OPTION_DAY_BLANKING   0x07
#define I2C_SET_OPTION_BLANK_START    0x08
#define I2C_SET_OPTION_BLANK_END      0x09
#define I2C_SET_OPTION_FADE_STEPS     0x0a
#define I2C_SET_OPTION_SCROLL_STEPS   0x0b
#define I2C_SET_OPTION_BACKLIGHT_MODE 0x0c
#define I2C_SET_OPTION_RED_CHANNEL    0x0d
#define I2C_SET_OPTION_GREEN_CHANNEL  0x0e
#define I2C_SET_OPTION_BLUE_CHANNEL   0x0f
#define I2C_SET_OPTION_CYCLE_SPEED    0x10
#define I2C_SHOW_IP_ADDR              0x11
#define I2C_SET_OPTION_FADE           0x12
#define I2C_SET_OPTION_USE_LDR        0x13
#define I2C_SET_OPTION_BLANK_MODE     0x14
#define I2C_SET_OPTION_SLOTS_MODE     0x15

// To blank tubes by proximity detection
#define I2C_PROXIMITY_BLANK     0x80

// Clock config
byte configHourMode;
byte configBlankLead;
byte configScrollback;
byte configSuppressACP;
byte configDateFormat;
byte configDayBlanking;
byte configBlankFrom;
byte configBlankTo;
byte configFadeSteps;
byte configScrollSteps;
byte configBacklightMode;
byte configRedCnl;
byte configGreenCnl;
byte configBlueCnl;
byte configCycleSpeed;
byte configUseFade;
byte configUseLDR;
byte configBlankMode;
byte configSlotsMode;
// For Proximity
byte configUseProximity;

ESP8266WebServer server(80);


// ---------------------------------------------------------------------------------------------------
// ------------------------------------------ DISPLAY FUNCTIONS --------------------------------------
// ---------------------------------------------------------------------------------------------------

/* ======================================================================
  Function: drawFrameWifi
  Purpose : WiFi logo and IP address
  Input   : OLED display pointer
  Output  : -
  Comments: -
  ====================================================================== */
void drawFrameWifi(OLEDDisplay *display, OLEDDisplayUiState* state, int16_t x, int16_t y) {
  display->clear();
  display->setTextAlignment(TEXT_ALIGN_CENTER);
  display->setFont(Roboto_Condensed_Bold_Bold_16);
  // see http://blog.squix.org/2015/05/esp8266-nodemcu-how-to-create-xbm.html
  // on how to create xbm files
  display->drawXbm( x + (128 - WiFi_width) / 2, 0, WiFi_width, WiFi_height, WiFi_bits);

  // Display Soft Access Point IP address if not connected.
  if (WiFi.status() == WL_CONNECTED) {
    display->drawString(x + 64, WiFi_height + 4, WiFi.localIP().toString());
  } else {
    display->drawString(x + 64, WiFi_height + 4, WiFi.softAPIP().toString());
  }
}

/* ======================================================================
  Function: drawFrameLogo
  Purpose : Company logo info screen (called by OLED ui)
  Input   : OLED display pointer
  Output  : -
  Comments: -
  ====================================================================== */
void drawFrameLogo(OLEDDisplay *display, OLEDDisplayUiState* state, int16_t x, int16_t y) {
  display->clear();
  display->drawXbm(x + (128 - stratacom_width) / 2, y, stratacom_width, stratacom_height, stratacom_bits);
  //ui.disableIndicator();
}
/* ======================================================================
  Function: drawWeather
  Purpose : Todays weather (called by OLED ui)
  Input   : OLED display pointer
  Output  : -
  Comments: -
  ====================================================================== */

void weatherdisplay(Weather * weather, int16_t x, int16_t y) {
  
  // Define a pointer to an array of arrays
  const char *type[31] = { w0_bits, w1_bits, w2_bits, w3_bits, w4_bits, w5_bits, w6_bits, w7_bits, w8_bits,
                           w9_bits, w10_bits, w11_bits, w12_bits, w13_bits, w14_bits, w15_bits, w16_bits, w17_bits,
                           w18_bits, w19_bits, w20_bits, w21_bits, w22_bits, w23_bits, w24_bits, w25_bits, w26_bits,
                           w27_bits, w28_bits, w29_bits, w30_bits };
  // display an image referenced in an array of icons
  display.clear();
  display.drawXbm(0, 22,32, 32, type[atoi(weather->weathertype)]);
  display.setFont(Roboto_Condensed_Bold_Bold_16);
  display.drawString(24, 48, String(weather->temp)+ "°C" );
  display.drawString(49, 24, String(weather->rainpercent)+ "%" );
  // Define a pointer to an array of arrays for wind direction
  const char *compass[17] = { N_bits, NNE_bits, NE_bits, ENE_bits, E_bits, ESE_bits, SE_bits, SSE_bits, 
                              S_bits, SSW_bits, SW_bits, WSW_bits, W_bits, WNW_bits, NW_bits, NNW_bits, 
                              STILL_bits };
  String compasscode[17] = { "N", "NNE", "NE", "ENE", "E", "ESE", "SE", "SSE", "S",
                                "SSW", "SW", "WSW", "W", "WNW", "NW", "NNW", "STILL" };

  char compasspoint[6];
  for( byte j = 0; j < 17; j++ ) {
    if (compasscode[j] == String(weather->winddirection) ) {
      display.drawXbm(90, 22,24, 24, compass[j]);
    }
  }
  display.setFont(Roboto_Condensed_Bold_Bold_16);
  display.drawString(102,48, String(weather->windspeed)+" mph" );
  display.drawHorizontalLine(0,17,128);
  display.setFont(Roboto_Condensed_Bold_Bold_16);
  
  // Calculate time period
  int hour = atoi(weather->minssincemidnight)/60;
  int hour3 = (hour +3) % 24;
    // If DST add one hour to time
  if (UK.locIsDST(now())) { 
    hour = (hour +1) % 24;
    hour3 = (hour3 +1) % 24;
    }
  display.drawString(x+64, y+0, String(hour)+":00 To "+String(hour3)+":00" );

  return;
}
// Draw first weather frame  
void drawWeather(OLEDDisplay *display, OLEDDisplayUiState* state, int16_t x, int16_t y) {
  weatherdisplay(&weather0, x,y);
}
// Draw second weather frame
void drawWeather3(OLEDDisplay *display, OLEDDisplayUiState* state, int16_t x, int16_t y) {
  weatherdisplay(&weather3, x,y);
}
// Draw third weather frame
void drawWeather6(OLEDDisplay *display, OLEDDisplayUiState* state, int16_t x, int16_t y) {
  weatherdisplay(&weather6, x,y);
}


/* ======================================================================
  Function: drawTempHumidity
  Purpose : Display Crrent room Temperature and Humidity on OLED (called by OLED ui)
  Input   : OLED display pointer
  Output  : -
  Comments: -
  ====================================================================== */
void drawTempHumidity(OLEDDisplay *display, OLEDDisplayUiState* state, int16_t x, int16_t y) {
  if (!has_sht30) return;
  char buffer [10];
  String s= dtostrf( temp,5,2,buffer);
  display->clear();
  display->setTextAlignment(TEXT_ALIGN_CENTER);
  display->setFont(Roboto_Condensed_Bold_Bold_16);
  display->drawString(64 + x, 40 + y, "Temp "+ s + "°C");
  display->setFont(ArialMT_Plain_24);
  s= dtostrf( humidity,3,0,buffer);
  display->drawString(64 + x, 10 + y, "Humidity "+ s + "%");
}

/* ======================================================================
  Function: drawTimeDate
  Purpose : Display Time and Date on OLED (called by OLED ui)
  Input   : OLED display pointer
  Output  : -
  Comments: -
  ====================================================================== */
void drawTimeDate(OLEDDisplay *display, OLEDDisplayUiState* state, int16_t x, int16_t y) {
  display->clear();
  display->setTextAlignment(TEXT_ALIGN_CENTER);
  display->setFont(Roboto_Condensed_Bold_Bold_16);
#ifdef USE_NTP
  display->drawString(64 + x, 40 + y, String(day())+" "+months[month()]);
  display->setFont(ArialMT_Plain_24);
  String hh= (hour() < 10) ? "0"+String(hour()) : String(hour());
  String mm= (minute() < 10) ? "0"+String(minute()) : String(minute());
  display->drawString(64 + x, 10 + y, hh+":"+mm);
#else 
  display->drawString(64 + x, 40 + y, String(daynow)+" "+months[monthnow]+" "+String(yearnow));
  display->setFont(ArialMT_Plain_24);
  display->drawString(64 + x, 10 + y, String(hournow)+":"+String(minutenow));
#endif
  //ui.disableIndicator();
}

// this array keeps function pointers to all frames
// frames are the single views that slide from right to left
  FrameCallback frames[] = { drawFrameLogo, drawFrameWifi, drawTimeDate, drawWeather, drawWeather3, drawWeather6, drawWeather, drawTempHumidity};
  int numberOfFrames = 8;

/* ======================================================================
  Function: drawProgress
  Purpose : prograss indication
  Input   : OLED display pointer
          percent of progress (0..100)
          String above progress bar
          String below progress bar
  Output  : -
  Comments: -
  ====================================================================== */
void drawProgress(OLEDDisplay *display, int percentage, String labeltop, String labelbot) {
  display->clear();
  display->setTextAlignment(TEXT_ALIGN_CENTER);
  display->setFont(Roboto_Condensed_Bold_Bold_16);
  display->drawString(64, 8, labeltop);
  display->drawProgressBar(10, 28, 108, 12, percentage);
  display->drawString(64, 48, labelbot);
  display->display();
}

/* ======================================================================
  Function: drawProgress
  Purpose : prograss indication
  Input   : OLED display pointer
          percent of progress (0..100)
          String above progress bar
  Output  : -
  Comments: -
  ====================================================================== */
void drawProgress(OLEDDisplay *display, int percentage, String labeltop ) {
  drawProgress(display, percentage, labeltop, String(""));
}


// ----------------------------------------------------------------------------------------------------
// ----------------------------------------------  Set up  --------------------------------------------
// ----------------------------------------------------------------------------------------------------
void setup()
{
//#ifdef DEBUG
  Serial.begin(115200);
  Serial.println();
  Serial.println("Configuring access point...");
//#endif
  pinMode(PROXIMITY, INPUT);    // Input for proximity detection.
  pinMode(blueLedPin, OUTPUT);

  Wire.begin(SDA_PIN, SDC_PIN); // SDA = 0, SCL = 2
#ifdef DEBUG
  Serial.println("I2C master started");
  Serial.println("SHT30 test");
#endif

// Scan I2C bus for SHT30/31
  for (int idx = 0x44 ; idx < 0x45 ; idx++)
  {
    Wire.beginTransmission(idx);
    int error = Wire.endTransmission();
    if (error == 0) {
#ifdef DEBUG
      Serial.print("Found SHT30/31 at");
      Serial.println(idx);
#endif 
      has_sht30 = true;
      sht30_address = idx;
    }
  }
  if(has_sht30) sht31.begin(sht30_address);

  
  // initialize display
  display.init();
  display.flipScreenVertically();
  display.clear();
  display.display();
  display.setFont(ArialMT_Plain_10);
  display.setTextAlignment(TEXT_ALIGN_CENTER);
  display.setContrast(255);    

  ui.setTargetFPS(15);
  ui.setFrameAnimation(SLIDE_LEFT);
  ui.setFrames(frames, numberOfFrames);
  ui.setTimePerFrame(5000);
  ui.setTimePerTransition(200);
  ui.disableAllIndicators();
  ui.init();
  display.flipScreenVertically();

  EEPROM.begin(512);
  delay(10);

  // read eeprom for ssid and pass
  String esid = getSSIDFromEEPROM();
  String epass = getPasswordFromEEPROM();
  timeServerURL = getTimeServerURLFromEEPROM();

  // Try to connect, if we have valid credentials
  boolean wlanConnected = false;
  if (esid.length() > 0) {
#ifdef DEBUG
    Serial.print("found esid: ");
    Serial.println(esid);
    Serial.println("Trying to connect");
#endif
    wlanConnected = connectToWLAN(esid.c_str(), epass.c_str());
  }

  // If we can't connect, fall back into AP mode
  if (wlanConnected) {
#ifdef DEBUG
    Serial.println("WiFi connected, stop softAP");
#endif
    drawProgress(&display, 100, F("Connected"));
    WiFi.mode(WIFI_STA);
  } else {
#ifdef DEBUG
    Serial.println("WiFi not connected, start softAP");
#endif
    drawProgress(&display, 100, F("Started SoftAP"));
    WiFi.mode(WIFI_AP_STA);
    
    // You can add the password parameter if you want the AP to be password protected
    if (strlen(ap_password) > 0) {
      WiFi.softAP(ap_ssid, ap_password);
    } else {
      WiFi.softAP(ap_ssid);
    }
  }

  IPAddress apIP = WiFi.softAPIP();
  IPAddress myIP = WiFi.localIP();
#ifdef DEBUG
  Serial.print("AP IP address: ");
  Serial.println(apIP);
  Serial.print("IP address: ");
  Serial.println(myIP);
#endif

// NTP setup
#ifdef DEBUG
  Serial.println("Starting UDP");
  Serial.print("Local port: ");
  Serial.println(udp.localPort());
#endif
  udp.begin(localPort);
 
  /* Set page handler functions */
  server.on("/",            rootPageHandler);
  server.on("/wlan_config", wlanPageHandler);
  server.on("/time",        timeServerPageHandler);
  server.on("/reset",       resetPageHandler);
  server.on("/updatetime",  updateTimePageHandler);
  server.on("/clockconfig", clockConfigPageHandler);
  server.on("/local.css",   localCSSHandler);
  server.onNotFound(handleNotFound);

  server.begin();

#ifdef DEBUG
  Serial.println("HTTP server started");
#endif

// Set up MQTT client
  mqttclient.setServer(mqttServerName, 1883);
#ifdef DEBUG
  Serial.println("MQTT client started");
#endif
  // Set up mDNS 
  MDNS.begin("nixieclock"); 
  delay(1000);
#ifdef DEBUG
  Serial.println("mDNS responder started");
#endif
  // Add service to MDNS-SD
  MDNS.addService("http", "tcp", 80);
// Proximity Setup
unsigned long proximitytimeout = PROXIMITY_TIMEOUT;
configUseProximity = EEPROM.read(EEPROM_PROXIMITY);    // Read saved configuration status

}

// ----------------------------------------------------------------------------------------------------
// --------------------------------------------- Main Loop --------------------------------------------
// ----------------------------------------------------------------------------------------------------
void loop()
{
  server.handleClient();
  
  if (ui.getUiState()->currentFrame == has_sht30 ? 7:6 ) { ui.setAutoTransitionBackwards(); }   //Cycle between weather frames as well as room temp if sht30 fitted
  if (ui.getUiState()->currentFrame == 3 ) { ui.setAutoTransitionForwards(); }
  
  int remainingTimeBudget = ui.update();
  if (remainingTimeBudget > 0) {
    // You can do some work here
    // Don't do stuff if you are below your
    // time budget.
    delay(remainingTimeBudget);
  }
  if (WiFi.status() == WL_CONNECTED) {
    if (lastMillis > millis()) {
      // rollover
      lastI2CUpdateTime = 0;
      lastWeatherUpdateTime = 0;
      lastTempUpdateTime = 0;
    }
    
    // See if it is time to update the Clock
    if (((millis() - lastI2CUpdateTime) > updatetimer) || 
         (lastI2CUpdateTime==0)
       ) {

#ifdef USE_NTP
      // Now recover the current time using NTP protocol
      String timeStr = getTimeFromNtpServer();
#else      
      // Try to recover the current time
      String timeStr = getTimeFromTimeZoneServer();
#endif

      // Send the time to the I2C client, but only if there was no error
      if (!timeStr.startsWith("ERROR:")) {
        sendTimeToI2C(timeStr);
        
        // all OK, flash 10 millisecond per second
        blinkOnTime = 10;
        blinkTopTime = 2000;
#ifdef DEBUG
        Serial.println("Normal time serve mode");
#endif
      } else {
        // connected, but time server not found, flash middle speed
        blinkOnTime = 250;
        blinkTopTime = 500;
#ifdef DEBUG
        Serial.println("Connected, but no time server found");
#endif
      }

      // Allow the IP to be displayed on the clock
      sendIPAddressToI2C(WiFi.localIP());

      lastI2CUpdateTime = millis();
    }


    // See if it is time to update the Weather
    if (((millis() - lastWeatherUpdateTime) > WEATHERUPDATEPERIOD) || (lastWeatherUpdateTime==0)) {
      fetchweather();
      lastWeatherUpdateTime = millis();
    }
    if (((millis() - lastMillis) > blinkTopTime) && blueLedState) {
      lastMillis = millis();
      blueLedState = false;
#ifdef DEBUG
      //setBlueLED(blueLedState);
#endif
    } else if (((millis() - lastMillis) > blinkOnTime) && !blueLedState) {
      blueLedState = true;
#ifdef DEBUG
      //setBlueLED(blueLedState);
#endif    
    }

    // See if it is time to update the Temp/Humidity
    if (((millis() - lastTempUpdateTime) > TEMPERATUREUPDATEPERIOD) || (lastTempUpdateTime==0)) {
      readtemp();
      lastTempUpdateTime = millis();
    } 
 
//  Proximity detection
//-----------------------------------------
    boolean proximity= digitalRead(PROXIMITY);
   digitalWrite(blueLedPin, proximity); 
//#ifdef DEBUG
   setBlueLED(proximity);
//#endif
    if (configUseProximity) {

      if (proximity && !proximityStatus) {    //Status change from inactive to active
        //Send I2C message to unblank tubes if blanked
        setClockOptionBoolean(I2C_PROXIMITY_BLANK, true);
#ifdef DEBUG
        Serial.println("Proximity unblanking");
#endif
        proximityStatus = true;
        lastproximityTime = millis();
      }
      if (!proximity && proximityStatus) {    //Status change from active to inactive
      // Start timeout after which we blank tubes
        lastproximityTime = millis();   //Remember timestamp of high to low transition
        proximityStatus = false;
      }
      if (!proximity && !proximityStatus) {    //Still inactive check if timer has expired
        if (millis() < lastproximityTime ) {   //Rollover clear transition status restart timer
          lastproximityTime = millis();
        }
      if ( lastproximityTime + PROXIMITY_TIMEOUT < millis() ) {   // Timer expired
        lastproximityTime = millis(); 
      // Timer expired blank tubes
#ifdef DEBUG
        Serial.println("Proximity blanking");
#endif
        setClockOptionBoolean(I2C_PROXIMITY_BLANK, false);
        }
      } 
    }  
  } else {
    // offline, flash fast
    blinkOnTime = 100;
    blinkTopTime = 200;
#ifdef DEBUG
    //Serial.println("Offline");
#endif
  
  }
  mqttclient.loop();
}

// ----------------------------------------------------------------------------------------------------
// ------------------------------------------- Page Handlers ------------------------------------------
// ----------------------------------------------------------------------------------------------------

/**
   Root page for the webserver
*/
void rootPageHandler()
{
#ifdef DEBUG
    Serial.println("Serving home page");
#endif
  String response_message = getHTMLHead();
  response_message += getNavBar();

  // Status table
  response_message += getTableHead2Col("Current Status", "Name", "Value");

  if (WiFi.status() == WL_CONNECTED)
  {
    IPAddress ip = WiFi.localIP();
    String ipStr = String(ip[0]) + '.' + String(ip[1]) + '.' + String(ip[2]) + '.' + String(ip[3]);

    response_message += getTableRow2Col("WLAN IP", ipStr);
    response_message += getTableRow2Col("WLAN MAC", WiFi.macAddress());
    response_message += getTableRow2Col("WLAN SSID", WiFi.SSID());
#ifdef USE_NTP
    response_message += getTableRow2Col("Time according to server", String(hour())+":"+String(minute())+"  "+String(day())+","+String(month())+","+String(year()));
#else
    response_message += getTableRow2Col("Time server URL", timeServerURL);
    response_message += getTableRow2Col("Time according to server", getTimeFromTimeZoneServer());
#endif
  }
  else
  {
    IPAddress softapip = WiFi.softAPIP();
    String ipStrAP = String(softapip[0]) + '.' + String(softapip[1]) + '.' + String(softapip[2]) + '.' + String(softapip[3]);
    response_message += getTableRow2Col("AP IP", ipStrAP);
    response_message += getTableRow2Col("AP MAC", WiFi.softAPmacAddress());
  }

  // Make the uptime readable
  long upSecs = millis() / 1000;
  long upDays = upSecs / 86400;
  long upHours = (upSecs - (upDays * 86400)) / 3600;
  long upMins = (upSecs - (upDays * 86400) - (upHours * 3600)) / 60;
  upSecs = upSecs - (upDays * 86400) - (upHours * 3600) - (upMins * 60);
  String uptimeString = ""; uptimeString += upDays; uptimeString += " days, "; uptimeString += upHours, uptimeString += " hours, "; uptimeString += upMins; uptimeString += " mins, "; uptimeString += upSecs; uptimeString += " secs";

  response_message += getTableRow2Col("Uptime", uptimeString);

  String lastUpdateString = ""; lastUpdateString += (millis() - lastI2CUpdateTime);
  response_message += getTableRow2Col("Time last update", lastUpdateString);

  response_message += getTableRow2Col("Version", SOFTWARE_VERSION);
  response_message += getTableRow2Col("Serial Number", serialNumber);

  // Scan I2C bus
  for (int idx = 0 ; idx < 128 ; idx++)
  {
    Wire.beginTransmission(idx);
    int error = Wire.endTransmission();
    if (error == 0) {
      response_message += getTableRow2Col("Found I2C slave at",idx);
    }
  }

  response_message += getTableFoot();

  float voltaje = (float)ESP.getVcc()/(float)1024;
  voltaje -= 0.01f;  // by default reads high
  char dtostrfbuffer[15];
  dtostrf(voltaje,7, 2, dtostrfbuffer);
  String vccString = String(dtostrfbuffer);
  
  // ESP8266 Info table
  response_message += getTableHead2Col("ESP8266 information", "Name", "Value");
  response_message += getTableRow2Col("Sketch size", ESP.getSketchSize());
  response_message += getTableRow2Col("Free sketch size", ESP.getFreeSketchSpace());
  response_message += getTableRow2Col("Free heap", ESP.getFreeHeap());
  response_message += getTableRow2Col("Boot version", ESP.getBootVersion());
  response_message += getTableRow2Col("CPU Freqency (MHz)", ESP.getCpuFreqMHz());
  response_message += getTableRow2Col("SDK version", ESP.getSdkVersion());
  response_message += getTableRow2Col("Chip ID", ESP.getChipId());
  response_message += getTableRow2Col("Flash Chip ID", ESP.getFlashChipId());
  response_message += getTableRow2Col("Flash size", ESP.getFlashChipRealSize());
  response_message += getTableRow2Col("Vcc", vccString);
  response_message += getTableFoot();

  response_message += getHTMLFoot();

  server.send(200, "text/html", response_message);
}

// ===================================================================================================================
// ===================================================================================================================

/**
   WLAN page allows users to set the WiFi credentials
*/
void wlanPageHandler()
{
  // Check if there are any GET parameters, if there are, we are configuring
  if (server.hasArg("ssid"))
  {
    if (server.hasArg("password"))
    {
#ifdef DEBUG
    Serial.println("Connect WiFi");
    Serial.print("SSID:");
    Serial.println(server.arg("ssid"));
    Serial.print("PASSWORD:");
    Serial.println(server.arg("password"));
#endif
      WiFi.begin(server.arg("ssid").c_str(), server.arg("password").c_str());
    }
    else
    {
      WiFi.begin(server.arg("ssid").c_str());
#ifdef DEBUG
    Serial.println("Connect WiFi");
    Serial.print("SSID:");
    Serial.println(server.arg("ssid"));
#endif
    }

    while (WiFi.status() != WL_CONNECTED)
    {
      delay(500);
#ifdef DEBUG
      Serial.print(".");
#endif
    }

    storeCredentialsInEEPROM(server.arg("ssid"), server.arg("password"));

#ifdef DEBUG
    Serial.println("");
    Serial.println("WiFi connected");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
    Serial.print("SoftAP IP address: ");
    Serial.println(WiFi.softAPIP());
#endif
  }

  String esid = getSSIDFromEEPROM();

  String response_message = getHTMLHead();
  response_message += getNavBar();

  // form header
  response_message += getFormHead("Set Configuration");

  // Get number of visible access points
  int ap_count = WiFi.scanNetworks();

  // Day blanking
  response_message += getDropDownHeader("WiFi:", "ssid", true);

  if (ap_count == 0)
  {
    response_message += getDropDownOption("-1", "No wifi found", true);
  }
  else
  {
    // Show access points
    for (uint8_t ap_idx = 0; ap_idx < ap_count; ap_idx++)
    {
      String ssid = String(WiFi.SSID(ap_idx));
      String wlanId = String(WiFi.SSID(ap_idx));
      (WiFi.encryptionType(ap_idx) == ENC_TYPE_NONE) ? wlanId += "" : wlanId += " (requires password)";
      wlanId += " (RSSI: ";
      wlanId += String(WiFi.RSSI(ap_idx));
      wlanId += ")";

#ifdef DEBUG
    Serial.println("");
    Serial.print("found ssid: ");
    Serial.println(WiFi.SSID(ap_idx));
    Serial.print("Is current: ");
    if ((esid==ssid)) {
      Serial.println("Y");
    } else {
      Serial.println("N");
    }
#endif

      response_message += getDropDownOption(ssid, wlanId, (esid==ssid));
    }

    response_message += getDropDownFooter();

    response_message += getTextInput("WiFi password (if required)","password","",false);
    response_message += getSubmitButton("Set");

    response_message += getFormFoot();
  }

  response_message += getHTMLFoot();

  server.send(200, "text/html", response_message);
}

// ===================================================================================================================
// ===================================================================================================================

/**
   Get the local time from the time server, and modify the time server URL if needed
*/
void timeServerPageHandler()
{
  // Check if there are any GET parameters, if there are, we are configuring
  if (server.hasArg("timeserverurl"))
  {
    if (strlen(server.arg("timeserverurl").c_str()) > 4) {
      timeServerURL = server.arg("timeserverurl").c_str();
      storeTimeServerURLInEEPROM(timeServerURL);
    }
  }

  String response_message = getHTMLHead();
  response_message += getNavBar();

  // form header
  response_message += getFormHead("Select time server");

  // only fill in the value we have if it looks realistic
  if ((timeServerURL.length() < 10) || (timeServerURL.length() > 250)) {
    timeServerURL = "";
  }

  response_message += getTextInputWide("URL", "timeserverurl", timeServerURL, false);
  response_message += getSubmitButton("Set");

  response_message += getFormFoot();
  response_message += getHTMLFoot();

  server.send(200, "text/html", response_message);
}

// ===================================================================================================================
// ===================================================================================================================

/**
   Get the local time from the time server, and send it via I2C right now
*/
void updateTimePageHandler()
{
  String timeString = getTimeFromTimeZoneServer();

  String response_message = getHTMLHead();
  response_message += getNavBar();


  if (timeString.substring(1,6) == "ERROR:") {
    response_message += "<div class=\"container\" role=\"main\"><h3 class=\"sub-header\">Send time to I2C right now</h3>";
    response_message += "<div class=\"alert alert-danger fade in\"><strong>Error!</strong> Could not recover the time from time server. ";
    response_message += timeString;
    response_message += "</div></div>";
  } else {
    boolean result = sendTimeToI2C(timeString);

    response_message += "<div class=\"container\" role=\"main\"><h3 class=\"sub-header\">Send time to I2C right now</h3>";
    if (result) {
      response_message += "<div class=\"alert alert-success fade in\"><strong>Success!</strong> Update sent.</div></div>";
    } else {
      response_message += "<div class=\"alert alert-danger fade in\"><strong>Error!</strong> Update was not sent.</div></div>";
    }
  }

  response_message += getHTMLFoot();

  server.send(200, "text/html", response_message);
}

// ===================================================================================================================
// ===================================================================================================================

/**
   Reset the EEPROM and stored values
*/
void resetPageHandler() {
  resetEEPROM();

  String response_message = getHTMLHead();
  response_message += getNavBar();

  response_message += "<div class=\"container\" role=\"main\"><h3 class=\"sub-header\">Send time to I2C right now</h3>";
  response_message += "<div class=\"alert alert-success fade in\"><strong>Success!</strong> Reset done.</div></div>";

  response_message += getHTMLFoot();

  server.send(200, "text/html", response_message);
}

// ===================================================================================================================
// ===================================================================================================================

/**
   Page for the clock configuration.
*/
void clockConfigPageHandler()
{
  if (server.hasArg("12h24hMode"))
  {
#ifdef DEBUG
    Serial.print("Got 24h mode param: "); Serial.println(server.arg("12h24hMode"));
#endif
    if ((server.arg("12h24hMode") == "24h") && (configHourMode)) {
#ifdef DEBUG
      Serial.println("I2C --> Set 24h mode");
#endif
      setClockOption12H24H(true);
    }

    if ((server.arg("12h24hMode") == "12h")  && (!configHourMode)) {
#ifdef DEBUG
      Serial.println("I2C --> Set 12h mode");
#endif
      setClockOption12H24H(false);
    }
  }

  // -----------------------------------------------------------------------------

  if (server.hasArg("blankLeading"))
  {
#ifdef DEBUG
    Serial.print("Got blankLeading param: "); Serial.println(server.arg("blankLeading"));
#endif
    if ((server.arg("blankLeading") == "blank") && (!configBlankLead)) {
#ifdef DEBUG
      Serial.println("I2C --> Set blank leading zero");
#endif
      setClockOptionBlankLeadingZero(false);
    }

    if ((server.arg("blankLeading") == "show") && (configBlankLead)) {
#ifdef DEBUG
      Serial.println("I2C --> Set show leading zero");
#endif
      setClockOptionBlankLeadingZero(true);
    }
  }

  // -----------------------------------------------------------------------------

  if (server.hasArg("useScrollback"))
  {
#ifdef DEBUG
    Serial.print("Got useScrollback param: "); Serial.println(server.arg("useScrollback"));
#endif
    if ((server.arg("useScrollback") == "on") && (!configScrollback)) {
#ifdef DEBUG
      Serial.println("I2C --> Set scrollback on");
#endif
      setClockOptionScrollback(false);
    }

    if ((server.arg("useScrollback") == "off") && (configScrollback)) {
#ifdef DEBUG
      Serial.println("I2C --> Set scrollback off");
#endif
      setClockOptionScrollback(true);
    }
  }

  // -----------------------------------------------------------------------------

  if (server.hasArg("suppressACP"))
  {
#ifdef DEBUG
    Serial.print("Got suppressACP param: "); Serial.println(server.arg("suppressACP"));
#endif
    if ((server.arg("suppressACP") == "on") && (!configSuppressACP)) {
#ifdef DEBUG
      Serial.println("I2C --> Set suppressACP on");
#endif
      setClockOptionSuppressACP(false);
    }

    if ((server.arg("suppressACP") == "off") && (configSuppressACP)) {
#ifdef DEBUG
      Serial.println("I2C --> Set suppressACP off");
#endif
      setClockOptionSuppressACP(true);
    }
  }

  // -----------------------------------------------------------------------------

  if (server.hasArg("useFade"))
  {
#ifdef DEBUG
    Serial.print("Got useFade param: "); Serial.println(server.arg("useFade"));
#endif
    if ((server.arg("useFade") == "on") && (!configUseFade)) {
#ifdef DEBUG
      Serial.println("I2C --> Set useFade on");
#endif
      setClockOptionUseFade(false);
    }

    if ((server.arg("useFade") == "off") && (configUseFade)) {
#ifdef DEBUG
      Serial.println("I2C --> Set useFade off");
#endif
      setClockOptionUseFade(true);
    }
  }

  // -----------------------------------------------------------------------------

  if (server.hasArg("useLDR"))
  {
#ifdef DEBUG
    Serial.print("Got useLDR param: "); Serial.println(server.arg("useLDR"));
#endif
    if ((server.arg("useLDR") == "on") && (!configUseLDR)) {
#ifdef DEBUG
      Serial.println("I2C --> Set useLDR on");
#endif
      setClockOptionUseLDR(false);
    }

    if ((server.arg("useLDR") == "off") && (configUseLDR)) {
#ifdef DEBUG
      Serial.println("I2C --> Set useLDR off");
#endif
      setClockOptionUseLDR(true);
    }
  }


//Proximity Option set
  if (server.hasArg("useProximity"))
  {
#ifdef DEBUG
    Serial.print("Got useProximity param: "); Serial.println(server.arg("useProximity"));
#endif
    if ((server.arg("useProximity") == "on") && (!configUseProximity)) {
#ifdef DEBUG
      Serial.println(" Set useProximity on");
#endif
      configUseProximity=1;
    }

    if ((server.arg("useProximity") == "off") && (configUseProximity)) {
#ifdef DEBUG
      Serial.println(" Set useProximity off");
#endif
      configUseProximity=0;
    }
    // Save to EEPROM
    EEPROM.write(EEPROM_PROXIMITY,configUseProximity);
    EEPROM.commit();
  }


  // -----------------------------------------------------------------------------

  if (server.hasArg("dateFormat")) {
#ifdef DEBUG
    Serial.print("Got dateFormat param: "); Serial.println(server.arg("dateFormat"));
#endif
    byte newDateFormat = atoi(server.arg("dateFormat").c_str());
    if (newDateFormat != configDateFormat) {
      setClockOptionDateFormat(newDateFormat);
#ifdef DEBUG
      Serial.print("I2C --> Set dateFormat: "); Serial.println(newDateFormat);
#endif
    }
  }

  // -----------------------------------------------------------------------------

  if (server.hasArg("dayBlanking")) {
#ifdef DEBUG
    Serial.print("Got dayBlanking param: "); Serial.println(server.arg("dayBlanking"));
#endif
    byte newDayBlanking = atoi(server.arg("dayBlanking").c_str());
    if (newDayBlanking != configDayBlanking) {
      setClockOptionDayBlanking(newDayBlanking);
#ifdef DEBUG
      Serial.print("I2C --> Set dayBlanking: "); Serial.println(newDayBlanking);
#endif
    }
  }

  // -----------------------------------------------------------------------------

  if (server.hasArg("blankFrom")) {
#ifdef DEBUG
    Serial.print("Got blankFrom param: "); Serial.println(server.arg("blankFrom"));
#endif
    byte newBlankFrom = atoi(server.arg("blankFrom").c_str());
    if (newBlankFrom != configBlankFrom) {
      setClockOptionBlankFrom(newBlankFrom);
#ifdef DEBUG
      Serial.print("I2C --> Set blankFrom: "); Serial.println(newBlankFrom);
#endif
    }
  }

  // -----------------------------------------------------------------------------

  if (server.hasArg("blankTo")) {
#ifdef DEBUG
    Serial.print("Got blankTo param: "); Serial.println(server.arg("blankTo"));
#endif
    byte newBlankTo = atoi(server.arg("blankTo").c_str());
    if (newBlankTo != configBlankTo) {
      setClockOptionBlankTo(newBlankTo);
#ifdef DEBUG
      Serial.print("I2C --> Set blankTo: "); Serial.println(newBlankTo);
#endif
    }
  }

  // -----------------------------------------------------------------------------

  if (server.hasArg("fadeSteps")) {
#ifdef DEBUG
    Serial.print("Got fadeSteps param: "); Serial.println(server.arg("fadeSteps"));
#endif
    byte newFadeSteps = atoi(server.arg("fadeSteps").c_str());
    if (newFadeSteps != configFadeSteps) {
      setClockOptionFadeSteps(newFadeSteps);
#ifdef DEBUG
      Serial.print("I2C --> Set fadeSteps: "); Serial.println(newFadeSteps);
#endif
    }
  }

  // -----------------------------------------------------------------------------

  if (server.hasArg("scrollSteps")) {
#ifdef DEBUG
    Serial.print("Got scrollSteps param: "); Serial.println(server.arg("scrollSteps"));
#endif
    byte newScrollSteps = atoi(server.arg("scrollSteps").c_str());
    if (newScrollSteps != configScrollSteps) {
      setClockOptionScrollSteps(newScrollSteps);
#ifdef DEBUG
      Serial.print("I2C --> Set fadeSteps: "); Serial.println(newScrollSteps);
#endif
    }
  }

  // -----------------------------------------------------------------------------

  if (server.hasArg("backLight")) {
#ifdef DEBUG
    Serial.print("Got backLight param: "); Serial.println(server.arg("backLight"));
#endif
    byte newBacklight = atoi(server.arg("backLight").c_str());
    if (newBacklight != configBacklightMode) {
      setClockOptionBacklightMode(newBacklight);
#ifdef DEBUG
      Serial.print("I2C --> Set backLight: "); Serial.println(newBacklight);
#endif
    }
  }

  // -----------------------------------------------------------------------------

  if (server.hasArg("redCnl")) {
#ifdef DEBUG
    Serial.print("Got redCnl param: "); Serial.println(server.arg("redCnl"));
#endif
    byte newRedCnl = atoi(server.arg("redCnl").c_str());
    if (newRedCnl != configRedCnl) {
      setClockOptionRedCnl(newRedCnl);
#ifdef DEBUG
      Serial.print("I2C --> Set redCnl: "); Serial.println(newRedCnl);
#endif
    }
  }

  // -----------------------------------------------------------------------------

  if (server.hasArg("grnCnl")) {
#ifdef DEBUG
    Serial.print("Got grnCnl param: "); Serial.println(server.arg("grnCnl"));
#endif
    byte newGreenCnl = atoi(server.arg("grnCnl").c_str());
    if (newGreenCnl != configGreenCnl) {
      setClockOptionGrnCnl(newGreenCnl);
#ifdef DEBUG
      Serial.print("I2C --> Set grnCnl: "); Serial.println(newGreenCnl);
#endif
    }
  }

  // -----------------------------------------------------------------------------

  if (server.hasArg("bluCnl")) {
#ifdef DEBUG
    Serial.print("Got bluCnl param: "); Serial.println(server.arg("bluCnl"));
#endif
    byte newBlueCnl = atoi(server.arg("bluCnl").c_str());
    if (newBlueCnl != configBlueCnl) {
      setClockOptionBluCnl(newBlueCnl);
#ifdef DEBUG
      Serial.print("I2C --> Set bluCnl: "); Serial.println(newBlueCnl);
#endif
    }
  }

  // -----------------------------------------------------------------------------

  if (server.hasArg("cycleSpeed")) {
#ifdef DEBUG
    Serial.print("Got cycleSpeed param: "); Serial.println(server.arg("cycleSpeed"));
#endif
    byte newCycleSpeed = atoi(server.arg("cycleSpeed").c_str());
    if (newCycleSpeed != configCycleSpeed) {
      setClockOptionCycleSpeed(newCycleSpeed);
#ifdef DEBUG
      Serial.print("I2C --> Set cycleSpeed: "); Serial.println(newCycleSpeed);
#endif
    }
  }

  // -----------------------------------------------------------------------------

  if (server.hasArg("blankMode")) {
#ifdef DEBUG
    Serial.print("Got blankMode param: "); Serial.println(server.arg("blankMode"));
#endif
    byte newBlankMode = atoi(server.arg("blankMode").c_str());
    if (newBlankMode != configBlankMode) {
      setClockOptionBlankMode(newBlankMode);
#ifdef DEBUG
      Serial.print("I2C --> Set blankMode: "); Serial.println(newBlankMode);
#endif
    }
  }

  // -----------------------------------------------------------------------------

  if (server.hasArg("slotsMode")) {
#ifdef DEBUG
    Serial.print("Got slotsMode param: "); Serial.println(server.arg("slotsMode"));
#endif
    byte newSlotsMode = atoi(server.arg("slotsMode").c_str());
    if (newSlotsMode != configSlotsMode) {
      setClockOptionSlotsMode(newSlotsMode);
#ifdef DEBUG
      Serial.print("I2C --> Set slotsMode: "); Serial.println(newSlotsMode);
#endif
    }
  }

  // -----------------------------------------------------------------------------

  // Get the options, put the result into variables called "config*"
  getClockOptionsFromI2C();

  String response_message = getHTMLHead();
  response_message += getNavBar();

  // form header
  response_message += getFormHead("Set Configuration");

  // 12/24 mode
  response_message += getRadioGroupHeader("12H/24H mode:");
  if (configHourMode == 0) {
    response_message += getRadioButton("12h24hMode", " 12H", "12h", false);
    response_message += getRadioButton("12h24hMode", " 24H", "24h", true);
  } else {
    response_message += getRadioButton("12h24hMode", " 12H", "12h", true);
    response_message += getRadioButton("12h24hMode", " 24H", "24h", false);
  }
  response_message += getRadioGroupFooter();

  // blank leading
  response_message += getRadioGroupHeader("Blank leading zero:");
  if (configBlankLead) {
    response_message += getRadioButton("blankLeading", "Blank", "blank", true);
    response_message += getRadioButton("blankLeading", "Show", "show", false);
  } else {
    response_message += getRadioButton("blankLeading", "Blank", "blank", false);
    response_message += getRadioButton("blankLeading", "Show", "show", true);
  }
  response_message += getRadioGroupFooter();
  //response_message += getCheckBox("blankLeadingZero", "on", "Blank leading zero on hours", (configBlankLead == 1));

  // Scrollback
  response_message += getRadioGroupHeader("Scrollback effect:");
  if (configScrollback) {
    response_message += getRadioButton("useScrollback", "On", "on", true);
    response_message += getRadioButton("useScrollback", "Off", "off", false);
  } else {
    response_message += getRadioButton("useScrollback", "On", "on", false);
    response_message += getRadioButton("useScrollback", "Off", "off", true);
  }
  response_message += getRadioGroupFooter();
  //response_message += getCheckBox("useScrollback", "on", "Use scrollback effect", (configScrollback == 1));

  // fade
  response_message += getRadioGroupHeader("Fade effect:");
  if (configUseFade) {
    response_message += getRadioButton("useFade", "On", "on", true);
    response_message += getRadioButton("useFade", "Off", "off", false);
  } else {
    response_message += getRadioButton("useFade", "On", "on", false);
    response_message += getRadioButton("useFade", "Off", "off", true);
  }
  response_message += getRadioGroupFooter();
  //response_message += getCheckBox("useFade", "on", "Use fade effect", (configUseFade == 1));

  // Suppress ACP
  response_message += getRadioGroupHeader("Suppress ACP when dimmed:");
  if (configSuppressACP) {
    response_message += getRadioButton("suppressACP", "On", "on", true);
    response_message += getRadioButton("suppressACP", "Off", "off", false);
  } else {
    response_message += getRadioButton("suppressACP", "On", "on", false);
    response_message += getRadioButton("suppressACP", "Off", "off", true);
  }
  response_message += getRadioGroupFooter();
  //response_message += getCheckBox("suppressACP", "on", "Suppress ACP when fully dimmed", (configSuppressACP == 1));

  // LDR
  response_message += getRadioGroupHeader("Use LDR:");
  if (configUseLDR) {
    response_message += getRadioButton("useLDR", "On", "on", true);
    response_message += getRadioButton("useLDR", "Off", "off", false);
  } else {
    response_message += getRadioButton("useLDR", "On", "on", false);
    response_message += getRadioButton("useLDR", "Off", "off", true);
  }
  response_message += getRadioGroupFooter();
  //response_message += getCheckBox("useLDR", "on", "Use LDR for dimming", (useLDR == 1));

  // Date format
  response_message += getDropDownHeader("Date format:", "dateFormat", false);
  response_message += getDropDownOption("0", "YY-MM-DD", (configDateFormat == 0));
  response_message += getDropDownOption("1", "MM-DD-YY", (configDateFormat == 1));
  response_message += getDropDownOption("2", "DD-MM-YY", (configDateFormat == 2));
  response_message += getDropDownFooter();

  // Day blanking
  response_message += getDropDownHeader("Day blanking:", "dayBlanking", true);
  response_message += getDropDownOption("0", "Never blank", (configDayBlanking == 0));
  response_message += getDropDownOption("1", "Blank all day on weekends", (configDayBlanking == 1));
  response_message += getDropDownOption("2", "Blank all day on week days", (configDayBlanking == 2));
  response_message += getDropDownOption("3", "Blank always", (configDayBlanking == 3));
  response_message += getDropDownOption("4", "Blank during selected hours every day", (configDayBlanking == 4));
  response_message += getDropDownOption("5", "Blank during selected hours on week days and all day on weekends", (configDayBlanking == 5));
  response_message += getDropDownOption("6", "Blank during selected hours on weekends and all day on week days", (configDayBlanking == 6));
  response_message += getDropDownOption("7", "Blank during selected hours on weekends only", (configDayBlanking == 7));
  response_message += getDropDownOption("8", "Blank during selected hours on week days only", (configDayBlanking == 8));
  response_message += getDropDownFooter();

  // Blank Mode
  response_message += getDropDownHeader("Blank Mode:", "blankMode", true);
  response_message += getDropDownOption("0", "Blank tubes only", (configBlankMode == 0));
  response_message += getDropDownOption("1", "Blank LEDs only", (configBlankMode == 1));
  response_message += getDropDownOption("2", "Blank tubes and LEDs", (configBlankMode == 2));
  response_message += getDropDownFooter();
  boolean hoursDisabled = (configDayBlanking < 4);

  
  // Proximity Blanking
  response_message += getRadioGroupHeader("Use Proximity Blanking");
  response_message += getRadioButton("useProximity", "On", "on", (configUseProximity==1));  
  response_message += getRadioButton("useProximity", "Off", "off",(configUseProximity==0));
  response_message += getRadioGroupFooter();

  // Blank hours from
  response_message += getNumberInput("Blank from:", "blankFrom", 0, 23, configBlankFrom, hoursDisabled);

  // Blank hours to
  response_message += getNumberInput("Blank to:", "blankTo", 0, 23, configBlankTo, hoursDisabled);

  // Fade steps
  response_message += getNumberInput("Fade steps:", "fadeSteps", 20, 200, configFadeSteps, false);

  // Scroll steps
  response_message += getNumberInput("Scroll steps:", "scrollSteps", 1, 40, configScrollSteps, false);

  // Back light
  response_message += getDropDownHeader("Back light:", "backLight", true);
  response_message += getDropDownOption("0", "Fixed RGB backlight, no dimming", (configBacklightMode == 0));
  response_message += getDropDownOption("1", "Pulsing RGB backlight, no dimming", (configBacklightMode == 1));
  response_message += getDropDownOption("2", "Cycling RGB backlight, no dimming", (configBacklightMode == 2));
  response_message += getDropDownOption("3", "Fixed RGB backlight, dims with ambient light", (configBacklightMode == 3));
  response_message += getDropDownOption("4", "Pulsing RGB backlight, dims with ambient light", (configBacklightMode == 4));
  response_message += getDropDownOption("5", "Cycling RGB backlight, dims with ambient light", (configBacklightMode == 5));
  response_message += getDropDownFooter();

  // RGB channels
  response_message += getNumberInput("Red intensity:", "redCnl", 0, 15, configRedCnl, false);
  response_message += getNumberInput("Green intensity:", "grnCnl", 0, 15, configGreenCnl, false);
  response_message += getNumberInput("Blue intensity:", "bluCnl", 0, 15, configBlueCnl, false);

  // Cycle speed
  response_message += getNumberInput("Backlight Cycle Speed:", "cycleSpeed", 2, 64, configCycleSpeed, false);

  // Slots Mode
  response_message += getDropDownHeader("Date Slots:", "slotsMode", true);
  response_message += getDropDownOption("0", "Don't use slots mode", (configSlotsMode == 0));
  response_message += getDropDownOption("1", "Scroll In, Scramble Out", (configSlotsMode == 1));
  response_message += getDropDownFooter();

  // form footer
  response_message += getSubmitButton("Set");

  response_message += "</form></div>";

  // all done
  response_message += getHTMLFoot();

  server.send(200, "text/html", response_message);
}

// ===================================================================================================================
// ===================================================================================================================

/* Called if requested page is not found */
void handleNotFound()
{
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET) ? "GET" : "POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";

  for (uint8_t i = 0; i < server.args(); i++)
  {
    message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
  }

  server.send(404, "text/plain", message);
}

// ===================================================================================================================
// ===================================================================================================================

/* Called if we need to have a local CSS */
void localCSSHandler()
{
  String message = ".navbar,.table{margin-bottom:20px}.nav>li,.nav>li>a,article,aside,details,figcaption,figure,footer,header,hgroup,main,menu,nav,section,summary{display:block}.btn,.form-control,.navbar-toggle{background-image:none}.table,label{max-width:100%}.sub-header{padding-bottom:10px;border-bottom:1px solid #eee}.h3,h3{font-size:24px}.table{width:100%}table{background-color:transparent;border-spacing:0;border-collapse:collapse}.table-striped>tbody>tr:nth-of-type(2n+1){background-color:#f9f9f9}.table>caption+thead>tr:first-child>td,.table>caption+thead>tr:first-child>th,.table>colgroup+thead>tr:first-child>td,.table>colgroup+thead>tr:first-child>th,.table>thead:first-child>tr:first-child>td,.table>thead:first-child>tr:first-child>th{border-top:0}.table>thead>tr>th{border-bottom:2px solid #ddd}.table>tbody>tr>td,.table>tbody>tr>th,.table>tfoot>tr>td,.table>tfoot>tr>th,.table>thead>tr>td,.table>thead>tr>th{padding:8px;line-height:1.42857143;vertical-align:top;border-top:1px solid #ddd}th{text-align:left}td,th{padding:0}.navbar>.container .navbar-brand,.navbar>.container-fluid .navbar-brand{margin-left:-15px}.container,.container-fluid{padding-right:15px;padding-left:15px;margin-right:auto;margin-left:auto}.navbar-inverse .navbar-brand{color:#9d9d9d}.navbar-brand{float:left;height:50px;padding:15px;font-size:18px;line-height:20px}a{color:#337ab7;text-decoration:none;background-color:transparent}.navbar-fixed-top{border:0;top:0;border-width:0 0 1px}.navbar-inverse{background-color:#222;border-color:#080808}.navbar-fixed-bottom,.navbar-fixed-top{border-radius:0;position:fixed;right:0;left:0;z-index:1030}.nav>li,.nav>li>a,.navbar,.navbar-toggle{position:relative}.navbar{border-radius:4px;min-height:50px;border:1px solid transparent}.container{width:750px}.navbar-right{float:right!important;margin-right:-15px}.navbar-nav{float:left;margin:7.5px -15px}.nav{padding-left:0;margin-bottom:0;list-style:none}.navbar-nav>li{float:left}.navbar-inverse .navbar-nav>li>a{color:#9d9d9d}.navbar-nav>li>a{padding-top:10px;padding-bottom:10px;line-height:20px}.nav>li>a{padding:10px 15px}.navbar-inverse .navbar-toggle{border-color:#333}.navbar-toggle{display:none;float:right;padding:9px 10px;margin-top:8px;margin-right:15px;margin-bottom:8px;background-color:transparent;border:1px solid transparent;border-radius:4px}button,select{text-transform:none}button{overflow:visible}button,html input[type=button],input[type=reset],input[type=submit]{-webkit-appearance:button;cursor:pointer}.btn-primary{color:#fff;background-color:#337ab7;border-color:#2e6da4}.btn{display:inline-block;padding:6px 12px;margin-bottom:0;font-size:14px;font-weight:400;line-height:1.42857143;text-align:center;white-space:nowrap;vertical-align:middle;-ms-touch-action:manipulation;touch-action:manipulation;cursor:pointer;-webkit-user-select:none;-moz-user-select:none;-ms-user-select:none;user-select:none;border:1px solid transparent;border-radius:4px}button,input,select,textarea{font-family:inherit;font-size:inherit;line-height:inherit}input{line-height:normal}button,input,optgroup,select,textarea{margin:0;font:inherit;color:inherit}.form-control,body{font-size:14px;line-height:1.42857143}.form-horizontal .form-group{margin-right:-15px;margin-left:-15px}.form-group{margin-bottom:15px}.form-horizontal .control-label{padding-top:7px;margin-bottom:0;text-align:right}.form-control{display:block;width:100%;height:34px;padding:6px 12px;color:#555;background-color:#fff;border:1px solid #ccc;border-radius:4px;-webkit-box-shadow:inset 0 1px 1px rgba(0,0,0,.075);box-shadow:inset 0 1px 1px rgba(0,0,0,.075);-webkit-transition:border-color ease-in-out .15s,-webkit-box-shadow ease-in-out .15s;-o-transition:border-color ease-in-out .15s,box-shadow ease-in-out .15s;transition:border-color ease-in-out .15s,box-shadow ease-in-out .15s}.col-xs-8{width:66.66666667%}.col-xs-3{width:25%}.col-xs-1,.col-xs-10,.col-xs-11,.col-xs-12,.col-xs-2,.col-xs-3,.col-xs-4,.col-xs-5,.col-xs-6,.col-xs-7,.col-xs-8,.col-xs-9{float:left}.col-lg-1,.col-lg-10,.col-lg-11,.col-lg-12,.col-lg-2,.col-lg-3,.col-lg-4,.col-lg-5,.col-lg-6,.col-lg-7,.col-lg-8,.col-lg-9,.col-md-1,.col-md-10,.col-md-11,.col-md-12,.col-md-2,.col-md-3,.col-md-4,.col-md-5,.col-md-6,.col-md-7,.col-md-8,.col-md-9,.col-sm-1,.col-sm-10,.col-sm-11,.col-sm-12,.col-sm-2,.col-sm-3,.col-sm-4,.col-sm-5,.col-sm-6,.col-sm-7,.col-sm-8,.col-sm-9,.col-xs-1,.col-xs-10,.col-xs-11,.col-xs-12,.col-xs-2,.col-xs-3,.col-xs-4,.col-xs-5,.col-xs-6,.col-xs-7,.col-xs-8,.col-xs-9{position:relative;min-height:1px;padding-right:15px;padding-left:15px}label{display:inline-block;margin-bottom:5px;font-weight:700}*{-webkit-box-sizing:border-box;-moz-box-sizing:border-box;box-sizing:border-box}body{font-family:\"Helvetica Neue\",Helvetica,Arial,sans-serif;color:#333}html{font-size:10px;font-family:sans-serif;-webkit-text-size-adjust:100%}";

  server.send(200, "text/css", message);
}

// ----------------------------------------------------------------------------------------------------
// ----------------------------------------- Network handling -----------------------------------------
// ----------------------------------------------------------------------------------------------------

/**
   Try to connect to the WiFi with the given credentials. Give up after 10 seconds or 20 retries
   if we can't get in.
*/
boolean connectToWLAN(const char* ssid, const char* password) {
  int retries = 0;
#ifdef DEBUG
  Serial.println("Connecting to WLAN");
#endif
  if (password && strlen(password) > 0 ) {
    WiFi.begin(ssid, password);
  } else {
    WiFi.begin(ssid);
  }

  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
#ifdef DEBUG
    Serial.print(".");
#endif
    retries++;
    if (retries > 20) {
      return false;
    }
  }

  return true;
}

/**
   Get the local time from the time zone server. Return the error description prefixed by "ERROR:" if something went wrong.
   Uses the global variable timeServerURL.
*/
String getTimeFromTimeZoneServer() {
  HTTPClient http;
  String payload;

  http.begin(timeServerURL);
  String espId = "";espId += ESP.getChipId();
  http.addHeader("ESP",espId);
  http.addHeader("ClientID",serialNumber);

  int httpCode = http.GET();

  // file found at server
  if (httpCode == HTTP_CODE_OK) {
    payload = http.getString();
    updatetimer=LONGUPDATE;     // 1 hour default between time updates
  } else {
#ifdef DEBUG
    Serial.printf("[HTTP] GET... failed, error: %s\n", http.errorToString(httpCode).c_str());
#endif
    if (httpCode > 0) {
      // RFC error codes don't have a string mapping
      payload = "ERROR: " + String(httpCode);
    } else {
      // ESP error codes have a string mapping
      payload = "ERROR: " + String(httpCode) + " ("+ http.errorToString(httpCode) + ")";
    }
    updatetimer=SHORTUPDATE;     // 60 sec default between time updates
  }    

  http.end();

  return payload;
}

// ----------------------------------------------------------------------------------------------------
// ------------------------------------------ EEPROM functions ----------------------------------------
// ----------------------------------------------------------------------------------------------------

String getSSIDFromEEPROM() {
#ifdef DEBUG
  Serial.println("Reading EEPROM ssid");
#endif
  String esid = "";
  for (int i = 0; i < 32; i++)
  {
    byte readByte = EEPROM.read(i);
    if (readByte == 0) {
      break;
    } else if ((readByte < 32) || (readByte == 0xFF)) {
      continue;
    }
    esid += char(readByte);
  }
#ifdef DEBUG
  Serial.print("Recovered SSID: ");
  Serial.println(esid);
#endif
  return esid;
}

String getPasswordFromEEPROM() {
#ifdef DEBUG
  Serial.println("Reading EEPROM password");
#endif

  String epass = "";
  for (int i = 32; i < 96; i++)
  {
    byte readByte = EEPROM.read(i);
    if (readByte == 0) {
      break;
    } else if ((readByte < 32) || (readByte == 0xFF)) {
      continue;
    }
    epass += char(EEPROM.read(i));
  }
#ifdef DEBUG
  Serial.print("Recovered password: ");
  Serial.println(epass);
#endif

  return epass;
}

void storeCredentialsInEEPROM(String qsid, String qpass) {
#ifdef DEBUG
  Serial.print("writing eeprom ssid, length ");
  Serial.println(qsid.length());
#endif
  for (int i = 0; i < 32; i++)
  {
    if (i < qsid.length()) {
      EEPROM.write(i, qsid[i]);
#ifdef DEBUG
      Serial.print("Wrote: ");
      Serial.println(qsid[i]);
#endif
    } else {
      EEPROM.write(i, 0);
    }
  }
#ifdef DEBUG
  Serial.print("writing eeprom pass, length ");
  Serial.println(qpass.length());
#endif
  for (int i = 0; i < 96; i++)
  {
    if ( i < qpass.length()) {
      EEPROM.write(32 + i, qpass[i]);
#ifdef DEBUG
      Serial.print("Wrote: ");
      Serial.println(qpass[i]);
#endif
    } else {
      EEPROM.write(32 + i, 0);
    }
  }

  EEPROM.commit();
}

String getTimeServerURLFromEEPROM() {
#ifdef DEBUG
  Serial.println("Reading time server URL");
#endif

  String eurl = "";
  for (int i = 96; i < (96 + 256); i++)
  {
    byte readByte = EEPROM.read(i);
    if (readByte == 0) {
      break;
    } else if ((readByte < 32) || (readByte == 0xFF)) {
      continue;
    }
    eurl += char(readByte);
  }
#ifdef DEBUG
  Serial.print("Recovered time server URL: ");
  Serial.println(eurl);
  Serial.println(eurl.length());
#endif

  if (eurl.length() == 0) {
#ifdef DEBUG
    Serial.println("Recovered blank time server URL: ");
    Serial.println("Returning default time server URL");
#endif
    eurl = DEFAULT_TIME_SERVER_URL;
  }

  return eurl;
}

void storeTimeServerURLInEEPROM(String timeServerURL) {
#ifdef DEBUG
  Serial.print("writing time server URL, length ");
  Serial.println(timeServerURL.length());
#endif
  for (int i = 0; i < 256; i++)
  {
    if (i < timeServerURL.length()) {
      EEPROM.write(96 + i, timeServerURL[i]);
#ifdef DEBUG
      Serial.print("Wrote: ");
      Serial.println(timeServerURL[i]);
#endif
    } else {
      EEPROM.write(96 + i, 0);
    }
  }

  EEPROM.commit();
}

void resetEEPROM() {
  wipeEEPROM();
  storeTimeServerURLInEEPROM(DEFAULT_TIME_SERVER_URL);
  storeCredentialsInEEPROM("","");
}

void wipeEEPROM() {
  for (int i = 0; i < 344; i++) {EEPROM.write(i, 0);}
  EEPROM.commit();
}



// ----------------------------------------------------------------------------------------------------
// ----------------------------------------- Utility functions ----------------------------------------
// ----------------------------------------------------------------------------------------------------

void toggleBlueLED() {
  blueLedState = ! blueLedState;
  digitalWrite(blueLedPin, blueLedState);
}

void setBlueLED(boolean newState) {
  digitalWrite(blueLedPin, newState);
}

/**
   Split a string based on a separator, get the element given by index
*/
String getValue(String data, char separator, int index)
{
  int found = 0;
  int strIndex[] = {0, -1};
  int maxIndex = data.length() - 1;
  for (int i = 0; i <= maxIndex && found <= index; i++) {
    if (data.charAt(i) == separator || i == maxIndex) {
      found++;
      strIndex[0] = strIndex[1] + 1;
      strIndex[1] = (i == maxIndex) ? i + 1 : i;
    }
  }
  return found > index ? data.substring(strIndex[0], strIndex[1]) : "";
}

/**
   Split a string based on a separator, get the element given by index, return an integer value
*/
int getIntValue(String data, char separator, int index) {
  String result = getValue(data, separator, index);
  return atoi(result.c_str());
}

// ----------------------------------------------------------------------------------------------------
// ------------------------------------------- I2C functions ------------------------------------------
// ----------------------------------------------------------------------------------------------------

/**
 * Send the time to the I2C slave. If the transmission went OK, return true, otherwise false.
 */
boolean sendTimeToI2C(String timeString) {

  int yearnow = getIntValue(timeString, ',', 0);
  byte monthnow = getIntValue(timeString, ',', 1);
  byte daynow = getIntValue(timeString, ',', 2);
  byte hournow = getIntValue(timeString, ',', 3);
  byte minutenow = getIntValue(timeString, ',', 4);
  byte secnow = getIntValue(timeString, ',', 5);

  byte yearAdjusted = (yearnow - 2000);

#ifdef DEBUG
  Serial.println("Sending time to I2C");
  Serial.println(timeString);
#endif

  Wire.beginTransmission(I2C_SLAVE_ADDR);
  Wire.write(I2C_TIME_UPDATE); // Command
#ifdef USE_NTP
  Wire.write(year()-2000);
  Wire.write(month());
  Wire.write(day());
  Wire.write(hour());
  Wire.write(minute());
  Wire.write(second());
#else
  Wire.write(yearAdjusted);
  Wire.write(monthnow);
  Wire.write(daynow);
  Wire.write(hournow);
  Wire.write(minutenow);
  Wire.write(secnow);
#endif
  int error = Wire.endTransmission();
  return (error == 0);
}

/**
   Get the options from the I2C slave. If the transmission went OK, return true, otherwise false.
*/
boolean getClockOptionsFromI2C() {
  int available = Wire.requestFrom(I2C_SLAVE_ADDR, 20);

#ifdef DEBUG
  Serial.print("I2C <-- Received bytes: ");
  Serial.println(available);
#endif
  if (available == 20) {

    byte receivedByte = Wire.read();
#ifdef DEBUG
    Serial.print("I2C <-- Got protocol header: ");
    Serial.println(receivedByte);
#endif

    if (receivedByte != 48) {
#ifdef DEBUG
      Serial.print("I2C Protocol ERROR! Expected header 48, but got: ");
      Serial.println(receivedByte);
#endif
      return false;
    }
    
    receivedByte = Wire.read();
#ifdef DEBUG
    Serial.print("I2C <-- Got hour mode: ");
    Serial.println(receivedByte);
#endif
    configHourMode = receivedByte;

    receivedByte = Wire.read();
#ifdef DEBUG
    Serial.print("I2C <-- Got blank lead: ");
    Serial.println(receivedByte);
#endif
    configBlankLead = receivedByte;

    receivedByte = Wire.read();
#ifdef DEBUG
    Serial.print("I2C <-- Got scrollback: ");
    Serial.println(receivedByte);
#endif
    configScrollback = receivedByte;

    receivedByte = Wire.read();
#ifdef DEBUG
    Serial.print("I2C <-- Got suppress ACP: ");
    Serial.println(receivedByte);
#endif
    configSuppressACP = receivedByte;

    receivedByte = Wire.read();
#ifdef DEBUG
    Serial.print("I2C <-- Got useFade: ");
    Serial.println(receivedByte);
#endif
    configUseFade = receivedByte;

    receivedByte = Wire.read();
#ifdef DEBUG
    Serial.print("I2C <-- Got date format: ");
    Serial.println(receivedByte);
#endif
    configDateFormat = receivedByte;

    receivedByte = Wire.read();
#ifdef DEBUG
    Serial.print("I2C <-- Got day blanking: ");
    Serial.println(receivedByte);
#endif
    configDayBlanking = receivedByte;

    receivedByte = Wire.read();
#ifdef DEBUG
    Serial.print("I2C <-- Got blank hour start: ");
    Serial.println(receivedByte);
#endif
    configBlankFrom = receivedByte;

    receivedByte = Wire.read();
#ifdef DEBUG
    Serial.print("I2C <-- Got blank hour end: ");
    Serial.println(receivedByte);
#endif
    configBlankTo = receivedByte;

    receivedByte = Wire.read();
#ifdef DEBUG
    Serial.print("I2C <-- Got fade steps: ");
    Serial.println(receivedByte);
#endif
    configFadeSteps = receivedByte;

    receivedByte = Wire.read();
#ifdef DEBUG
    Serial.print("I2C <-- Got scroll steps: ");
    Serial.println(receivedByte);
#endif
    configScrollSteps = receivedByte;

    receivedByte = Wire.read();
#ifdef DEBUG
    Serial.print("I2C <-- Got backlight mode: ");
    Serial.println(receivedByte);
#endif
    configBacklightMode = receivedByte;

    receivedByte = Wire.read();
#ifdef DEBUG
    Serial.print("I2C <-- Got red channel: ");
    Serial.println(receivedByte);
#endif
    configRedCnl = receivedByte;

    receivedByte = Wire.read();
#ifdef DEBUG
    Serial.print("I2C <-- Got green channel: ");
    Serial.println(receivedByte);
#endif
    configGreenCnl = receivedByte;

    receivedByte = Wire.read();
#ifdef DEBUG
    Serial.print("I2C <-- Got blue channel: ");
    Serial.println(receivedByte);
#endif
    configBlueCnl = receivedByte;

    receivedByte = Wire.read();
#ifdef DEBUG
    Serial.print("I2C <-- Got cycle speed: ");
    Serial.println(receivedByte);
#endif
    configCycleSpeed = receivedByte;

    receivedByte = Wire.read();
#ifdef DEBUG
    Serial.print("I2C <-- Got useLDR: ");
    Serial.println(receivedByte);
#endif
    configUseLDR = receivedByte;

    receivedByte = Wire.read();
#ifdef DEBUG
    Serial.print("I2C <-- Got blankMode: ");
    Serial.println(receivedByte);
#endif
    configBlankMode = receivedByte;

    receivedByte = Wire.read();
#ifdef DEBUG
    Serial.print("I2C <-- Got slotsMode: ");
    Serial.println(receivedByte);
#endif
    configSlotsMode = receivedByte;

  } else {
    // didn't get the right number of bytes
#ifdef DEBUG
    Serial.print("I2C <-- Got wrong number of bytes, expected 20, got: ");
    Serial.println(available);
#endif
  }
  
  int error = Wire.endTransmission();
  return (error == 0);
}

boolean sendIPAddressToI2C(IPAddress ip) {
  
#ifdef DEBUG
  Serial.println("Sending IP Address to I2C");
  Serial.print(ip[0]);
  Serial.print(".");
  Serial.print(ip[1]);
  Serial.print(".");
  Serial.print(ip[2]);
  Serial.print(".");
  Serial.print(ip[3]);
  Serial.println("");
#endif

  Wire.beginTransmission(I2C_SLAVE_ADDR);
  Wire.write(I2C_SHOW_IP_ADDR); // Command
  Wire.write(ip[0]);
  Wire.write(ip[1]);
  Wire.write(ip[2]);
  Wire.write(ip[3]);
  
  int error = Wire.endTransmission();
  return (error == 0);
}

boolean setClockOption12H24H(boolean newMode) {
  return setClockOptionBoolean(I2C_SET_OPTION_12_24, newMode);
}

boolean setClockOptionBlankLeadingZero(boolean newMode) {
  return setClockOptionBoolean(I2C_SET_OPTION_BLANK_LEAD, newMode);
}

boolean setClockOptionScrollback(boolean newMode) {
  return setClockOptionBoolean(I2C_SET_OPTION_SCROLLBACK, newMode);
}

boolean setClockOptionSuppressACP(boolean newMode) {
  return setClockOptionBoolean(I2C_SET_OPTION_SUPPRESS_ACP, newMode);
}

boolean setClockOptionUseFade(boolean newMode) {
  return setClockOptionBoolean(I2C_SET_OPTION_FADE, newMode);
}

boolean setClockOptionUseLDR(boolean newMode) {
  return setClockOptionBoolean(I2C_SET_OPTION_USE_LDR, newMode);
}

boolean setClockOptionDateFormat(byte newMode) {
  return setClockOptionByte(I2C_SET_OPTION_DATE_FORMAT, newMode);
}

boolean setClockOptionDayBlanking(byte newMode) {
  return setClockOptionByte(I2C_SET_OPTION_DAY_BLANKING, newMode);
}

boolean setClockOptionBlankFrom(byte newMode) {
  return setClockOptionByte(I2C_SET_OPTION_BLANK_START, newMode);
}

boolean setClockOptionBlankTo(byte newMode) {
  return setClockOptionByte(I2C_SET_OPTION_BLANK_END, newMode);
}

boolean setClockOptionFadeSteps(byte newMode) {
  return setClockOptionByte(I2C_SET_OPTION_FADE_STEPS, newMode);
}

boolean setClockOptionScrollSteps(byte newMode) {
  return setClockOptionByte(I2C_SET_OPTION_SCROLL_STEPS, newMode);
}

boolean setClockOptionBacklightMode(byte newMode) {
  return setClockOptionByte(I2C_SET_OPTION_BACKLIGHT_MODE, newMode);
}

boolean setClockOptionRedCnl(byte newMode) {
  return setClockOptionByte(I2C_SET_OPTION_RED_CHANNEL, newMode);
}

boolean setClockOptionGrnCnl(byte newMode) {
  return setClockOptionByte(I2C_SET_OPTION_GREEN_CHANNEL, newMode);
}

boolean setClockOptionBluCnl(byte newMode) {
  return setClockOptionByte(I2C_SET_OPTION_BLUE_CHANNEL, newMode);
}

boolean setClockOptionCycleSpeed(byte newMode) {
  return setClockOptionByte(I2C_SET_OPTION_CYCLE_SPEED, newMode);
}

boolean setClockOptionBlankMode(byte newMode) {
  return setClockOptionByte(I2C_SET_OPTION_BLANK_MODE, newMode);
}

boolean setClockOptionSlotsMode(byte newMode) {
  return setClockOptionByte(I2C_SET_OPTION_SLOTS_MODE, newMode);
}

/**
   Send the options from the I2C slave. If the transmission went OK, return true, otherwise false.
*/
boolean setClockOptionBoolean(byte option, boolean newMode) {
#ifdef DEBUG
  Serial.print("I2C --> setting boolean option: ");
  Serial.print(option);
  Serial.print(" with value: ");
  Serial.println(newMode);
#endif

  Wire.beginTransmission(I2C_SLAVE_ADDR);
  Wire.write(option);
  byte newOption;
  if (newMode) {
    newOption = 0;
  } else {
    newOption = 1;
  }
  Wire.write(newOption);
  int error = Wire.endTransmission();
  delay(10);
  return (error == 0);
}

/**
   Send the options from the I2C slave. If the transmission went OK, return true, otherwise false.
*/
boolean setClockOptionByte(byte option, byte newMode) {
#ifdef DEBUG
  Serial.print("I2C --> setting byte option: ");
  Serial.print(option);
  Serial.print(" with value: ");
  Serial.println(newMode);
#endif

  Wire.beginTransmission(I2C_SLAVE_ADDR);
  Wire.write(option);
  Wire.write(newMode);
  int error = Wire.endTransmission();
  delay(10);
  return (error == 0);
}

// ----------------------------------------------------------------------------------------------------
// ------------------------------------------- HTML functions -----------------------------------------
// ----------------------------------------------------------------------------------------------------

String getHTMLHead() {
  String header = "<!DOCTYPE html><html><head>";

  if (WiFi.status() == WL_CONNECTED) {
    header += "<link href=\"https://maxcdn.bootstrapcdn.com/bootstrap/3.3.6/css/bootstrap.min.css\" rel=\"stylesheet\" integrity=\"sha384-1q8mTJOASx8j1Au+a5WDVnPi2lkFfwwEAa8hDDdjZlpLegxhjVME1fgjWPGmkzs7\" crossorigin=\"anonymous\">";
    header += "<link href=\"http://www.open-rate.com/wl.css\" rel=\"stylesheet\" type=\"text/css\">";
    //header += "<script src=\"http://code.jquery.com/jquery-1.12.3.min.js\" integrity=\"sha256-aaODHAgvwQW1bFOGXMeX+pC4PZIPsvn2h1sArYOhgXQ=\" crossorigin=\"anonymous\"></script>";
    //header += "<script src=\"https://maxcdn.bootstrapcdn.com/bootstrap/3.3.6/js/bootstrap.min.js\" integrity=\"sha384-0mSbJDEHialfmuBBQP6A4Qrprq5OVfW37PRR3j5ELqxss1yVqOtnepnHVP9aJ7xS\" crossorigin=\"anonymous\"></script>";
  } else {
    header += "<link href=\"local.css\" rel=\"stylesheet\">";
  }
  header += "<title>Arduino Nixie Clock Time Module</title></head>";
  header += "<body>";
  return header;
}

/**
   Get the bootstrap top row navbar, including the Bootstrap links
*/
String getNavBar() {
  String navbar = "<nav class=\"navbar navbar-inverse navbar-fixed-top\">";
  navbar += "<div class=\"container-fluid\"><div class=\"navbar-header\"><button type=\"button\" class=\"navbar-toggle collapsed\" data-toggle=\"collapse\" data-target=\"#navbar\" aria-expanded=\"false\" aria-controls=\"navbar\">";
  navbar += "<span class=\"sr-only\">Toggle navigation</span><span class=\"icon-bar\"></span><span class=\"icon-bar\"></span><span class=\"icon-bar\"></span></button>";
  navbar += "<a class=\"navbar-brand\" href=\"#\">Arduino Nixie Clock Time Module</a></div><div id=\"navbar\" class=\"navbar-collapse collapse\"><ul class=\"nav navbar-nav navbar-right\">";
#ifdef USE_NTP
  navbar += "<li><a href=\"/\">Summary</a></li><li><li><a href=\"/wlan_config\">Configure WLAN settings</a></li><li><a href=\"/clockconfig\">Configure clock settings</a></li></ul></div></div></nav>";
#else
  navbar += "<li><a href=\"/\">Summary</a></li><li><a href=\"/time\">Configure Time Server</a></li><li><a href=\"/wlan_config\">Configure WLAN settings</a></li><li><a href=\"/clockconfig\">Configure clock settings</a></li></ul></div></div></nav>";
#endif
  return navbar;
} 

/**
   Get the header for a 2 column table
*/
String getTableHead2Col(String tableHeader, String col1Header, String col2Header) {
  String tableHead = "<div class=\"container\" role=\"main\"><h3 class=\"sub-header\">";
  tableHead += tableHeader;
  tableHead += "</h3><div class=\"table-responsive\"><table class=\"table table-striped\"><thead><tr><th>";
  tableHead += col1Header;
  tableHead += "</th><th>";
  tableHead += col2Header;
  tableHead += "</th></tr></thead><tbody>";

  return tableHead;
}

String getTableRow2Col(String col1Val, String col2Val) {
  String tableRow = "<tr><td>";
  tableRow += col1Val;
  tableRow += "</td><td>";
  tableRow += col2Val;
  tableRow += "</td></tr>";

  return tableRow;
}

String getTableRow2Col(String col1Val, int col2Val) {
  String tableRow = "<tr><td>";
  tableRow += col1Val;
  tableRow += "</td><td>";
  tableRow += col2Val;
  tableRow += "</td></tr>";

  return tableRow;
}

String getTableFoot() {
  return "</tbody></table></div></div>";
}

/**
   Get the header for an input form
*/
String getFormHead(String formTitle) {
  String tableHead = "<div class=\"container\" role=\"main\"><h3 class=\"sub-header\">";
  tableHead += formTitle;
  tableHead += "</h3><form class=\"form-horizontal\">";

  return tableHead;
}

/**
   Get the header for an input form
*/
String getFormFoot() {
  return "</form></div>";
}

String getHTMLFoot() {
  return "</body></html>";
}

String getRadioGroupHeader(String header) {
  String result = "<div class=\"form-group\"><label class=\"control-label col-xs-3\">";
  result += header;
  result += "</label>";
  return result;
}

String getRadioButton(String group_name, String text, String value, boolean checked) {
  String result = "<div class=\"col-xs-1\">";
  if (checked) {
    result += "<label class=\"radio-inline\"><input checked type=\"radio\" name=\"";
  } else {
    result += "<label class=\"radio-inline\"><input type=\"radio\" name=\"";
  }
  result += group_name;
  result += "\" value=\"";
  result += value;
  result += "\"> ";
  result += text;
  result += "</label></div>";
  return result;
}

String getRadioGroupFooter() {
  String result = "</div>";
  return result;
}

String getCheckBox(String checkbox_name, String value, String text, boolean checked) {
  String result = "<div class=\"form-group\"><div class=\"col-xs-offset-3 col-xs-9\"><label class=\"checkbox-inline\">";
  if (checked) {
    result += "<input checked type=\"checkbox\" name=\"";
  } else {
    result += "<input type=\"checkbox\" name=\"";
  }

  result += checkbox_name;
  result += "\" value=\"";
  result += value;
  result += "\"> ";
  result += text;
  result += "</label></div></div>";

  return result;
}

String getDropDownHeader(String heading, String group_name, boolean wide) {
  String result = "<div class=\"form-group\"><label class=\"control-label col-xs-3\">";
  result += heading;
  if (wide) {
    result += "</label><div class=\"col-xs-8\"><select class=\"form-control\" name=\"";
  } else {
    result += "</label><div class=\"col-xs-2\"><select class=\"form-control\" name=\"";
  }
  result += group_name;
  result += "\">";
  return result;
}

String getDropDownOption (String value, String text, boolean checked) {
  String result = "";
  if (checked) {
    result += "<option selected value=\"";
  } else {
    result += "<option value=\"";
  }
  result += value;
  result += "\">";
  result += text;
  result += "</option>";
  return result;
}

String getDropDownFooter() {
  return "</select></div></div>";
}

String getNumberInput(String heading, String input_name, byte minVal, byte maxVal, byte value, boolean disabled) {
  String result = "<div class=\"form-group\"><label class=\"control-label col-xs-3\" for=\"";
  result += input_name;
  result += "\">";
  result += heading;
  result += "</label><div class=\"col-xs-2\"><input type=\"number\" class=\"form-control\" name=\"";
  result += input_name;
  result += "\" id=\"";
  result += input_name;
  result += "\" min=\"";
  result += minVal;
  result += "\" max=\"";
  result += maxVal;
  result += "\" value=\"";
  result += value;
  if (disabled) {
    result += " disabled";
  }
  result += "\"></div></div>";

  return result;
}

String getNumberInputWide(String heading, String input_name, byte minVal, byte maxVal, byte value, boolean disabled) {
  String result = "<div class=\"form-group\"><label class=\"control-label col-xs-8\" for=\"";
  result += input_name;
  result += "\">";
  result += heading;
  result += "</label><div class=\"col-xs-2\"><input type=\"number\" class=\"form-control\" name=\"";
  result += input_name;
  result += "\" id=\"";
  result += input_name;
  result += "\" min=\"";
  result += minVal;
  result += "\" max=\"";
  result += maxVal;
  result += "\" value=\"";
  result += value;
  if (disabled) {
    result += " disabled";
  }
  result += "\"></div></div>";

  return result;
}

String getTextInput(String heading, String input_name, String value, boolean disabled) {
  String result = "<div class=\"form-group\"><label class=\"control-label col-xs-3\" for=\"";
  result += input_name;
  result += "\">";
  result += heading;
  result += "</label><div class=\"col-xs-2\"><input type=\"text\" class=\"form-control\" name=\"";
  result += input_name;
  result += "\" id=\"";
  result += input_name;
  result += "\" value=\"";
  result += value;
  if (disabled) {
    result += " disabled";
  }
  result += "\"></div></div>";

  return result;
}

String getTextInputWide(String heading, String input_name, String value, boolean disabled) {
  String result = "<div class=\"form-group\"><label class=\"control-label col-xs-3\" for=\"";
  result += input_name;
  result += "\">";
  result += heading;
  result += "</label><div class=\"col-xs-8\"><input type=\"text\" class=\"form-control\" name=\"";
  result += input_name;
  result += "\" id=\"";
  result += input_name;
  result += "\" value=\"";
  result += value;
  if (disabled) {
    result += " disabled";
  }
  result += "\"></div></div>";

  return result;
}

String getSubmitButton(String buttonText) {
  String result = "<div class=\"form-group\"><div class=\"col-xs-offset-3 col-xs-9\"><input type=\"submit\" class=\"btn btn-primary\" value=\"";
  result += buttonText;
  result += "\"></div></div>";
  return result;
}



// --------------------------------------------------------------------------------------------------
// -------------------------------------------NTP functions -----------------------------------------
// --------------------------------------------------------------------------------------------------

// send an NTP request to the time server at the given address
unsigned long sendNTPpacket(IPAddress& address)
{
  Serial.println("sending NTP packet...");
  // set all bytes in the buffer to 0
  memset(packetBuffer, 0, NTP_PACKET_SIZE);
  // Initialize values needed to form NTP request
  // (see URL above for details on the packets)
  packetBuffer[0] = 0b11100011;   // LI, Version, Mode
  packetBuffer[1] = 0;     // Stratum, or type of clock
  packetBuffer[2] = 6;     // Polling Interval
  packetBuffer[3] = 0xEC;  // Peer Clock Precision
  // 8 bytes of zero for Root Delay & Root Dispersion
  packetBuffer[12]  = 49;
  packetBuffer[13]  = 0x4E;
  packetBuffer[14]  = 49;
  packetBuffer[15]  = 52;

  // all NTP fields have been given values, now
  // you can send a packet requesting a timestamp:
  udp.beginPacket(address, 123); //NTP requests are to port 123
  udp.write(packetBuffer, NTP_PACKET_SIZE);
  udp.endPacket();
}

/**
 *       Code for NTP time server
         -----------------------
   Get the local time from the NTP server. Return the string "ERROR:" if something went wrong.
*/
String getTimeFromNtpServer() {
      String payload = "";
      if ( !ntp_ok ) {
        //get a random server from the pool if last request did not work else use previous IP address
        WiFi.hostByName(ntpServerName, timeServerIP); 
      }
      sendNTPpacket(timeServerIP); // send an NTP packet to a time server
      // wait to see if a reply is available
      delay(2000);
        int cb = udp.parsePacket();
  if (!cb) {
#ifdef DEBUG    
    Serial.println("no packet yet");
#endif    
    payload = "ERROR: ";
    ntp_ok = false;
    updatetimer=SHORTUPDATE;     // 60 sec default between time updates
    return payload;
  }
  else {
#ifdef DEBUG    
    Serial.print("packet received, length=");
    Serial.println(cb);
#endif    
    ntp_ok = true;
    // We've received a packet, read the data from it
    udp.read(packetBuffer, NTP_PACKET_SIZE); // read the packet into the buffer

    //the timestamp starts at byte 40 of the received packet and is four bytes,
    // or two words, long. First, esxtract the two words:

    unsigned long highWord = word(packetBuffer[40], packetBuffer[41]);
    unsigned long lowWord = word(packetBuffer[42], packetBuffer[43]);
    // combine the four bytes (two words) into a long integer
    // this is NTP time (seconds since Jan 1 1900):
    unsigned long secsSince1900 = highWord << 16 | lowWord;

    // now convert NTP time into everyday time:
    // Unix time starts on Jan 1 1970. In seconds, that's 2208988800:
    const unsigned long seventyYears = 2208988800UL;
    // subtract seventy years:
    unsigned long epoch = secsSince1900 - seventyYears;
    local = UK.toLocal(epoch, &tcr);    // Convert to local time
    setTime(local);                     // Set clock to local time

#ifdef DEBUG   
    // print NTP time
    Serial.print("Seconds since Jan 1 1900 = " );
    Serial.println(secsSince1900);
    // print Unix time:
    Serial.print("Unix time = ");
    Serial.println(epoch);

    // print the hour, minute and second:
    Serial.print("The UTC time is ");       // UTC is the time at Greenwich Meridian (GMT)
    Serial.print((epoch  % 86400L) / 3600); // print the hour (86400 equals secs per day)
    Serial.print(':');
    if ( ((epoch % 3600) / 60) < 10 ) {
      // In the first 10 minutes of each hour, we'll want a leading '0'
      Serial.print('0');
    }
    Serial.print((epoch  % 3600) / 60); // print the minute (3600 equals secs per minute)
    Serial.print(':');
    if ( (epoch % 60) < 10 ) {
      // In the first 10 seconds of each minute, we'll want a leading '0'
      Serial.print('0');
    }
    Serial.println(epoch % 60); // print the second

    Serial.print("Local time is ");
    if ( hour() < 10 ) {
      Serial.print('0');
    }
    Serial.print(hour());
    Serial.print(':');
    if ( minute() < 10 ) {
      Serial.print('0');
    }
    Serial.print(minute());
    Serial.print(':');
    Serial.print(second());
    Serial.print(" ");
    Serial.print(day());
    Serial.print(" ");
    Serial.print(months[month()]);
    Serial.print(" ");
    Serial.print(year()); 
    Serial.println();
#endif
    updatetimer=LONGUPDATE;     // 120 sec default between time updates
    return payload;
  }

}

// --------------------------------------------------------------------------------------------------
// ------------------------------------------- Weather functions ------------------------------------
// --------------------------------------------------------------------------------------------------
bool fetchweather(){
  HTTPClient http;
  http.begin(String(WEATHER_SERVER_URL)+String(WEATHER_LOCATION_ID)+"?res=3hourly&key="+String(WEATHER_API_KEY));   //Fetch weather forecast from UK Met Office
  // start connection and send HTTP header
  int httpCode = http.GET();
  // httpCode will be negative on error
  if(httpCode > 0) {
    // HTTP header has been send and Server response header has been handled
#ifdef DEBUG
    Serial.printf("[HTTP] GET... code: %d\n", httpCode);
#endif
   } else {
#ifdef DEBUG
      Serial.printf("[HTTP] GET... failed, error: %s\n", http.errorToString(httpCode).c_str());
#endif
      return false;
   }

   // Allocate a temporary memory pool
   DynamicJsonBuffer jsonBuffer(http.getSize()+64);
   WiFiClient * stream = http.getStreamPtr();
   JsonObject& root = jsonBuffer.parseObject(*stream);

   if (!root.success()) {
#ifdef DEBUG
     Serial.println("JSON parsing failed!");
#endif
     return false;
   }

   int minutestoday = elapsedSecsToday(UK.toUTC(now()))/60;
   int j[]= {0,0,0};   // Find which data series correspond to present period
   int i[]= {0,0,0};   // Array to address the json fields for 3 time periods
   
   int datasetsize[] = { root["SiteRep"]["DV"]["Location"]["Period"][0]["Rep"].size(),
                         root["SiteRep"]["DV"]["Location"]["Period"][1]["Rep"].size()  };

   
   for ( int l=1; l>=0; l--) {

    for ( int k=datasetsize[l]-1; k>=0; k-- ) {

      if ( minutestoday < 180+atoi(root["SiteRep"]["DV"]["Location"]["Period"][l]["Rep"][k]["$"] )) {
        j[0]=k;
        i[0]=l;        
      }
      
      if ( minutestoday+180 < l*1440 + 180+ atoi(root["SiteRep"]["DV"]["Location"]["Period"][l]["Rep"][k]["$"] )) {
        j[1]=k;
        i[1]=l;
      } 
      
      if ( minutestoday+360 < l*1440 + 180 + atoi(root["SiteRep"]["DV"]["Location"]["Period"][l]["Rep"][k]["$"] )) {
        j[2]=k;
        i[2]=l;
      }     
    }
   }

  

   
   strcpy( weather0.temp, root["SiteRep"]["DV"]["Location"]["Period"][i[0]]["Rep"][j[0]]["T"]);
   strcpy( weather0.winddirection, root["SiteRep"]["DV"]["Location"]["Period"][i[0]]["Rep"][j[0]]["D"]);
   strcpy( weather0.windspeed, root["SiteRep"]["DV"]["Location"]["Period"][i[0]]["Rep"][j[0]]["S"]);
   strcpy( weather0.rainpercent, root["SiteRep"]["DV"]["Location"]["Period"][i[0]]["Rep"][j[0]]["Pp"]);
   strcpy( weather0.weathertype, root["SiteRep"]["DV"]["Location"]["Period"][i[0]]["Rep"][j[0]]["W"]);
   strcpy( weather0.minssincemidnight, root["SiteRep"]["DV"]["Location"]["Period"][i[0]]["Rep"][j[0]]["$"]);

   strcpy( weather3.temp, root["SiteRep"]["DV"]["Location"]["Period"][i[1]]["Rep"][j[1]]["T"]);
   strcpy( weather3.winddirection, root["SiteRep"]["DV"]["Location"]["Period"][i[1]]["Rep"][j[1]]["D"]);
   strcpy( weather3.windspeed, root["SiteRep"]["DV"]["Location"]["Period"][i[1]]["Rep"][j[1]]["S"]);
   strcpy( weather3.rainpercent, root["SiteRep"]["DV"]["Location"]["Period"][i[1]]["Rep"][j[1]]["Pp"]);
   strcpy( weather3.weathertype, root["SiteRep"]["DV"]["Location"]["Period"][i[1]]["Rep"][j[1]]["W"]);
   strcpy( weather3.minssincemidnight, root["SiteRep"]["DV"]["Location"]["Period"][i[1]]["Rep"][j[1]]["$"]);

   strcpy( weather6.temp, root["SiteRep"]["DV"]["Location"]["Period"][i[2]]["Rep"][j[2]]["T"]);
   strcpy( weather6.winddirection, root["SiteRep"]["DV"]["Location"]["Period"][i[2]]["Rep"][j[2]]["D"]);
   strcpy( weather6.windspeed, root["SiteRep"]["DV"]["Location"]["Period"][i[2]]["Rep"][j[2]]["S"]);
   strcpy( weather6.rainpercent, root["SiteRep"]["DV"]["Location"]["Period"][i[2]]["Rep"][j[2]]["Pp"]);
   strcpy( weather6.weathertype, root["SiteRep"]["DV"]["Location"]["Period"][i[2]]["Rep"][j[2]]["W"]);
   strcpy( weather6.minssincemidnight, root["SiteRep"]["DV"]["Location"]["Period"][i[2]]["Rep"][j[2]]["$"]);  

   
   http.end();
   return true;
}


//******************************************************************************
// MQTT, Temp and Humidity Functions
//*******************************************************************************

void reconnect() {
  // Check if we're reconnected to MQTT server
  if (!mqttclient.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Create a random client ID
    String clientId = "ESP8266-NixieClock-";
    clientId += String(random(0xffff), HEX);
    // Attempt to connect
    if (mqttclient.connect(clientId.c_str())) {
      Serial.println("connected to MQTT");
      // Once connected, publish an announcement...
      mqttclient.publish("emon/nixie/connected", "1");
    } else {
      Serial.print("failed, rc=");
      Serial.print(mqttclient.state());
    }
  }
}

void readtemp(){   // Read the I2c temperature and humidity sensor

  float t = sht31.readTemperature();
  float h = sht31.readHumidity();

  if (isnan(t) || isnan(h)) {  // check if 'is not a number'
#ifdef DEBUG
    Serial.println("Failed to read temperature/humidity");
#endif
    sht31.reset();
  } else { 
    temp = t;
    humidity =h;
    // Ensure we are connected to MQTT
    if (!mqttclient.connected()) {
      reconnect();
    } else {
      // mqtt needs values to be ascii text
      char buffer [10];
      dtostrf( temp,5,2,buffer);
      mqttclient.publish("emon/nixie/temperature", buffer);
      dtostrf( humidity,5,2,buffer);
      mqttclient.publish("emon/nixie/humidity", buffer);
    }
  }
}


