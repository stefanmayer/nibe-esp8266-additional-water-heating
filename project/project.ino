#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266WebServer.h>
#include <WiFiClient.h>
#include <SoftwareSerial.h>
#include <ArduinoJson.h>
#include <InfluxDbClient.h>
#include <InfluxDbCloud.h>
#include <EEPROM.h>
#include <NTPClient.h>
#include <WiFiUdp.h>


/*************************
 ***   Influx Config   ***
 *************************/
#define MY_INFLUXDB_URL       "https://<INFLUX_DB_HOST>:8086"
#define MY_INFLUXDB_TOKEN     "<INFLUX_DB_TOKEN>"
#define MY_INFLUXDB_ORG       "<INFLUX_DB_ORG>"
#define MY_INFLUXDB_BUCKET    "srwp"
#define TZ_INFO               "UTC"
#define DE_RE                 D3
#define LED                   D4
#define DATA_POLL_INTERVAL    1000

Point sensor("nibe");
InfluxDBClient influxClient(MY_INFLUXDB_URL, MY_INFLUXDB_ORG, MY_INFLUXDB_BUCKET, MY_INFLUXDB_TOKEN);


/*************************
 ***    RS485 Config   ***
 *************************/
#define TX_485                D3
#define RX_485                D2
#define RS485_control         D1
#define RS485transmit         HIGH
#define RS485receive          LOW
#define NIBE_DISPLAY_1        0x50
#define NIBE_DISPLAY_2        0x51
#define NIBE_DISPLAY_3        0x52
#define NIBE_DISPLAY_4        0x53

SoftwareSerial RS485_serial (RX_485, TX_485);


/*************************
 ***    Wifi Config    ***
 *************************/
#define HTTP_REST_PORT        80
#define WAIT_TIME             ((12 *(60*60) + 0 *(60) + 0));

const char* ssid = "<YOUR_SSID>";
const char* password = "<YOUR_WIFI_PASSWORD>";

const char daysOfTheWeek[7][12] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};

WiFiUDP ntpUDP;
ESP8266WebServer httpRestServer(HTTP_REST_PORT);
NTPClient timeClient(ntpUDP, "pool.ntp.org", 3600);

int acceptNewFiringTimeLeft = 0;


/*************************
 ***     WP Config     ***
 *************************/

#define EXTRA_HOT_WATER_BUTTON  D8
#define BUTTON_CLICK_DELAY_MS   500
#define LOOP_DELAY_MS           5000
#define POWER_BUF_LIMIT         30

typedef struct {
  bool listenToInverter;
  int excludeTimeStart;
  int excludeTimeStop;
  int powerThreshold;
  byte stopTempLowPowerValue;
  byte minStartTempDiff;
  int hotWaterStopTemp;
} WPControlConfig;

WPControlConfig wpControlConfig;

int wpHotWaterValue = 0;

bool wpCompressActive = false;
bool wpCompressAActive = false;

bool wpElectricHeaterActive = false;
bool wpElectricHeaterIActive = false;
bool wpElectricHeaterIIActive = false;
bool wpElectricHeaterIIIActive = false;

bool wpHotWaterActive = false;
bool wpHotWaterAActive = false;
bool wpHotWaterBActive = false;

bool wpPumpActive = false;
bool wpPumpIActive = false;
bool wpPumpIIActive = false;
bool wpFloorheatActive = false;
bool wpDefrostActive = false;

String lastStartDayHotWater = "None";
String lastStopDayHotWater = "None";
int powerIntValue = 0;
int influxDataReady = 0;
long lastTimestamp = 0;
bool toggleLed = false;
int powerBuffer[POWER_BUF_LIMIT] = {0};
int powerBufferCnt = 0;
int oneMinuteCnt = 0;
int powerMeanValue = 0;
String lastParamUpdates = "\"paramsUpdated\":{}";
String lastHotWaterStatusMsg = "";


void clickExtraHotWater() {
  digitalWrite(LED_BUILTIN, LOW);
  digitalWrite(EXTRA_HOT_WATER_BUTTON, HIGH);
  delay(BUTTON_CLICK_DELAY_MS);
  digitalWrite(LED_BUILTIN, HIGH);
  digitalWrite(EXTRA_HOT_WATER_BUTTON, LOW);
  delay(BUTTON_CLICK_DELAY_MS);
  digitalWrite(LED_BUILTIN, LOW);
}

