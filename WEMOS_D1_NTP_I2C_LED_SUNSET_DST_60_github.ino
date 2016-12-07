#include <FS.h>
#include <TimeLord.h>
#include <Timezone.h>
#include <ThingSpeak.h>

#include <RtcDS3231.h>

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <ESP8266mDNS.h>

#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <DHT.h>

char ssid[] = "XXXXXXXXXXX";  //  your network SSID (name)
char pass[] = "xxxxxxxxxxx"; // your network password

unsigned int localPort = 2390;      // local port to listen for UDP packets

WiFiServer server(80);

/* Don't hardwire the IP address or we won't get the benefits of the pool.
    Lookup the IP address for the host name instead */
//IPAddress timeServer(129, 6, 15, 28); // time.nist.gov NTP server
IPAddress timeServerIP; // time.nist.gov NTP server address
// const char* ntpServerName = "time.nist.gov";
const char* ntpServerName = "time.nist.gov";

const int NTP_PACKET_SIZE = 48; // NTP time stamp is in the first 48 bytes of the message

byte packetBuffer[ NTP_PACKET_SIZE]; //buffer to hold incoming and outgoing packets

// A UDP instance to let us send and receive packets over UDP
WiFiUDP udp;

//HU Central Europen Time Zone (Budapest, Hungary)
TimeChangeRule myDST = {"CEST", Last, Sun, Mar, 2, +120};    //Daylight time = UTC + 2 hours
TimeChangeRule mySTD = {"EST", Last, Sun, Oct, 3, +60};     //Standard time = UTC + 1 hours
Timezone myTZ(myDST, mySTD);

TimeChangeRule *tcr;        //pointer to the time change rule
time_t hungary, utc;

// ------------------------ variables ---------------------------------------------------------------------------------------------------------------
unsigned long waitUntil = 0;
int timeZoneOffset;

//String webPage = "";

// Budapest - Home
float const LONGITUDE = 19.117506;
float const LATITUDE = 47.561406;

boolean isBackLight = false;
boolean isSoilOk = false;

int bulbSmall = 25;
int bulbBig = 50;
int bulbUVB = 13;

int LED = 3; // Power Led / Dry Indicator at Digital PIN D8
int ledState = LOW;
long onTime = 700;           // milliseconds of on-time
long offTime = 500;          // milliseconds of off-time

unsigned long previousMillis = 0;
const long secInterval = 1000;

int brightness = 1;    // how bright the LED is
int rtcBrightness = 10;    // how bright the LED is
int fadeAmount = 5;    // how many points to fade the LED by

int optimalGephazTemperature = 40; // control-room

int optimalFutesTemperature = 55; // furnace temp
int optimalFutesHumidity = 10; // furnace hum

int optimalBelsoTemperature = 35; // cave temp
int optimalBelsoHumidity = 55; // cave hum

int optimalKifutoTemperature = 30; // runway temp
int optimalKifutoHumidity = 40; // runway hum

float Belso1Temp;
float Belso2Temp;
float avgTemp;
float avgHum;
int avgTempRound;

int SENSE = 0; // Soil Sensor input at Analog PIN A0
int value = 0;

int uvHours = 8;
int sunSetOffset = 1;
int sunRiseOffset = 1;
int lcdOffset = 30;
int uvOffset = 10;

int sunRise_Hour;
int sunRise_Minute;
int sunSet_Hour;
int sunSet_Minute;

// ThingSpeak Settings
char thingSpeakAddress[] = "api.thingspeak.com";
String thingtweetAPIKey = "XXXXXX";
String writeAPIKey = "xxxxxxx";

// Variable Setup
long lastConnectionTime = 0;
boolean lastConnected = false;
int failedCounter = 0;

// open client
WiFiClient client;

// ------------------------ define ----------------------------------------------------------------------------------------------------------------
RtcDS3231 Rtc;

/*
  pin
  00 RELAY1_UVB
  01 RX
  02 RELAY3_25W
  03 TX / LED
  04 SDA
  05 SCL
  12 DHT22 runway
  13 DHT22 cave1
  14 DHT22 furnace
  15 dht22 cave2
  16 RELAY2_50W
*/

#define countof(a) (sizeof(a) / sizeof(a[0]))

#define RELAY1_UVB  0   
#define RELAY2_50W  2   
#define RELAY3_25W  16  

#define interval 60000

#define DHTPIN1 14 //14pin DHT22 - furnace
#define DHTPIN2 13 //13pin DHT22 - cave 1
#define DHTPIN3 15 //15pin DHT22 - cave 2
#define DHTPIN4 12 //12pin DHT22 - runway

#define DHTTYPE DHT22 // DHT 22 (AM2302)
//#define DHT4TYPE DHT11 // DHT 11
//#define DHTTYPE DHT22 // DHT 22 (AM2302)
//#define DHTTYPE DHT21 // DHT 21 (AM2301)
DHT dht1(DHTPIN1, DHTTYPE); // furnace
DHT dht2(DHTPIN2, DHTTYPE);
DHT dht3(DHTPIN3, DHTTYPE);
DHT dht4(DHTPIN4, DHTTYPE); // runway

LiquidCrystal_I2C lcd(0x27, 2, 1, 0, 4, 5, 6, 7, 3, POSITIVE); // Set the LCD I2C address

// ------------------------ Wifi connect ------------------------------------------------------------------------------------------------------------
void wifiConnect()
{
  Serial.begin(115200);
  //delay(10);

  // We start by connecting to a WiFi network
  Serial.print("Connecting to ");
  Serial.println(ssid);
  WiFi.begin(ssid, pass);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");

  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());

  Serial.println("Starting UDP");
  udp.begin(localPort);
  Serial.print("Local port: ");
  Serial.println(udp.localPort());
}