void extraHotWater() {
  clickExtraHotWater();
  httpRestServer.send(200, F("application/json"), F("{\"status\":\"The extra hot water button pressed!\"}"));
}

void getInverterData() {
  String powerValue = httpRestServer.arg("power");
  if (powerValue == "") {
    httpRestServer.send(503, "application/json", F("{\"error\":\"got no value for inverter power\"}"));
    return;
  }

  powerIntValue = powerValue.toInt();

  Serial.println("Received powerValue=" + String(powerIntValue) + " from inverter");
  httpRestServer.send(200, "application/json", F("{\"status\":\"power value received\"}"));
}

void getWPConfigData() {
  String response = "{";
  response += "\"listenToInverter\":\"" + String(wpControlConfig.listenToInverter ? "true" : "false") + "\",";
  response += "\"excludeTimeStart\":" + String(wpControlConfig.excludeTimeStart) + ",";
  response += "\"excludeTimeStop\":" + String(wpControlConfig.excludeTimeStop) + ",";
  response += "\"powerThreshold\":" + String(wpControlConfig.powerThreshold) + ",";
  response += "\"stopTempLowPowerValue\":" + String(wpControlConfig.stopTempLowPowerValue) + ",";
  response += "\"minStartTempDiff\":" + String(wpControlConfig.minStartTempDiff) + ",";
  response += "\"hotWaterStopTemp\":" + String(wpControlConfig.hotWaterStopTemp) + ",";
  response += "\"lastStartDayHotWater\":\"" + String(lastStartDayHotWater) + "\",";
  response += "\"lastStopDayHotWater\":\"" + String(lastStopDayHotWater) + "\",";
  response += "\"currentPowerValue\":" + String(powerIntValue) + ",";
  response += "\"currentDayOfTheWeek\":\"" + String(daysOfTheWeek[timeClient.getDay()]) + "\",";
  response += "\"currentTime\":\"" + String(timeClient.getHours() * 100 + timeClient.getMinutes()) + "\",";
  response += "\"currentTimeFormated\":\"" + String(timeClient.getHours()) + ":" + String(timeClient.getMinutes()) + ":" + String(timeClient.getSeconds()) + "\",";
  response += "\"currentHotWaterValue\":\"" + String(wpHotWaterValue / 10) + "\",";
  response += "\"currentCompressActive\":\"" + String(wpCompressActive ? "true" : "false") + "\","; 
  response += "\"currentCompressAActive\":\"" + String(wpCompressAActive ? "true" : "false") + "\","; 
  response += "\"currentElectricHeaterActive\":\"" + String(wpElectricHeaterActive ? "true" : "false") + "\","; 
  response += "\"currentElectricHeaterIActive\":\"" + String(wpElectricHeaterIActive ? "true" : "false") + "\","; 
  response += "\"currentElectricHeaterIIActive\":\"" + String(wpElectricHeaterIIActive ? "true" : "false") + "\","; 
  response += "\"currentElectricHeaterIIIActive\":\"" + String(wpElectricHeaterIIIActive ? "true" : "false") + "\","; 
  response += "\"currentHotWaterActive\":\"" + String(wpHotWaterActive ? "true" : "false") + "\",";
  response += "\"currentHotWaterAActive\":\"" + String(wpHotWaterAActive ? "true" : "false") + "\",";
  response += "\"currentHotWaterBActive\":\"" + String(wpHotWaterBActive ? "true" : "false") + "\",";
  response += "\"currentPumpActive\":\"" + String(wpPumpActive ? "true" : "false") + "\",";
  response += "\"currentPumpIActive\":\"" + String(wpPumpIActive ? "true" : "false") + "\",";
  response += "\"currentPumpIIActive\":\"" + String(wpPumpIIActive ? "true" : "false") + "\",";
  response += "\"currentFloorheatActive\":\"" + String(wpFloorheatActive ? "true" : "false") + "\",";
  response += "\"currentDefrostActive\":\"" + String(wpDefrostActive ? "true" : "false") + "\"";
  response += "}";
  
  httpRestServer.send(200, "application/json", response);
}

void setWPConfigData() {
  lastParamUpdates = "\"paramsUpdated\":{";

  for (int i = 0; i < httpRestServer.args(); ++i) {
    Serial.println("Parm -> " + String(httpRestServer.argName(i)) + ", value -> " + String(httpRestServer.arg(i)));

    if (httpRestServer.argName(i) == "listenToInverter") {
      String listenToInverterStr = httpRestServer.arg(i);
      listenToInverterStr.toLowerCase();
      lastParamUpdates += "\"listenToInverter\":\"" + listenToInverterStr + "\",";
      Serial.println("Updated listenToInverter=" + listenToInverterStr);

      wpControlConfig.listenToInverter = (listenToInverterStr == "true");
    }
    else if (httpRestServer.argName(i) == "excludeTimeStart") {
      String excludeTimeStartStr = httpRestServer.arg(i);
      lastParamUpdates += "\"excludeTimeStart\":\"" + excludeTimeStartStr + "\",";
      Serial.println("Updated excludeTimeStart=" + excludeTimeStartStr);

      wpControlConfig.excludeTimeStart = excludeTimeStartStr.toInt();
    }
    else if (httpRestServer.argName(i) == "excludeTimeStop") {
      String excludeTimeStopStr = httpRestServer.arg(i);
      lastParamUpdates += "\"excludeTimeStop\":\"" + excludeTimeStopStr + "\",";
      Serial.println("Updated excludeTimeStop=" + excludeTimeStopStr);

      wpControlConfig.excludeTimeStop = excludeTimeStopStr.toInt();
    }
    else if (httpRestServer.argName(i) == "powerThreshold") {
      String powerThresholdStr = httpRestServer.arg(i);
      lastParamUpdates += "\"powerThreshold\":\"" + powerThresholdStr + "\",";
      Serial.println("Updated powerThreshold=" + powerThresholdStr);

      wpControlConfig.powerThreshold = powerThresholdStr.toInt();
    }
    else if (httpRestServer.argName(i) == "stopTempLowPowerValue") {
      String stopTempLowPowerValueStr = httpRestServer.arg(i);
      lastParamUpdates += "\"stopTempLowPowerValue\":\"" + stopTempLowPowerValueStr + "\",";
      Serial.println("Updated stopTempLowPowerValue=" + stopTempLowPowerValueStr);

      wpControlConfig.stopTempLowPowerValue = (byte)stopTempLowPowerValueStr.toInt();
    }
    else if (httpRestServer.argName(i) == "minStartTempDiff") {
      String minStartTempDiffStr = httpRestServer.arg(i);
      lastParamUpdates += "\"minStartTempDiff\":\"" + minStartTempDiffStr + "\",";
      Serial.println("Updated minStartTempDiff=" + minStartTempDiffStr);

      wpControlConfig.minStartTempDiff = (byte)minStartTempDiffStr.toInt();
    }
    else if (httpRestServer.argName(i) == "hotWaterStopTemp") {
      String hotWaterStopTempStr = httpRestServer.arg(i);
      lastParamUpdates += "\"hotWaterStopTemp\":\"" + hotWaterStopTempStr + "\",";
      Serial.println("Updated hotWaterStopTemp=" + hotWaterStopTempStr);

      wpControlConfig.hotWaterStopTemp = hotWaterStopTempStr.toInt();
    }
  }

  int length = lastParamUpdates.length();
  if (lastParamUpdates[length - 1] == ',') {
    lastParamUpdates.remove(length - 1, 1);
  }

  lastParamUpdates += "}";

  EEPROM.begin(sizeof(WPControlConfig));
  EEPROM.put(0, wpControlConfig);
  EEPROM.commit();

  httpRestServer.send(200, "application/json", F("{\"status\":\"values received and written to EEPROM\"}"));
}

void resetWPConfig() {
  lastStopDayHotWater = "None";
  lastStartDayHotWater = "None";
  httpRestServer.send(200, "application/json", F("{\"status\":\"wp settings reset\"}"));
}