// ------------------------ NTP update --------------------------------------------------------------------------------------------------------------
void ntpUpdate()
{
  //get a random server from the pool
  WiFi.hostByName(ntpServerName, timeServerIP);

  sendNTPpacket(timeServerIP); // send an NTP packet to a time server
  // wait to see if a reply is available
  delay(1000);

  int cb = udp.parsePacket();
  if (!cb) {
    Serial.println("no packet yet");
    //  sendNTPpacket(timeServerIP); // send an NTP packet to a time server
    }
  else {
    Serial.print("packet received, length=");
    Serial.println(cb);
    // We've received a packet, read the data from it
    udp.read(packetBuffer, NTP_PACKET_SIZE); // read the packet into the buffer

    //the timestamp starts at byte 40 of the received packet and is four bytes,
    // or two words, long. First, esxtract the two words:

    unsigned long highWord = word(packetBuffer[40], packetBuffer[41]);
    unsigned long lowWord = word(packetBuffer[42], packetBuffer[43]);
    // combine the four bytes (two words) into a long integer
    // this is NTP time (seconds since Jan 1 1900):
    unsigned long secsSince1900 = highWord << 16 | lowWord;
    Serial.print("Seconds since Jan 1 1900 = " );
    Serial.println(secsSince1900);

    // now convert NTP time into everyday time:
    Serial.print("Unix time = ");
    // Unix time starts on Jan 1 1970. In seconds, that's 2208988800:
    const unsigned long seventyYears = 2208988800UL;
    // subtract seventy years:
    unsigned long epoch = secsSince1900 - seventyYears;
    // unsigned long epoch = secsSince1900;
    // print Unix time:
    // Serial.println(epoch);
    // Europe/Budapest (CEST) time trick
    // epoch = epoch + 7200;

    // print the hour, minute and second:
    Serial.print("Europe/Budapest (CEST) - NTP: ");  // UTC is the time at Greenwich Meridian (GMT)
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

    /*  seconds = (epoch % 60);
        minutes = ((epoch  % 3600) / 60);
        hours = ((epoch  % 86400L) / 3600);*/

    /*RtcDateTime compiled = RtcDateTime(__DATE__, __TIME__);
      printDateTime(compiled);
      Serial.println(); */

    if (!Rtc.IsDateTimeValid())
    {
      // Common Cuases:
      //    1) first time you ran and the device wasn't running yet
      //    2) the battery on the device is low or even missing

      Serial.println("RTC lost confidence in the DateTime!");
      updateTwitterStatus("RTC lost confidence...");

      // following line sets the RTC to the date & time this sketch was compiled
      // it will also reset the valid flag internally unless the Rtc device is
      // having an issue

      //Rtc.SetDateTime(epoch - 946684800);
      Rtc.SetDateTime(epoch);
    }

    if (!Rtc.GetIsRunning())
    {
      Serial.println("RTC was not actively running, starting now");
      Rtc.SetIsRunning(true);
      updateTwitterStatus("RTC is starting now...");
    }

    //int correctedNTP = epoch - 946684800;
    int correctedNTP = epoch;

    Serial.print("RTC:   ");
    Serial.println(Rtc.GetDateTime());

    Serial.print("NTP:   ");
    Serial.println(correctedNTP);

    Serial.print("EPOCH: ");
    Serial.println(epoch);

    if (Rtc.GetDateTime() < correctedNTP)
      //if (Rtc.GetDateTime() > correctedNTP)
    {
      Serial.println("RTC is older than NTP time!  (Updating DateTime)");
      Rtc.SetDateTime(correctedNTP);
      updateTwitterStatus("RTC update...");

    }
    else if (Rtc.GetDateTime() > correctedNTP)
      //else if (Rtc.GetDateTime() < correctedNTP)
    {
      Serial.println("RTC is newer than NTP time. (this is expected)");
    }
    else if (Rtc.GetDateTime() == correctedNTP)
    {
      Serial.println("RTC is the same as NTP time! (not expected but all is fine)");
    }

    // never assume the Rtc was last configured by you, so
    // just clear them to your needed state
    Rtc.Enable32kHzPin(false);
    Rtc.SetSquareWavePin(DS3231SquareWavePin_ModeNone);
  }
}

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

// ------------------------ OTA ------------------------------------------------------------------------------------------------------------
void updateOTA()
{
  // Port defaults to 8266
  // ArduinoOTA.setPort(8266);

  // Hostname defaults to esp8266-[ChipID]
  ArduinoOTA.setHostname("Terrarium");

  // No authentication by default
  ArduinoOTA.setPassword((const char *)"PASSWORD");

  ArduinoOTA.onStart([]() {
    Serial.println("Start");
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });
  ArduinoOTA.begin();
  Serial.println("Ready");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
}

// ------------------------ SpIFFS -------------------------------------------------------------------------------------------------------
void spiffs() {

  bool ok = SPIFFS.begin();
  if (ok) {
    Serial.println("ok");
    bool exist = SPIFFS.exists("/css/bootstrap.min.css");

    if (exist) {
      Serial.println("The file exists!");

      File f = SPIFFS.open("/css/bootstrap.min.css", "r");
      if (!f) {
        Serial.println("Some thing went wrong trying to open the file...");
      }
      else {
        int s = f.size();
        Serial.printf("Size=%d\r\n", s);


        // USE THIS DATA VARIABLE

        String data = f.readString();
        Serial.println(data);

        f.close();
      }
    }
    else {
      Serial.println("No such file found.");
    }
  }
}

// ------------------------ webserver-------------------------------------------------------------------------------------------------------
void webServer(time_t t, char *tz)
{
  int err;
  RtcTemperature temp = Rtc.GetTemperature();

  sun(hungary, tcr -> abbrev);
  Serial.println();

  //furnace
  float t1 = dht1.readTemperature(); // t temperature in Celsius
  float h1 = dht1.readHumidity();//h humidity

  //cave 1
  float t2 = dht2.readTemperature(); // t temperature in Celsius
  float h2 = dht2.readHumidity();//h humidity

  //cave 1
  // until we have only 1 pcs cave sensor
  float t3 = dht2.readTemperature(); // t temperature in Celsius
  float h3 = dht2.readHumidity(); //h humidity

  //cave avg
  avgTemp = ((t2 + t3) / 2);
  avgHum = ((h2 + h3) / 2);

  //runway
  float t4 = dht4.readTemperature(); // t temperature in Celsius
  float h4 = dht4.readHumidity();//h humidity

  WiFiClient client = server.available();
  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: text/html");
  client.println("Connection: close");  // the connection will be closed after completion of the response
  //client.println("Refresh: 60");  // refresh the page automatically every 60 sec
  client.println();
  client.println("<!DOCTYPE html>");
  client.println("<html xmlns='http://www.w3.org/1999/xhtml'>");
  client.println("<head>\n<meta charset='UTF-8'>");
  client.println("<title>Smart terrarium</title>");
  client.println("</head>\n<body>");
  client.print("<H2>Smart Terrarium");
  client.print("&nbsp;(");

  client.print(year(t));
  client.print(".");
  client.print(monthShortStr(month(t)));
  client.print(".");
  if (day(t) < 10)
  {
    client.print("0");
  }
  client.print(day(t));
  client.print(".&nbsp;-&nbsp;");

  if (hour(t) < 10)
  {
    client.print("0");
  }
  client.print(hour(t));
  client.print(":");
  if (minute(t) < 10)
  {
    client.print("0");
  }
  client.print(minute(t));

  client.print("&nbsp;");
  client.print(tcr -> abbrev);
  client.println(")</H2>");

  client.print("<H3>");

  client.print("Sunrise: ");
  if (sunRise_Hour < 10)
  {
    client.print("0");
  }
  client.print(sunRise_Hour);
  client.print(":");
  if (sunRise_Minute < 10)
  {
    client.print("0");
  }
  client.println(sunRise_Minute);

  client.print("<br>");
  client.print("Sunset: ");
  if (sunSet_Hour < 10)
  {
    client.print("0");
  }
  client.print(sunSet_Hour);
  client.print(":");
  if (sunSet_Minute < 10)
  {
    client.print("0");
  }
  client.println(sunSet_Minute);

  client.print("</H3>");

  client.println("<H3>Relay status</H3>");

  client.print("<p>LCD&nbsp;&nbsp;");
  if (isBackLight == true)
  {
    client.println("<button disabled>ON</button></a>&nbsp;<a href=\"#\"><button type=\"button\" class=\"btn btn - danger\">OFF</button></a></p>");
  }
  else
  {
    client.println("<button>ON </button></a>&nbsp;<a href=\"#\"><button disabled>OFF</button></a></p>");
  }

  client.print("<p>UVB&nbsp;&nbsp;");
  if (digitalRead(RELAY1_UVB) == HIGH)
  {
    client.println("<button disabled id=\"0\" class=\"relay\">ON</button></a>&nbsp;<a href=\"#\"><button id=\"0\" class=\"relay\">OFF</button></a></p>");
  }
  else
  {
    client.println("<button id=\"0\" class=\"relay\">ON</button></a>&nbsp;<a href=\"#\"><button disabled id=\"0\" class=\"relay\">OFF</button></a></p>");
  }

  client.print("<p>25W&nbsp;&nbsp;");
  if (digitalRead(RELAY3_25W) == HIGH)
  {
    client.println("<button disabled id=\"16\" class=\"relay\">ON</button></a>&nbsp;<a href=\"#\"><button id=\"16\" class=\"relay\">OFF</button></a></p>");
  }
  else
  {
    client.println("<button id=\"16\" class=\"relay\">ON</button></a>&nbsp;<a href=\"#\"><button disabled id=\"16\" class=\"relay\">OFF</button></a></p>");
  }

  client.print("<p>50W&nbsp;&nbsp;");
  if (digitalRead(RELAY2_50W) == HIGH)
  {
    client.println("<button disabled id=\"2\" class=\"relay\">ON</button></a>&nbsp;<a href=\"#\"><button id=\"2\" class=\"relay\">OFF</button></a></p>");
  }
  else
  {
    client.println("<button id=\"2\" class=\"relay\">ON</button></a>&nbsp;<a href=\"#\"><button disabled id=\"2\" class=\"relay\">OFF</button></a></p>");
  }

  client.println("<H3>Sensor status</H3>");
  client.println("<pre>");

  client.print("Control temperature:&nbsp;");
  client.print(temp.AsFloat(), 1);
  client.print("°C");
  client.println("<br>");

  client.print("Furnace temperature:&nbsp;");
  client.print((float)t1, 1);
  client.println("°C");
  client.print("Furnace humidity:&nbsp;");
  client.print((float)h1, 1);
  client.println("%");
  client.println("<br>");

  client.print("Cave1 temperature:&nbsp;");
  client.print((float)t2, 1);
  client.println("°C");
  client.print("Cave1 humidity:&nbsp;");
  client.print((float)h2, 1);
  client.println("%");
  client.println("<br>");

  client.print("Cave2 temperature:&nbsp;");
  client.print((float)t2, 1);
  client.println("°C");
  client.print("Cave2 humidity:&nbsp;");
  client.print((float)h2, 1);
  client.println("%");
  client.println("<br>");

  client.print("<b>Avg temperature:&nbsp;");
  client.print((float)avgTemp, 1);
  client.println("°C");
  client.print("Avg humidity:&nbsp;");
  client.print((float)avgHum, 1);
  client.println("%</b>");
  client.println("<br>");

  client.print("Runway temperature:&nbsp;");
  client.print((float)t4, 1);
  client.println("°C");
  client.print("Runway humidity:&nbsp;");
  client.print((float)h4, 1);
  client.println("%");
  client.println("</pre>");
  client.println("<br>");

  client.println("<a href=\"https://www.twitter.com/smartterrarium\">https://www.twitter.com/smartterrarium</a><br>");
  client.println("<a href=\"https://thingspeak.com/channels/126060\">https://thingspeak.com/channels/126060</a>");
  client.print("<br><br>");
  client.print("<iframe width=\"450\" height=\"260\" style=\"border: 1px solid #cccccc;\" src=\"https://thingspeak.com/channels/126060/charts/1?bgcolor=%23ffffff&color=%23d62020&dynamic=true&results=120&round=0&title=Terrarium+Cave&type=line&yaxis=Avg+Temperature+%28%E2%84%83%29\"></iframe>");
  client.print("</body>\n</html>");
}

// ------------------------ Soil moisture warning -------------------------------------------------------------------------------------
void soilChk (time_t t, char *tz)
{
  if (hour(t) * 60 + minute(t) >= sunRise_Hour * 60 + sunRise_Minute && hour(t) * 60 + minute(t) <= sunSet_Hour * 60 + sunSet_Minute) //between 6:00AM and 8:00PM
  {
    if (isSoilOk == false)
    {
      // check to see if it's time to change the state of the LED
      unsigned long currentMillis = millis();

      if ((ledState == HIGH) && (currentMillis - previousMillis >= onTime))
      {
        ledState = LOW;  // Turn it off
        previousMillis = currentMillis;  // Remember the time
        digitalWrite(LED, ledState);  // Update the actual LED
      }
      else if ((ledState == LOW) && (currentMillis - previousMillis >= offTime))
      {
        ledState = HIGH;  // turn it on
        previousMillis = currentMillis;   // Remember the time
        digitalWrite(LED, ledState);   // Update the actual LED
      }
    }
  }
}
// ------------------------ Soil moisture check -------------------------------------------------------------------------------------------
void soilMoisture (time_t t, char *tz)
{
  if (minute(t) % 30 == 0 ) //every 30. minutes
  {

    value = analogRead(SENSE);
    value = value / 10;
    //Serial.println(value);
    if (value < 50)
    {
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Talajnedvesseg");
      lcd.setCursor(0, 1);
      lcd.print("optimalis");
      Serial.print("Talaj nedvesség optimális, ");
      Serial.println(value);
      //updateTwitterStatus("Soil moisture is fine");
      isSoilOk = true;
    }
    else
    {
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Talajnedvesseg");
      lcd.setCursor(0, 1);
      lcd.print("alacsony");
      Serial.print("Talaj nedvesség alacsony, ");
      Serial.println(value);
      updateTwitterStatus("Soil moisture is low");
      isSoilOk = false;
    }
    delay(5000);
  }
}



// ------------------------ Terrarium control setup --------------------------------------------------------------------------------------------
void sun(time_t t, char *tz)
{
    TimeLord tardis;
    tardis.TimeZone(60); // tell TimeLord what timezone your RTC is synchronized to. -- ITT LEHET GOND, HA NEM PONTOS
    tardis.DstRules(3, 4, 10, 4, 60);
    // You can ignore DST as long as the RTC never changes back and forth between DST and non-DST
    tardis.Position(LATITUDE, LONGITUDE); // tell TimeLord where in the world we are

    byte today[] = {  0, 0, 12, day(t), month(t), year(t)    }; // store today's date (at noon) in an array for TimeLord to use

    if (tardis.SunRise(today)) // if the sun will rise today (it might not, in the [ant]arctic)
    {
      sunRise_Hour = today[tl_hour];
      sunRise_Minute = today[tl_minute];
    }

    if (tardis.SunSet(today)) // if the sun will set today (it might not, in the [ant]arctic)
    {
      sunSet_Hour = today[tl_hour];
      sunSet_Minute = today[tl_minute];
    }
}


// ------------------------ Sunrise - Sunset -------------------------------------------------------------------------------------------------------
void sunInfo (time_t t, char *tz, int show)
{
  int *p = &show;
  sun(hungary, tcr -> abbrev); //xxx
  
  Serial.println();

  if (*p == 0)
  {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Sunrise: ");
    lcdPrintI00(sunRise_Hour);
    lcdPrintDigits(sunRise_Minute);
    lcd.setCursor(0, 1);
    lcd.print("Sunset:  ");
    lcdPrintI00(sunSet_Hour);
    lcdPrintDigits(sunSet_Minute);

    Serial.print("Napfelkelte: ");
    Serial.print(sunRise_Hour);
    Serial.print(":");
    Serial.println(sunRise_Minute);
    Serial.print("Naplemente: ");
    Serial.print(sunSet_Hour);
    Serial.print(":");
    Serial.println(sunSet_Minute);
  }
  else if (*p == 1)
  {
    client.print("Sunrise:&nbsp;");
    client.print(sunRise_Hour);
    client.print(":");
    client.println(sunRise_Minute);
    client.print("sunset:&nbsp;&nbsp;");
    client.print(sunSet_Hour);
    client.print(":");
    client.println(sunSet_Minute);
    client.println(*p);
  }
}