void getStatus() {
  String response = "{";
  response += "\"lastHotWaterStatusMsg\":\"" + lastHotWaterStatusMsg + "\",";
  response += lastParamUpdates;
  response += "}";
  
  httpRestServer.send(200, "application/json", response);
}

void restServerRouting() {
  httpRestServer.on("/inverterinput", HTTP_GET, getInverterData);
  httpRestServer.on("/resetwpconfig", HTTP_GET, resetWPConfig);
  httpRestServer.on("/setwpconfig", HTTP_GET, setWPConfigData);
  httpRestServer.on("/getwpconfig", HTTP_GET, getWPConfigData);
  httpRestServer.on("/status", HTTP_GET, getStatus);
  httpRestServer.on("/forceextrabw", HTTP_POST, extraHotWater);
  httpRestServer.on("/extrabw", HTTP_POST, []() {
    if (acceptNewFiringTimeLeft == 0) {
      acceptNewFiringTimeLeft = WAIT_TIME;
      extraHotWater();
    } else {
      String response = "{\"status\":\"Already fired -> wait for ";
      response += (unsigned int)(((acceptNewFiringTimeLeft) / 60) / 60);
      response += "h ";
      response += (unsigned int)(((acceptNewFiringTimeLeft) / 60) % 60);
      response += "m ";
      response += (unsigned int)((acceptNewFiringTimeLeft) % 60);
      response += "s!\"}";
      httpRestServer.send(200, F("application/json"), response);
    }
  });
}

void initRS485() {
  RS485_serial.begin(19200);

  delay(2000);
  pinMode(RS485_control, OUTPUT);
  pinMode(D4, OUTPUT);
  pinMode(D8, OUTPUT);

  digitalWrite(D4, RS485transmit);
  delay(500);
  digitalWrite(D4, RS485receive);
  delay(500);

  digitalWrite(RS485_control, RS485receive);
  digitalWrite(D4, RS485receive);
}

void initREST() {
  restServerRouting();
  httpRestServer.begin();
}

void initWifi() {
  IPAddress ip(192, 168, 1, 40); // <- set it back to 40!!!!
  IPAddress dns(8, 8, 8, 8);
  IPAddress gateway(192, 168, 1, 1);
  IPAddress subnet(255, 255, 255, 0);
  WiFi.config(ip, dns, gateway, subnet);

  WiFi.begin(ssid, password);
  Serial.println("Connecting");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.print("Connected to WiFi network with IP Address: ");
  Serial.println(WiFi.localIP());

  timeClient.begin();
  timeClient.update();

  delay(1000);
}

void setup() {
  EEPROM.begin(sizeof(WPControlConfig));
  EEPROM.get(0, wpControlConfig);

  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);

  Serial.begin(19200);
  Serial.println("Up and Running on 19200 baud");

  initWifi();

  // use SSL for influxdb connection -> workaround to allow connection
  influxClient.setInsecure();

  if (influxClient.validateConnection()) {
    Serial.print("Connected to InfluxDB: ");
    Serial.println(influxClient.getServerUrl());
  } else {
    Serial.print("InfluxDB connection failed: ");
    Serial.println(influxClient.getLastErrorMessage());
  }

  lastStopDayHotWater = daysOfTheWeek[timeClient.getDay()];
  lastStartDayHotWater = daysOfTheWeek[timeClient.getDay()];

  initRS485();
  initREST();
}

void writeToInfluxDB() {
  Serial.print("Writing: ");
  Serial.println(sensor.toLineProtocol());
  if (!influxClient.writePoint(sensor)) {
    Serial.print("InfluxDB write failed: ");
    Serial.println(influxClient.getLastErrorMessage());
    //influxDBState = false;
  }
}

void printPayload(byte *pbuf, int payloadLength) {
  Serial.readBytes(pbuf, payloadLength);
  Serial.print(" -> payload: ");

  for (int i = 0; i < payloadLength; ++i) {
    if (pbuf[i] <= 0xF) {
      Serial.print("0");
    }
    Serial.print(pbuf[i], HEX);
    Serial.print(" ");
  }

  Serial.println();
  Serial.print(" -> ascii: ");

  for (int i = 0; i < payloadLength; ++i) {
    Serial.print((char)pbuf[i]);
  }

  Serial.println();
  Serial.println("----------");
  payloadLength = 0;
}