// ------------------------ Heating -------------------------------------------------------------------------------------------------------
void showFutesTemp ()
{
  float t1 = dht1.readTemperature(); // t-be olvassa be a hőmérséklet értékét Celsiusban
  float h1 = dht1.readHumidity();//h a páratartalom értékét adja

  int t1_round = roundf(t1); // t temperature in Celsius
  int h1_round = roundf(h1);//h hum

  if (isnan(t1) || isnan(h1))
  {
    delay(2000);
    t1_round = roundf(dht1.readTemperature()); // t temperature in Celsius
    h1_round = roundf(dht1.readHumidity());//h hum

    if (isnan(t1) || isnan(h1))
    {
      Serial.println("Failed to read from DHT1 #1");
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Kazan:  N/A");
      lcd.setCursor(0, 1);
      lcd.print("Parat.: N/A");
      analogWrite(LED, 20);
    }
    else
    {
      Serial.print("Futes temp:");
      Serial.print(t1);
      Serial.print(" / ");
      Serial.println(t1_round);
      lcd.clear();
      lcd.print("Kazan:  ");// a cave1 temp to LCD
      lcd.print(t1_round);//
      lcd.print((char)223); //degree sign
      lcd.print("C");

      if (t1 <= optimalFutesTemperature + 5)
      {
        lcd.setCursor(14, 0);
        lcd.print("OK");
      }
      else
      {
        lcd.setCursor(14, 0);
        lcd.print("HI");
      }

      lcd.setCursor(0, 1);
      lcd.print("Parat.: ");
      lcd.print(h1_round);
      lcd.print("%");

      if (h1 <= optimalFutesHumidity + 10)
      {
        lcd.setCursor(14, 1);
        lcd.print("OK");
      }
      else
      {
        lcd.setCursor(14, 1);
        lcd.print("HI");
      }
    }
  }
  else
  {
    Serial.print("Kazan temp:");
    Serial.print(t1);
    Serial.print(" / ");
    Serial.println(t1_round);
    lcd.clear();
    lcd.print("Kazan:  ");// a cave1 temp to LCD
    lcd.print(t1_round);//
    lcd.print((char)223); //degree sign
    lcd.print("C");

    if (t1 <= optimalFutesTemperature + 5)
    {
      lcd.setCursor(14, 0);
      lcd.print("OK");
    }
    else
    {
      lcd.setCursor(14, 0);
      lcd.print("HI");
    }

    lcd.setCursor(0, 1);
    lcd.print("Parat.: ");
    lcd.print(h1_round);
    lcd.print("%");

    if (h1 <= optimalFutesHumidity + 10)
    {
      lcd.setCursor(14, 1);
      lcd.print("OK");
    }
    else
    {
      lcd.setCursor(14, 1);
      lcd.print("HI");
    }
    delay(2000);
  }
}

// ------------------------ Cave 1 temp -------------------------------------------------------------------------------------------------------
void showBelso1Temp ()
{
  float t2 = dht2.readTemperature(); // t temperature celsius
  float h2 = dht2.readHumidity();//h humidity

  int t2_round = roundf(t2); // t temperature celsius
  int h2_round = roundf(h2);//h humidity

  if (isnan(t2) || isnan(h2))
  {
    Serial.println("Failed to read from DHT2 #2");
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Belso1: N/A");
    lcd.setCursor(0, 1);
    lcd.print("Parat.: N/A");
  }
  else
  {
    Serial.print("Belso1 temp:");
    Serial.print(t2);
    Serial.print(" / ");
    Serial.println(t2_round);
    lcd.clear();
    lcd.print("Belso1: ");// cave1 temp to LCD
    lcd.print(t2_round);//
    lcd.print((char)223); //degree sign
    lcd.print("C");

    if ((optimalBelsoTemperature - 3 <= t2) && (t2 <= optimalBelsoTemperature + 5))
    {
      lcd.setCursor(14, 0);
      lcd.print("OK");
    }
    else if (t2 >= optimalBelsoTemperature + 5)
    {
      lcd.setCursor(14, 0);
      lcd.print("HI");
    }
    else
    {
      lcd.setCursor(14, 0);
      lcd.print("LO");
    }

    lcd.setCursor(0, 1);
    lcd.print("Parat.: ");
    lcd.print(h2_round);
    lcd.print("%");

    if ((optimalBelsoHumidity - 5 <= h2) && (h2 <= optimalBelsoHumidity + 5))
    {
      lcd.setCursor(14, 1);
      lcd.print("OK");
    }
    else if (h2 > optimalBelsoHumidity + 3)
    {
      lcd.setCursor(14, 1);
      lcd.print("HI");
    }
    else
    {
      lcd.setCursor(14, 1);
      lcd.print("LO");
    }
  }
}

// ------------------------ Belso2 hőmérséklet -------------------------------------------------------------------------------------------------------
void showBelso2Temp ()
{
 
  float t3 = dht2.readTemperature(); // until cave2 sensor is not working
  float h3 = dht2.readHumidity();// until cave2 sensor is not working

  int t3_round = roundf(t3); // until cave2 sensor is not working
  int h3_round = roundf(h3); // until cave2 sensor is not working

  if (isnan(t3) || isnan(h3))
  {
    Serial.println("Failed to read from DHT3 #3");
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Belso2: N/A");
    lcd.setCursor(0, 1);
    lcd.print("Parat.: N/A");
  }
  else
  {
    Serial.print("Belso2 temp:");
    Serial.print(t3);
    Serial.print(" / ");
    Serial.println(t3_round);
    lcd.clear();
    lcd.print("Belso2: ");// a cave1 temp to LCD
    lcd.print(t3_round);//
    lcd.print((char)223); //degree sign
    lcd.print("C");

    if ((optimalBelsoTemperature - 3 <= t3) && (t3 <= optimalBelsoTemperature + 5))
    {
      lcd.setCursor(14, 0);
      lcd.print("OK");
    }
    else if (t3 > optimalBelsoTemperature + 3)
    {
      lcd.setCursor(14, 0);
      lcd.print("HI");
    }
    else
    {
      lcd.setCursor(14, 0);
      lcd.print("LO");
    }

    lcd.setCursor(0, 1);
    lcd.print("Parat.: ");
    lcd.print(h3_round);
    lcd.print("%");

    if ((optimalBelsoHumidity - 5 <= h3) && (h3 <= optimalBelsoHumidity + 5))
    {
      lcd.setCursor(14, 1);
      lcd.print("OK");
    }
    else if (h3 > optimalBelsoHumidity + 3)
    {
      lcd.setCursor(14, 1);
      lcd.print("HI");
    }
    else
    {
      lcd.setCursor(14, 1);
      lcd.print("LO");
    }
  }
}

// ------------------------ Runway temp -------------------------------------------------------------------------------------------------------
void showKifutoTemp ()
{
  float t4 = dht4.readTemperature(); // t humidity Celsius
  float h4 = dht4.readHumidity();//h humidity

  int t4_round = roundf(t4); // t humidity Celsius
  int h4_round = roundf(h4);//h humidity

  if (isnan(t4) || isnan(h4))
  {
    Serial.println("Failed to read from DHT4 #4");
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Kifuto: N/A");
    lcd.setCursor(0, 1);
    lcd.print("Parat.: N/A");
  }
  else
  {
    Serial.print("Kifuto temp:");
    Serial.print(t4);
    Serial.print(" / ");
    Serial.println(t4_round);
    lcd.clear();
    lcd.print("Kifuto: ");// a cave1 temp to LCD
    lcd.print(t4_round);//
    lcd.print((char)223); //degree sign
    lcd.print("C");

    if ((optimalKifutoTemperature - 5 <= t4) && (t4 <= optimalKifutoTemperature + 5))
    {
      lcd.setCursor(14, 0);
      lcd.print("OK");
    }
    else if (t4 > optimalKifutoTemperature + 5)
    {
      lcd.setCursor(14, 0);
      lcd.print("HI");
    }
    else
    {
      lcd.setCursor(14, 0);
      lcd.print("LO");
    }

    lcd.setCursor(0, 1);
    lcd.print("Parat.: ");
    lcd.print(h4_round);
    lcd.print("%");

    if ((optimalKifutoHumidity - 10 <= h4) && (h4 <= optimalKifutoHumidity + 10))
    {
      lcd.setCursor(14, 1);
      lcd.print("OK");
    }
    else if (h4 > optimalKifutoHumidity + 3)
    {
      lcd.setCursor(14, 1);
      lcd.print("HI");
    }
    else
    {
      lcd.setCursor(14, 1);
      lcd.print("LO");
    }
  }
}

// ------------------------ LCD backlight -------------------------------------------------------------------------------------------------------
void lcdBacklight (time_t t, char *tz)
{
  if (hour(t) * 60 + minute(t) >= sunRise_Hour * 60 + sunRise_Minute - uvOffset - lcdOffset && hour(t) * 60 + minute(t) <= sunSet_Hour * 60 + sunSet_Minute + uvOffset + lcdOffset) // 30min before sunrise and 30 minutes after sunset
  {
    if (isBackLight == false)
    {
      lcd.clear();
      lcd.backlight();
      lcd.setCursor(0, 0);
      lcd.print("LCD vilagitas");
      lcd.setCursor(0, 1);
      lcd.print("bekapcsol...");
      isBackLight = true;
      delay(500);
      digitalWrite(LED, LOW);
      updateTwitterStatus("LCD backlight: ON");
      delay(4500);
    }
  }
  else //30 minutes after sunset LCD backlight down
  {
    if (isBackLight == true)
    {
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("LCD vilagitas");
      lcd.setCursor(0, 1);
      lcd.print("kikapcsol...");
      delay(4500);
      lcd.noBacklight();
      isBackLight = false;
      updateTwitterStatus("LCD backlight: OFF / Night mode: ON");
      delay(500);
      analogWrite(LED, brightness);
    }
  }
}
// ------------------------ Initialize -------------------------------------------------------------------------------------------------------
void systemInit ()
{
  lcd.backlight();
  isBackLight = true;
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Smart Terrarium");
  lcd.setCursor(0, 1);
  lcd.print("bekapcsol...");
  delay(3000);

  lcd.clear();
  lcd.print("Wifi kapcsolodas");
  lcd.setCursor(0, 1);
  lcd.print(ssid);
  delay(3000);

  wifiConnect();

  updateTwitterStatus("Terrarium online");

  lcd.clear();
  lcd.print("Terrarium online");
  lcd.setCursor(0, 1);
  lcd.print("IP request...");
  delay(3000);

  lcd.clear();
  lcd.print("IP cim: ");
  lcd.setCursor(0, 1);
  lcd.print(WiFi.localIP());
  delay(3000);

  ntpUpdate();

  lcd.clear();
  lcd.print("Halozati ido"); // NTP request
  lcd.setCursor(0, 1);
  lcd.print("lekerdezese...");
  delay(3000);

  utc = Rtc.GetDateTime();
  hungary = myTZ.toLocal(utc, &tcr);
  sunInfo (hungary, tcr -> abbrev, 0);
  delay(3000);

  //localTimeTest(hungary, tcr -> abbrev);

  futesVezerlesInit (hungary, tcr -> abbrev);

  localShowTime (hungary, tcr -> abbrev);
  delay(3000);

  uvbVezerles (hungary, tcr -> abbrev);

  showGepgazTemp ();
  delay(3000);

  showFutesTemp ();
  delay(3000);

  showBelso1Temp ();
  delay(3000);

  showBelso2Temp ();
  delay(3000);

  showKifutoTemp ();
  delay(3000);

  localShowTime (hungary, tcr -> abbrev);
}
// ------------------------ Local Time Test ----------------------------------------------------------------------------------------------------------
void localTimeTest (time_t t, char *tz)
{
  lcd.clear();
  lcd.print("Hour");
  lcd.print(hour(t));
  lcd.setCursor(0, 1);
  lcd.print("Minute");
  lcd.print(minute(t));
  delay(8000);
}

// ------------------------ UV control ----------------------------------------------------------------------------------------------------------
void uvbVezerles (time_t t, char *tz)
{
  if (hour(t) * 60 + minute(t) >= sunRise_Hour * 60 + sunRise_Minute - uvOffset && hour(t) * 60 + minute(t) <= (sunRise_Hour + uvHours) * 60 + sunRise_Minute) //from sunrise-offset +7 hour UVB
  {
    if (digitalRead(RELAY1_UVB) == LOW)
    {
      Serial.println("UVB bekapcsolása");
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("UVB vilagitas");
      lcd.setCursor(0, 1);
      lcd.print("bekapcsol");
      delay(1000);
      digitalWrite(RELAY1_UVB, HIGH);          // Turns ON Relays 1
      updateTwitterStatus("UVB control: ON");
    }
  }
  else if (hour(t) * 60 + minute(t) >= (sunSet_Hour - sunSetOffset) * 60 + sunSet_Minute && hour(t) * 60 + minute(t) <= sunSet_Hour * 60 + sunSet_Minute + uvOffset) //+1 hour UVB before sunset and + uvOffset
  {
    if (digitalRead(RELAY1_UVB) == LOW)
    {
      Serial.println("UVB bekapcsolása");
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("UVB vilagitas");
      lcd.setCursor(0, 1);
      lcd.print("bekapcsol");
      delay(1000);
      digitalWrite(RELAY1_UVB, HIGH);          // Turns ON Relays 1
      updateTwitterStatus("UVB control: ON");
    }
  }
  else
  {
    if (digitalRead(RELAY1_UVB) == HIGH)
    {
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("UVB vilagitas");
      lcd.setCursor(0, 1);
      lcd.print("kikapcsol");
      delay(1000);
      Serial.println("UVB kikapcsolása");
      digitalWrite(RELAY1_UVB, LOW);         // Turns Relay 1 Off
      updateTwitterStatus("UVB control: OFF");
    }
  }
  delay(3000);
}