void checkHotWaterCondition() {
  /* is the WR/WP logic activated? */
  if (wpControlConfig.listenToInverter) {
    int currentTime = timeClient.getHours() * 100 + timeClient.getMinutes();

    /* is the current time not in the exclude range e.g. not in the time range 1100 - 1200 */
    if (currentTime < wpControlConfig.excludeTimeStart || currentTime > wpControlConfig.excludeTimeStop) {
      /* stop hot water procedure when temp limit has reached and not enough PV power is available and the hot water procedur runs already */
      // TODO: add here some hysteresis -> otherwise it could be that the procedure is stopped if a cloud appears on the sky
      if ((wpHotWaterValue / 10 >= wpControlConfig.hotWaterStopTemp ||
           (wpHotWaterValue / 10 >= wpControlConfig.stopTempLowPowerValue && powerIntValue /*powerMeanValue*/ < wpControlConfig.powerThreshold))
          && lastStopDayHotWater != daysOfTheWeek[timeClient.getDay()]
          && (wpHotWaterActive)) {
        // -> TRIGGER HOT WATER STOP
        lastStopDayHotWater = daysOfTheWeek[timeClient.getDay()];

        if (wpHotWaterValue / 10 >= wpControlConfig.hotWaterStopTemp) {
          lastHotWaterStatusMsg = "Stop hot water because hotWaterStopTemp has reached!";
        } else if (powerIntValue < wpControlConfig.powerThreshold) {
          lastHotWaterStatusMsg = "Stop hot water because lowLimit has reached and inverter power is to low!";
        }

        Serial.println("Stop hot water procedure because to less PV power and temp limit (" + String(wpControlConfig.stopTempLowPowerValue) + ") has reached!");
        clickExtraHotWater();
      }

      /* start after the exclude time the hotWater procedure when it was not started on this day and currently hotWater not running */
      else if (currentTime > wpControlConfig.excludeTimeStop
               && lastStartDayHotWater != daysOfTheWeek[timeClient.getDay()]
               && (!wpHotWaterActive))  {
        lastStartDayHotWater = daysOfTheWeek[timeClient.getDay()];

        
          lastHotWaterStatusMsg = "Start hot water procedure!";

        Serial.println("Start auto hot water procedure for today (" + String(lastStartDayHotWater) + ")");
        // -> TRIGGER HOT WATER START
        clickExtraHotWater();
      }
    }
  }
}