// ------------------------ Heating Control Timer -------------------------------------------------------------------------------------------------
void futesVezerlesTimer (time_t t, char *tz)
{
  if (hour(t) * 60 + minute(t) >= sunRise_Hour * 60 + sunRise_Minute && hour(t) * 60 + minute(t) <= (sunSet_Hour - sunSetOffset) * 60 + sunSet_Minute) //between sunrise and sunset
  {
    if (hour(t) * 60 + minute(t) == sunRise_Hour * 60 + sunRise_Minute)  //morning ON
    {
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Futesrendszer");
      lcd.setCursor(0, 1);
      lcd.print("bekapcsol...");
      delay(3000);
      futesVezerles ();
    }
    else if (minute(t) % 10 == 0  && hour(t) * 60 + minute(t) != sunRise_Hour * 60 + sunRise_Minute)  //check every 10 minutes
    {
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Homerseklet ell.");
      lcd.setCursor(0, 1);
      lcd.print("(10 percenkent)");
      delay(3000);
      futesVezerles ();
    }
  }
  else if (hour(t) * 60 + minute(t) >= (sunSet_Hour - sunSetOffset) * 60 + sunSet_Minute && hour(t) * 60 + minute(t) <= sunSet_Hour * 60 + sunSet_Minute) //one hour before sunset
  {
    if (digitalRead(RELAY3_25W) == HIGH)
    {
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Terrarium");
      lcd.setCursor(0, 1);
      lcd.print("futes: szunetel");
      delay(4500);
      digitalWrite(RELAY3_25W, LOW);         // Turns Relay 3 Off
      updateTwitterStatus("Heating night mode: 25W OFF"); 
      Serial.println("25W lekapcsolása / LOW");
    }
    if (digitalRead(RELAY2_50W) == HIGH)
    {
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Terrarium");
      lcd.setCursor(0, 1);
      lcd.print("futes: szunetel");
      delay(4500);
      digitalWrite(RELAY2_50W, LOW);         // Turns Relay 2 Off
      updateTwitterStatus("Heating night mode: 50W OFF"); 
      Serial.println("50W lekapcsolása / LOW");
    }
    delay(500);
  }
}
// ------------------------ Heating control initialize-------------------------------------------------------------------------------------------------------
void futesVezerlesInit (time_t t, char *tz)
{
  delay(500);
  float Belso1TempControl = dht2.readTemperature();
  float Belso2TempControl = Belso1TempControl;
  avgTemp = ((Belso1TempControl + Belso2TempControl) / 2);

  if (avgTemp <=  10 || isnan(avgTemp))
  {
    delay(2000);
    float Belso1TempControl = dht2.readTemperature();
    float Belso2TempControl = Belso1TempControl;

    Belso1Temp = Belso1TempControl;
    Belso2Temp = Belso2TempControl;
    avgTemp = ((Belso1Temp + Belso2Temp) / 2);
    //avgTempRound = ((float)avgTemp, 0);
    avgTempRound = (roundf(avgTemp));

    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Thermostat");
    lcd.setCursor(0, 1);
    lcd.print("setup+: ");
    lcd.print(avgTempRound);
    lcd.print((char)223); //degree sign
    lcd.print("C");
    delay(2500);
  }
  else
  {
    Belso1Temp = Belso1TempControl;
    Belso2Temp = Belso2TempControl;
    avgTemp = ((Belso1Temp + Belso2Temp) / 2);
    avgTempRound = (roundf(avgTemp));
  }

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Thermostat");
  lcd.setCursor(0, 1);
  lcd.print("setup: ");
  lcd.print(avgTempRound);
  lcd.print((char)223); //degree sign
  lcd.print("C");
  delay(4500);

  if (hour(t) * 60 + minute(t) >= sunRise_Hour * 60 + sunRise_Minute && hour(t) * 60 + minute(t) <= (sunSet_Hour - sunSetOffset) * 60 + sunSet_Minute) //between sunrise and sunset - 1 hour
  {
    futesVezerles ();
  }
}
// ------------------------ Fűtés vezérlés -------------------------------------------------------------------------------------------------------
void futesVezerles ()
{
  if (optimalBelsoTemperature < avgTemp) // if avg is higher than optimal 
  {
    if (digitalRead(RELAY3_25W) == HIGH) // if ON
    {
      Serial.println("25W bekapcsolva");
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Terrarium");
      lcd.setCursor(0, 1);
      lcd.print("futes: 25W /");
      lcd.print(avgTempRound);
      lcd.print((char)223); //degree sign
      lcd.print("C");
    }
    else if (digitalRead(RELAY2_50W) == HIGH) // if ON
    {
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Terrarium");
      lcd.setCursor(0, 1);
      lcd.print("futes: 50W > 25W");
      digitalWrite(RELAY3_25W, HIGH);          // Turns ON Relays 3
      Serial.println("25W bekapcsolása / HIGH");
      updateTwitterStatus("Heating control: 50W > 25W");
      delay(500);
      digitalWrite(RELAY2_50W, LOW);         // Turns Relay 3 Off
      Serial.println("50W lekapcsolása / LOW");
    }
    else if ((digitalRead(RELAY3_25W) == LOW) && (digitalRead(RELAY2_50W) == LOW)) // if both OFF
    {
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Terrarium");
      lcd.setCursor(0, 1);
      lcd.print("futes: 25W /");
      lcd.print(avgTempRound);
      lcd.print((char)223); //degree sign
      lcd.print("C");
      digitalWrite(RELAY3_25W, HIGH);          // Turns ON Relays 3
      Serial.println("25W bekapcsolása / HIGH");
      updateTwitterStatus("Heating control: 25W");
    }
  }
  else // if avg lower than optimal
  {
    if (digitalRead(RELAY2_50W) == HIGH) // if 50W ON
    {
      Serial.println("50W bekapcsolva");
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Terrarium");
      lcd.setCursor(0, 1);
      lcd.print("futes: 50W /");
      lcd.print(avgTempRound);
      lcd.print((char)223); //degree sign
      lcd.print("C");
    }
    else if (digitalRead(RELAY3_25W) == HIGH) // if 25W ON
    {
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Terrarium");
      lcd.setCursor(0, 1);
      lcd.print("futes: 25W > 50W");
      delay(5000);
      digitalWrite(RELAY2_50W, HIGH);          // Turns ON Relays 2
      Serial.println("50W bekapcsolása / HIGH");
      updateTwitterStatus("Heating control: 25W > 50W");
      delay(500);
      digitalWrite(RELAY3_25W, LOW);         // Turns Relay 3 Off
      Serial.println("25W lekapcsolása / LOW");
    }
    else if ((digitalRead(RELAY3_25W) == LOW) && (digitalRead(RELAY2_50W) == LOW)) // if both OFF
    {
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Terrarium");
      lcd.setCursor(0, 1);
      lcd.print("futes: 50W /");
      lcd.print(avgTempRound);
      lcd.print((char)223); //degree sign
      lcd.print("C");
      digitalWrite(RELAY2_50W, HIGH);          // Turns ON Relays 2
      Serial.println("50W bekapcsolása / LOW");
      updateTwitterStatus("Heating control: 50W");
    }
  }
  delay(3000);
}