void loop() {
  if (Serial.available () > 0)
  {
    byte rxByte = Serial.read();

    if (rxByte == NIBE_DISPLAY_1) {
      byte hbuf[3];
      Serial.readBytes(hbuf, 3);
      if (hbuf[0] == 0x00 && hbuf[1] == 0x59 && hbuf[2] == 0x03) {
        Serial.println("Found 0x50 (Status bar) -> header bytes read payload");

        byte pbuf[3];
        printPayload(pbuf, 3);

        wpCompressActive = (bool)(pbuf[0] & 0x80);
        wpCompressAActive = (bool)(pbuf[0] & 0x40);
        
        wpElectricHeaterActive = (bool)(pbuf[0] & 0x08);
        wpElectricHeaterIActive = (bool)(pbuf[0] & 0x04);
        wpElectricHeaterIIActive = (bool)(pbuf[0] & 0x02);
        wpElectricHeaterIIIActive = (bool)(pbuf[0] & 0x01);

        wpHotWaterActive = (bool)(pbuf[1] & 0x80);
        wpHotWaterAActive = (bool)(pbuf[1] & 0x40);
        wpHotWaterBActive = (bool)(pbuf[1] & 0x20);
        
        wpPumpActive = (bool)(pbuf[2] & 0x80);
        wpPumpIActive = (bool)(pbuf[2] & 0x40);
        wpPumpIIActive = (bool)(pbuf[2] & 0x20);
        
        wpFloorheatActive = (bool)(pbuf[2] & 0x08);
        wpDefrostActive = (bool)(pbuf[2] & 0x01);

        sensor.addField("compress", wpCompressActive);
        sensor.addField("compressA", wpCompressAActive);
        sensor.addField("electricHeater", wpElectricHeaterActive);
        sensor.addField("electricHeaterI", wpElectricHeaterIActive);
        sensor.addField("electricHeaterII", wpElectricHeaterIIActive);
        sensor.addField("electricHeaterIII", wpElectricHeaterIIIActive);
        sensor.addField("compressA", wpCompressAActive);
        sensor.addField("hotWater", wpHotWaterActive);
        sensor.addField("hotWaterA", wpHotWaterAActive);
        sensor.addField("hotWaterB", wpHotWaterBActive);
        sensor.addField("pump", wpPumpActive);
        sensor.addField("pumpI", wpPumpIActive);
        sensor.addField("pumpII", wpPumpIIActive);
        sensor.addField("floorheat", wpFloorheatActive);
        sensor.addField("defrost", wpDefrostActive);

        influxDataReady = influxDataReady | 0x01;
      }
    }

    else if (rxByte == NIBE_DISPLAY_2) {
      byte hbuf[3];
      Serial.readBytes(hbuf, 3);
      if (hbuf[0] == 0x00 && hbuf[1] == 0x59 && hbuf[2] == 0x0B) {
        Serial.println("Found 0x51 (Value) -> header bytes read payload");

        byte pbuf[11];
        printPayload(pbuf, 11);

        wpHotWaterValue = (int)((pbuf[4] - 0x30) * 100 + (pbuf[5] - 0x30) * 10 + (pbuf[7] - 0x30));

        sensor.addField("hotWaterValue", wpHotWaterValue);

        influxDataReady = influxDataReady | 0x02;
      }
    }

    else if (rxByte == NIBE_DISPLAY_3) {
      byte hbuf[3];
      Serial.readBytes(hbuf, 3);
      if (hbuf[0] == 0x00 && hbuf[1] == 0x59 /*&& hbuf[2] == 0x12 ?oder? 18*/) {
        Serial.println("Found 0x52 (Display title) -> header bytes read payload");

        byte pbuf[12];
        printPayload(pbuf, 12);
      }
    }

    else if (rxByte == NIBE_DISPLAY_4) {
      byte hbuf[3];
      Serial.readBytes(hbuf, 3);
      if (hbuf[0] == 0x00 && hbuf[1] == 0x59 /*&& hbuf[2] == ??*/) {
        Serial.println("Found 0x53 (Clock) -> header bytes read payload");

        byte pbuf[20];
        printPayload(pbuf, 20);
      }
    }

    if (influxDataReady == 0x3) {
      Serial.println("WP data collected -> send to influxDB");
      influxDataReady = 0;
      writeToInfluxDB();
      sensor.clearFields();
    }
  }

  /* eait for loop delay time -> 5000ms */
  if (lastTimestamp + LOOP_DELAY_MS < millis()) {
    lastTimestamp = millis();

    /*oneMinuteCnt++;
      if (oneMinuteCnt >= 12) {
      oneMinuteCnt = 0;
      powerBuffer[powerBufferCnt] = powerIntValue;
      powerBufferCnt++;
      if (powerBufferCnt >= POWER_BUF_LIMIT) {
        powerBufferCnt = 0;
      }

      powerMeanValue = 0;
      for (int i = 0; i < POWER_BUF_LIMIT; ++i) {
        powerMeanValue = powerMeanValue + powerBuffer[i];
      }
      powerMeanValue = powerMeanValue / POWER_BUF_LIMIT;
      }*/

    digitalWrite(LED_BUILTIN, toggleLed ? HIGH : LOW);
    toggleLed = !toggleLed;

    checkHotWaterCondition();
    timeClient.update();
  }

  httpRestServer.handleClient();

  /*checkHotWaterCondition();

    timeClient.update();
    httpRestServer.handleClient();

    if (acceptNewFiringTimeLeft > 0) {
    acceptNewFiringTimeLeft--;
    }
    else {
    acceptNewFiringTimeLeft = 0;
    digitalWrite(LED_BUILTIN, HIGH);
    }

    delay(1000);*/
}