// ------------------------ Heating Control -------------------------------------------------------------------------------------------------------
void futesVezerlesOrig ()
{
  if (optimalBelsoTemperature <= avgTemp) // if avg temp is higher than optimal
  {
    if (digitalRead(RELAY3_25W) == HIGH) // if 25W ON
    {
      Serial.println("25W bekapcsolva");
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Terrarium");
      lcd.setCursor(0, 1);
      lcd.print("futes: 25W /");
      lcd.print(avgTempRound);
      lcd.print((char)223); //degree sign
      lcd.print("C");
    }
    else if (digitalRead(RELAY2_50W) == HIGH) // if 50W ON
    {
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Terrarium");
      lcd.setCursor(0, 1);
      lcd.print("futes: 50W > 25W");
      digitalWrite(RELAY3_25W, HIGH);          // Turns ON Relays 3
      Serial.println("25W bekapcsolása / HIGH");
      updateTwitterStatus("Heating control: 50W > 25W");
      delay(500);
      digitalWrite(RELAY2_50W, LOW);         // Turns Relay 3 Off
      Serial.println("50W lekapcsolása / LOW");
    }
    else if ((digitalRead(RELAY3_25W) == LOW) && (digitalRead(RELAY2_50W) == LOW)) // if both turned OFF 
    {
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Terrarium");
      lcd.setCursor(0, 1);
      lcd.print("futes: 25W /");
      lcd.print(avgTempRound);
      lcd.print((char)223); //degree sign
      lcd.print("C");
      digitalWrite(RELAY3_25W, HIGH);          // Turns ON Relays 3
      Serial.println("25W bekapcsolása / HIGH");
      updateTwitterStatus("Heating control: 25W");
    }
  }
  else // if avg is lower than optimal
  {
    if (digitalRead(RELAY2_50W) == HIGH) // if 50W ON
    {
      Serial.println("50W bekapcsolva");
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Terrarium");
      lcd.setCursor(0, 1);
      lcd.print("futes: 50W /");
      lcd.print(avgTempRound);
      lcd.print((char)223); //degree sign
      lcd.print("C");
    }
    else if (digitalRead(RELAY3_25W) == HIGH) // if 25W ON
    {
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Terrarium");
      lcd.setCursor(0, 1);
      lcd.print("futes: 25W > 50W");
      delay(5000);
      digitalWrite(RELAY2_50W, HIGH);          // Turns ON Relays 2
      Serial.println("50W bekapcsolása / HIGH");
      updateTwitterStatus("Heating control: 25W > 50W");
      delay(500);
      digitalWrite(RELAY3_25W, LOW);         // Turns Relay 3 Off
      Serial.println("25W lekapcsolása / LOW");
    }
    else if ((digitalRead(RELAY3_25W) == LOW) && (digitalRead(RELAY2_50W) == LOW)) // if both turned OFF
    {
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Terrarium");
      lcd.setCursor(0, 1);
      lcd.print("futes: 50W /");
      lcd.print(avgTempRound);
      lcd.print((char)223); //degree sign
      lcd.print("C");
      digitalWrite(RELAY2_50W, HIGH);          // Turns ON Relays 2
      Serial.println("50W bekapcsolása / LOW");
      updateTwitterStatus("Heating control: 50W");
    }
  }
  delay(3000);
}
// ------------------------ Status Display ---------------------------------------------------------------------------------------------------
void statusDisplay (const RtcDateTime & dt)
{
  Belso1Temp = dht2.readTemperature();
  Belso2Temp = dht2.readTemperature();
  avgTemp = ((Belso1Temp + Belso2Temp) / 2);

  if (dt.Hour() * 60 + dt.Minute() >= sunRise_Hour * 60 + sunRise_Minute && dt.Hour() * 60 + dt.Minute() <= sunSet_Hour * 60 + sunSet_Minute) //reggel 06:00 és este 20:00 között fut csak
  {
    if (digitalRead(RELAY3_25W) == HIGH) // if 25W turned ON
    {
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("25W / ");
      lcd.print(avgTemp);
      lcd.print((char)223); //degree sign
      lcd.print("C");
    }
    else if (digitalRead(RELAY2_50W) == HIGH) // if 50W turned ON
    {
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("50W / ");
      lcd.print(avgTemp);
      lcd.print((char)223); //degree sign
      lcd.print("C");
    }
    if (digitalRead(RELAY1_UVB) == HIGH)
    {
      lcd.clear();
      lcd.setCursor(0, 1);
      lcd.print("UVB ON");
    }
    else
    {
      lcd.clear();
      lcd.setCursor(0, 1);
      lcd.print("UVB OFF");
    }
  }
}

// ------------------------ Terrarium tweet-----------------------------------------------------------------------------------------------------
void terrariumTweet()
{
  // Print Update Response to Serial Monitor
  if (client.available())
  {
    char c = client.read();
    Serial.print(c);
  }

  // Disconnect from ThingSpeak
  if (!client.connected() && lastConnected)
  {
    Serial.println("...disconnected");
    Serial.println();

    client.stop();
  }

  // Check if Arduino Ethernet needs to be restarted
  /*  if (failedCounter > 3 ) {startEthernet();} */

  lastConnected = client.connected();
}

void updateTwitterStatus(String tsData)
{
  if (client.connect(thingSpeakAddress, 80))
  {
    // Create HTTP POST Data
    tsData = "api_key=" + thingtweetAPIKey + "&status=" + tsData;

    client.print("POST /apps/thingtweet/1/statuses/update HTTP/1.1\n");
    client.print("Host: api.thingspeak.com\n");
    client.print("Connection: close\n");
    client.print("Content-Type: application/x-www-form-urlencoded\n");
    client.print("Content-Length: ");
    client.print(tsData.length());
    client.print("\n\n");

    client.print(tsData);

    lastConnectionTime = millis();

    if (client.connected())
    {
      Serial.println("Connecting to ThingSpeak...");
      Serial.println();

      failedCounter = 0;
    }
    else
    {
      failedCounter++;

      Serial.println("Connection to ThingSpeak failed (" + String(failedCounter, DEC) + ")");
      Serial.println();
    }
  }
  else
  {
    failedCounter++;

    Serial.println("Connection to ThingSpeak Failed (" + String(failedCounter, DEC) + ")");
    Serial.println();

    lastConnectionTime = millis();
  }
}
// ------------------------ RTC + time show  ------------------------------------------------------------------------------------------------
void printDateTime(time_t t, char *tz)
{
  char datestring[20];

  snprintf_P(datestring,
             countof(datestring),
             PSTR("%04u.%02u.%02u %02u:%02u:%02u"),
             year(t),
             month(t),
             day(t),
             hour(t),
             minute(t),
             second(t) );
  Serial.print(datestring);
}

void clientDateTime(time_t t, char *tz)
{
  char datestring[20];

  snprintf_P(datestring,
             countof(datestring),
             PSTR("%04u.%02u.%02u %02u:%02u:%02u"),
             year(t),
             month(t),
             day(t),
             hour(t),
             minute(t),
             second(t) );
  client.print(datestring);
}


void localShowTime(time_t t, char *tz)
{
  lcd.clear();
  lcd.setCursor(2, 1);
  lcd.print(year(t));
  lcd.print(".");
  lcd.print(monthShortStr(month(t)));
  //lcdPrintI00(month(t));
  lcd.print(".");
  lcdPrintI00(day(t));

  lcd.setCursor(1, 0);
  lcd.print("Ido: ");
  lcdPrintI00(hour(t));
  lcdPrintDigits(minute(t));
  lcd.print(" ");
  lcd.print(tcr -> abbrev);

  Serial.print("RTC time:");
  printDateTime(hungary, tcr -> abbrev);
  Serial.println();
}

//Print an integer in "00" format (with leading zero).
//Input value assumed to be between 0 and 99.
void lcdPrintI00(int val)
{
  if (val < 10) lcd.print('0');
  lcd.print(val, DEC);
  return;
}
//Print an integer in ":00" format (with leading zero).
//Input value assumed to be between 0 and 99.
void lcdPrintDigits(int val)
{
  lcd.print(':');
  if (val < 10) lcd.print('0');
  lcd.print(val, DEC);
}

// ------------------------ Control room temp -------------------------------------------------------------------------------------------------------
void showGepgazTemp ()
{
  RtcTemperature temp = Rtc.GetTemperature();
  Serial.print("Gephaz temp: ");
  Serial.print(roundf(temp.AsFloat() - 2), 0); // -2C degrees correction
  Serial.println("C");

  lcd.clear();
  lcd.print("Gephaz: ");// a Belso1 temp kiíratás
  lcd.print(roundf(temp.AsFloat() - 2), 0); // -2C degrees correction
  lcd.print((char)223); //degree sign
  lcd.print("C");

  if ((roundf(temp.AsFloat() - 2), 0) <= optimalGephazTemperature + 3)
  {
    lcd.setCursor(14, 0);
    lcd.print("OK");
  }
  else if ((roundf(temp.AsFloat() - 2), 0) > optimalGephazTemperature + 3)
  {
    lcd.setCursor(14, 0);
    lcd.print("HI");
  }

  lcd.setCursor(0, 1);
  lcd.print("Optimalis: ");
  lcd.print(optimalGephazTemperature);
  lcd.print((char)223); //degree sign
  lcd.print("C");
}

// ------------------------ Cloud connect --------------------------------------------------------------------------------------------------------
void cloudConnect ()
{
  if (client.connect(thingSpeakAddress, 80))
  {

    float Belso1Temperature = Belso1Temp;
    float Belso2Temperature = Belso2Temp;
    float avgTemperature = avgTemp;

    float Belso1Hum = dht2.readHumidity();
    float Belso2Hum = dht2.readHumidity();
    float avgHum = ((Belso1Hum + Belso2Hum) / 2);

    float futesTemp = dht1.readTemperature(); // t temp Celsius
    float futesHum = dht1.readHumidity();//h humidity

    float kifutoTemp = dht4.readTemperature(); // t temp Celsius
    float kifutoHum = dht4.readHumidity();//h humidity

    RtcTemperature gephazTemp = Rtc.GetTemperature();

    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("ThingSpeak.com");
    lcd.setCursor(0, 1);
    lcd.print("adatkuldes...");

    String postStr = writeAPIKey;
    postStr += "&field1=";
    postStr += String(avgTemperature);
    postStr += "&field2=";
    postStr += String(avgHum);
    postStr += "&field3=";
    postStr += String(futesTemp);
    postStr += "&field4=";
    postStr += String(futesHum);
    postStr += "&field5=";
    postStr += String(kifutoTemp);
    postStr += "&field6=";
    postStr += String(kifutoHum);
    postStr += "&field7=";
    postStr += String(gephazTemp.AsFloat());
    postStr += "&field8=";
    if (digitalRead(RELAY2_50W) == HIGH && digitalRead(RELAY3_25W) == LOW)
    {
      if (digitalRead(RELAY1_UVB) == HIGH)
      {
        postStr += String(bulbBig + bulbUVB);
      }
      else
      {
        postStr += String(bulbBig);
      }
    }
    else if (digitalRead(RELAY3_25W) == HIGH && digitalRead(RELAY2_50W) == LOW)
    {
      if (digitalRead(RELAY1_UVB) == HIGH)
      {
        postStr += String(bulbSmall + bulbUVB);
      }
      else
      {
        postStr += String(bulbSmall);
      }
    }
    else if (digitalRead(RELAY1_UVB) == HIGH && digitalRead(RELAY2_50W) == LOW)
    {
      postStr += String(bulbUVB);
    }
    else
    {
      postStr += String(0);
    }

    postStr += "\r\n\r\n";

    client.print("POST /update HTTP/1.1\n");
    client.print("Host: api.thingspeak.com\n");
    client.print("Connection: close\n");
    client.print("X-THINGSPEAKAPIKEY: " + writeAPIKey + "\n");
    client.print("Content-Type: application/x-www-form-urlencoded\n");
    client.print("Content-Length: ");
    client.print(postStr.length());
    client.print("\n\n");
    client.print(postStr);

    Serial.print("avgTemperature: ");
    Serial.print(avgTemperature);
    Serial.print("degrees Celsius Humidity: ");
    Serial.print(avgHum);
    Serial.println("Sending data to Thingspeak");
  }
  client.stop();
}
// ------------------------ Sunrise/Sunset tweet --------------------------------------------------------------------------------------------
void timeLord(time_t t, char *tz)
{
  if (hour(t) * 60 + minute(t) == 180) // test dawn tweet
  {

    sun(hungary, tcr -> abbrev);
    updateTwitterStatus("Sunrise/Sunset");

  }
}
// ------------------------ Terrarium control setup --------------------------------------------------------------------------------------------
void setup()
{
  Rtc.Begin();

  //Wire.begin(04, 05); // due to limited pins, use pin 14 and 15 for SDA, SCL
  lcd.begin (16, 2); // for 16 x 2 LCD module
  pinMode(LED, OUTPUT);
  pinMode(RELAY1_UVB, OUTPUT);
  pinMode(RELAY3_25W, OUTPUT);
  pinMode(RELAY2_50W, OUTPUT);

  digitalWrite(RELAY1_UVB, LOW);
  digitalWrite(RELAY3_25W, LOW);
  digitalWrite(RELAY2_50W, LOW);

  digitalWrite(LED, LOW);

  systemInit();

  server.begin();

  updateOTA();
  analogWrite(LED, brightness); // down the LED highness end of the setup process
}

// ------------------------ Terrarium control loop ----------------------------------------------------------------------------------------------
void loop()
{
  ArduinoOTA.handle();
  webServer(hungary, tcr -> abbrev);
  utc = Rtc.GetDateTime();
  hungary = myTZ.toLocal(utc, &tcr);

  //soilChk (now);

  if ((unsigned long)(millis() - waitUntil) >= interval)
  {
    Serial.println(millis());

    ntpUpdate();

    timeLord(hungary, tcr -> abbrev);

    uvbVezerles (hungary, tcr -> abbrev);

    soilMoisture (hungary, tcr -> abbrev);

    futesVezerlesTimer (hungary, tcr -> abbrev);

    lcdBacklight (hungary, tcr -> abbrev);

    localShowTime (hungary, tcr -> abbrev);
    delay(5000);

    sunInfo (hungary, tcr -> abbrev, 0);
    delay(3000);

    showGepgazTemp ();
    delay(5000);

    showFutesTemp ();
    delay(3000); // másodlagos ellenőrzés miatt

    showBelso1Temp ();
    delay(5000);

    showBelso2Temp ();
    delay(5000);

    showKifutoTemp ();
    delay(5000);

    cloudConnect ();
    delay(3000);

    localShowTime (hungary, tcr -> abbrev);

    waitUntil = waitUntil + interval;  // wait another interval cycle
  }
}
