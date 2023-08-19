// -------------------------------------------------------------------
// used libraries
// -------------------------------------------------------------------

// standard
#include <Arduino.h>
// settings
#include <SettingsGeneral.h>
#include <SettingsWiFi.h>
// additions
#include <WiFi.h>
#include <time.h>
#include <Preferences.h>
#include <nvs_flash.h>
#include <PubSubClient.h>

// -------------------------------------------------------------------
// objects
// -------------------------------------------------------------------

// NVS object
Preferences preferences;
// WiFi object
WiFiClient wifiClient;
// MQTT object
PubSubClient mqttClient(wifiClient);

// -------------------------------------------------------------------
// forward declarations (allows functions in any order)
// -------------------------------------------------------------------

void WiFiEventHandlersSetup();
void WiFiStationConnected(WiFiEvent_t event, WiFiEventInfo_t info);
void WiFiGotIP(WiFiEvent_t event, WiFiEventInfo_t info);
void WiFiStationDisconnected(WiFiEvent_t event, WiFiEventInfo_t info);
void WiFiStartConnection();
void WiFiActions();
void MQTTStartConnection();
void MQTTCallback(char* TopicName, byte* Message, unsigned int MessageLength);
void NTPGetServerTime();
void NTPDateTime();
float NTPTimeDecimal();
int NVSControlInteger(const char* DBName, const char* VariableName, bool WritingModeIsActive, int DefaultValue, int NewValue);
float NVSControlFloat(const char* DBName, const char* VariableName, bool WritingModeIsActive, float DefaultValue, float NewValue);
void NVSFormat();
void EmptySerialBuffer();

// -------------------------------------------------------------------
// functions
// -------------------------------------------------------------------

// WiFi - events
void WiFiStationConnected(WiFiEvent_t event, WiFiEventInfo_t info) {
  Serial.printf("WiFi / '%s' successfully connected with '%s'!\n", WiFiHostname, WiFiSSID);
}
void WiFiGotIP(WiFiEvent_t event, WiFiEventInfo_t info) {
  if (!(WiFi.localIP().toString() == "0.0.0.0")) {
    Serial.print("WiFi / the IP address is: ");
    Serial.println(WiFi.localIP());
    digitalWrite(LED_BUILTIN, HIGH);
    WiFiActions();
  }
  else {
    WiFiStartConnection();
  }
}
void WiFiStationDisconnected(WiFiEvent_t event, WiFiEventInfo_t info) {
  if (!isConnecting) {
    isConnecting = true;
    WiFi.removeEvent(ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
    Serial.println("WiFi / the connection was disconnected, start a new connection attempt...");
    digitalWrite(LED_BUILTIN, LOW);
    WiFiStartConnection();
  }
}

// WiFi - initilize events (except 'WiFiStationDisconnected')
void WiFiEventHandlersSetup() {
  WiFi.onEvent(WiFiStationConnected, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_CONNECTED);
  WiFi.onEvent(WiFiGotIP, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_GOT_IP);
}

// WiFi - establish connection
void WiFiStartConnection() {
  isConnecting = true;
  Serial.printf("WiFi / connection establishment with '%s'...\n", WiFiSSID);
  WiFi.hostname(WiFiHostname);
  WiFi.begin(WiFiSSID, WiFiPassword);
  delay(5000);
}

// WiFi - establish further connections + initilize event 'WiFiStationDisconnected'
void WiFiActions() {
  delay(5000);
  MQTTStartConnection();
  NTPGetServerTime();
  WiFi.onEvent(WiFiStationDisconnected, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
  OneTimeCodeExecutedDay = false;
  OneTimeCodeExecutedNight = false;
  isConnecting = false;
}

// MQTT - establish connection
void MQTTStartConnection() {
  // Set MQTT broker
  mqttClient.setServer(MQTTServer, MQTTPort);
  Serial.println("-----");
  Serial.printf("MQTT / connection establishment with MQTT broker '%s'...\n", MQTTServer);
  while (!mqttClient.connected()) {
    delay(500);
    if (mqttClient.connect("ESP32Client")) {
      Serial.printf("MQTT / connected successfully with '%s'!\n", MQTTServer);
      Serial.println("-----");
      mqttClient.subscribe(MQTTTopicLEDStatus);
      mqttClient.subscribe(MQTTTopicLEDBrightness);
      mqttClient.subscribe(MQTTTopicLEDQuickness);
      mqttClient.subscribe(MQTTTopicLEDTau);
      mqttClient.subscribe(MQTTTopicLEDColorTop);
      mqttClient.subscribe(MQTTTopicLEDColorBottom);
      mqttClient.setCallback(MQTTCallback);
    }
    else
    {
      Serial.print("MQTT / connection failes, restart attempt...: ");
      Serial.println(mqttClient.state());
    }
  }
}

// MQTT - callback function for receiving a new MQTT Message
void MQTTCallback(char* TopicName, byte* Message, unsigned int MessageLength) {
  // -------------------------------------------------------------------
  // topic is 'LEDStatus'
  // -------------------------------------------------------------------
  if (strcmp(TopicName, MQTTTopicLEDStatus) == 0) {
    LEDStatus = 0;
    // read message and store value
    for (int i=0;i<MessageLength;i++) {
      LEDStatus = LEDStatus * 10 + ((char)Message[i] -'0');
    }
    Serial.printf("MQTT / message received on topic '%s': %d\n", TopicName, LEDStatus);
    Serial.println("");
    // store 'NewValue' in NVS database
    NVSControlInteger(NVSDBName, NVSVarLEDStatus, true, NVSStdLEDStatus, LEDStatus);
    Serial.println("-----");
    //
    //
    // For test purposes only
    //
    //
    // If the received message is "1", turn on the LED
    if (Message[0] == '1') {
      digitalWrite(LED_BUILTIN, HIGH);
    }
    // If the received message is "1", turn off the LED
    else if (Message[0] == '0') {
      digitalWrite(LED_BUILTIN, LOW);
    }
  }
  // -------------------------------------------------------------------
  // topic is 'LEDBrightness'
  // -------------------------------------------------------------------
  else if (strcmp(TopicName, MQTTTopicLEDBrightness) == 0) {
    LEDBrightness = 0;
    // read message and store value
    for (int i=0;i<MessageLength;i++) {
      LEDBrightness = LEDBrightness * 10 + ((char)Message[i] -'0');
    }
    Serial.printf("MQTT / message received on topic '%s': %d\n", TopicName, LEDBrightness);
    Serial.println("");
    // store 'NewValue' in NVS database
    NVSControlInteger(NVSDBName, NVSVarLEDBrightnessDay, true, NVSStdLEDBrightnessDay, LEDBrightness);
    Serial.println("-----");
  }
  // -------------------------------------------------------------------
  // topic is 'LEDQuickness'
  // -------------------------------------------------------------------
  else if (strcmp(TopicName, MQTTTopicLEDQuickness) == 0) {
    LEDQuickness = 0;
    bool isNegative = false;
    // check if first character is minus sign
    if (Message[0] == '-') {
      isNegative = true;
    }
    // start the loop at 0 or 1, depending on whether there is a minus sign
    int startIndex = isNegative ? 1 : 0;
    // read message and store value
    for (int i=startIndex;i<MessageLength;i++) {
      LEDQuickness = LEDQuickness * 10 + ((char)Message[i] -'0');
    }
    if (isNegative) {
      LEDQuickness = LEDQuickness * -1;
    }
    Serial.printf("MQTT / message received on topic '%s': %d\n", TopicName, LEDQuickness);
    Serial.println("");
    // store 'NewValue' in NVS database
    NVSControlInteger(NVSDBName, NVSVarLEDQuicknessDay, true, NVSStdLEDQuicknessDay, LEDQuickness);
    Serial.println("-----");
  }
  // -------------------------------------------------------------------
  // topic is 'LEDTau'
  // -------------------------------------------------------------------
  else if (strcmp(TopicName, MQTTTopicLEDTau) == 0) {
    LEDTau = 0.0;
    int LEDTauTemp = 0;
    // read message and store value
    for (int i=0;i<MessageLength;i++) {
      LEDTauTemp = LEDTauTemp * 10.0 + ((char)Message[i] -'0');
    }
    // devide by 1000 to get correct float
    LEDTau = LEDTauTemp / 1000.0;
    Serial.printf("MQTT / message received on topic '%s': %.3f\n", TopicName, LEDTau);
    Serial.println("");
    // store 'NewValue' in NVS database
    NVSControlFloat(NVSDBName, NVSVarLEDTauDay, true, NVSStdLEDTauDay, LEDTau);
    Serial.println("-----");
  }
  // -------------------------------------------------------------------
  // topic is 'LEDColorTop'
  // -------------------------------------------------------------------
  else if (strcmp(TopicName, MQTTTopicLEDColorTop) == 0) {
    LEDColorTopR = 0;
    LEDColorTopG = 0;
    LEDColorTopB = 0;
    // read message and store values
    for (int i=1;i<=3;i++) {
      if ((char)Message[i] != ' ') {
        LEDColorTopR = LEDColorTopR * 10 + ((char)Message[i] -'0');
      }
    }
    for (int i=5;i<=7;i++) {
      if ((char)Message[i] != ' ') {
        LEDColorTopG = LEDColorTopG * 10 + ((char)Message[i] -'0');
      }
    }
    for (int i=9;i<=11;i++) {
      if ((char)Message[i] != ' ') {
        LEDColorTopB = LEDColorTopB * 10 + ((char)Message[i] -'0');
      }
    }
    Serial.printf("MQTT / message received on topic '%s': %d, %d, %d\n", TopicName, LEDColorTopR, LEDColorTopG, LEDColorTopB);
    Serial.println("");
    // store 'NewValue' in NVS database
    NVSControlInteger(NVSDBName, NVSVarLEDColorTopDayR, true, NVSStdLEDColorTopDayR, LEDColorTopR);
    NVSControlInteger(NVSDBName, NVSVarLEDColorTopDayG, true, NVSStdLEDColorTopDayG, LEDColorTopG);
    NVSControlInteger(NVSDBName, NVSVarLEDColorTopDayB, true, NVSStdLEDColorTopDayB, LEDColorTopB);
    Serial.println("-----");
  }
  // -------------------------------------------------------------------
  // topic is 'LEDColorBottom'
  // -------------------------------------------------------------------
  else if (strcmp(TopicName, MQTTTopicLEDColorBottom) == 0) {
    LEDColorBottomR = 0;
    LEDColorBottomG = 0;
    LEDColorBottomB = 0;
    // read message and store values
    for (int i=1;i<=3;i++) {
      if ((char)Message[i] != ' ') {
      LEDColorBottomR = LEDColorBottomR * 10 + ((char)Message[i] -'0');
      }
    }
    for (int i=5;i<=7;i++) {
      if ((char)Message[i] != ' ') {
      LEDColorBottomG = LEDColorBottomG * 10 + ((char)Message[i] -'0');
      }
    }
    for (int i=9;i<=11;i++) {
      if ((char)Message[i] != ' ') {
      LEDColorBottomB = LEDColorBottomB * 10 + ((char)Message[i] -'0');
      }
    }
    Serial.printf("MQTT / message received on topic '%s': %d, %d, %d\n", TopicName, LEDColorBottomR, LEDColorBottomG, LEDColorBottomB);
    Serial.println("");
    // store 'NewValue' in NVS database
    NVSControlInteger(NVSDBName, NVSVarLEDColorBottomDayR, true, NVSStdLEDColorBottomDayR, LEDColorBottomR);
    NVSControlInteger(NVSDBName, NVSVarLEDColorBottomDayG, true, NVSStdLEDColorBottomDayG, LEDColorBottomG);
    NVSControlInteger(NVSDBName, NVSVarLEDColorBottomDayB, true, NVSStdLEDColorBottomDayB, LEDColorBottomB);
    Serial.println("-----");
  }
}

// NTP - synchronize system time with NTP server
void NTPGetServerTime() {
  // synchronize system time with NTP server
  configTime(gmtOffset_sec, daylightOffset_sec, NTPServer);
  delay(5000);
  if (time(nullptr) > 1000) {
    Serial.print("Time / successfully synchronized: ");
    NTPDateTime();
    Serial.printf("Time / current time as decimal number: %f\n", NTPTimeDecimal());
    Serial.println("-----");
  } else {
    Serial.println("Time / error synchronizing the time, restart attempt...");
    NTPGetServerTime(); // try again...
  }
}

// NTP - output current system time as German date/time stamp
void NTPDateTime() {
  const char* WeekDays[] = {"Sonntag", "Montag", "Dienstag", "Mittwoch", "Donnerstag", "Freitag", "Samstag"};
  const char* Months[] = {"Januar", "Februar", "März", "April", "Mai", "Juni", "Juli", "August", "September", "Oktober", "November", "Dezember"};
  time_t now;
  struct tm* timeinfo;
  time(&now);
  timeinfo = localtime(&now);
  Serial.printf("%s, %d. %s %d %02d:%02d:%02d\n",
                WeekDays[timeinfo->tm_wday],
                timeinfo->tm_mday,
                Months[timeinfo->tm_mon],
                timeinfo->tm_year + 1900,
                timeinfo->tm_hour,
                timeinfo->tm_min,
                timeinfo->tm_sec);
}

// NTP - output current system time as decimal number
float NTPTimeDecimal() {
  struct tm timeinfo;
  if(!getLocalTime(&timeinfo)){
    Serial.println("Time / no system time exists!");
    return -1;
  }
  return float(timeinfo.tm_hour) + float(timeinfo.tm_min)/60;
}

// NVS - create, read and edit database variables, type integer
int NVSControlInteger(const char* DBName, const char* VariableName, bool WritingModeIsActive, int DefaultValue, int NewValue = 999999) {
  int SavedValue = 999999;
  int ReturnValue;
  // query variable in read mode
  // 'DBName' does not exist
  if(!preferences.begin(DBName, true)) {
    Serial.printf("NVS / database '%s' does not exist, therefore the output value: %d\n", DBName, SavedValue);
    ReturnValue = SavedValue;
  }
  // 'DBName' does exist
  else {
    SavedValue = preferences.getInt(VariableName, 999999);
    // 'DBName' does exist + 'VariableName' does not exist
    if (SavedValue == 999999) {
      Serial.printf("NVS / variable '%s' does not exist, therefore the output value: %d\n", VariableName, SavedValue);
      ReturnValue = SavedValue;
    }
    // 'DBName' does exist + 'VariableName' does exist
    else {
      Serial.printf("NVS / variable '%s' read with value: %d\n", VariableName, SavedValue);
      ReturnValue = SavedValue;
    }
  }
  // close storage
  preferences.end();

  // writing mode is active
  if (WritingModeIsActive) {
    // if 'DBName' does not exist, it will be automatically created now
    preferences.begin(DBName, false);
    // 'VariableName' does exist
    if (SavedValue != 999999) {
      // 'VariableName' does exist + 'NewValue' was passed
      if (NewValue != 999999) {
        // 'VariableName' does exist + 'NewValue' was passed + 'NewValue' != 'SavedValue'
        if (SavedValue != NewValue) {
          // update 'VariableName' with 'NewValue' in database
          preferences.putInt(VariableName, NewValue);
          Serial.printf("NVS / variable '%s' overwritten, old value: ", VariableName);
          Serial.print(SavedValue);
          Serial.print(", new value: ");
          Serial.print(NewValue);
          Serial.println();
          ReturnValue = NewValue;
        }
        // 'VariableName' does exist + 'NewValue' was passed + 'NewValue' = 'SavedValue'
        else {
          // no change of 'VariableName' in database
          Serial.printf("NVS / variable '%s' without change of identical value: ", VariableName);
          Serial.print(SavedValue);
          Serial.println();
          ReturnValue = SavedValue;
        }
      }
      // 'VariableName' does exist + 'NewValue' was not passed
      else {
        // no change of 'VariableName' in database
        Serial.printf("NVS / variable '%s' without change of value: ", VariableName);
        Serial.print(SavedValue);
        Serial.println();
        ReturnValue = SavedValue;
      }
    }
    // 'VariableName' does not exist
    else {
      // 'VariableName' does not exist + 'NewValue' was passed
      if (NewValue != 999999) {
        // store 'VariableName' with 'NewValue' in database
        preferences.putInt(VariableName, NewValue);
        Serial.printf("NVS / variable '%s' created with new value: ", VariableName);
        Serial.print(NewValue);
        Serial.println();
        ReturnValue = NewValue;
      }
      // 'VariableName' does not exist + 'NewValue' was not passed
      else {
        // store 'VariableName' with 'DefaultValue' in database
        preferences.putInt(VariableName, DefaultValue);
        Serial.printf("NVS / variable '%s' created with default value: ", VariableName);
        Serial.print(DefaultValue);
        Serial.println();
        ReturnValue = DefaultValue;
      }
    }
    // close storage
    preferences.end();
  }
  return ReturnValue;
}

// NVS - create, read and edit database variables, type float
float NVSControlFloat(const char* DBName, const char* VariableName, bool WritingModeIsActive, float DefaultValue, float NewValue = 999999) {
  float SavedValue = 999999;
  float ReturnValue;
  // query variable in read mode
  // 'DBName' does not exist
  if(!preferences.begin(DBName, true)) {
    Serial.printf("NVS / database '%s' does not exist, therefore the output value: %.3f\n", DBName, SavedValue);
    ReturnValue = SavedValue;
  }
  // 'DBName' does exist
  else {
    SavedValue = preferences.getFloat(VariableName, 999999);
    // 'DBName' does exist + 'VariableName' does not exist
    if (SavedValue == 999999) {
      Serial.printf("NVS / variable '%s' does not exist, therefore the output value: %.3f\n", VariableName, SavedValue);
      ReturnValue = SavedValue;
    }
    // 'DBName' does exist + 'VariableName' does exist
    else {
      Serial.printf("NVS / variable '%s' read with value: %.3f\n", VariableName, SavedValue);
      ReturnValue = SavedValue;
    }
  }
  // close storage
  preferences.end();

  // writing mode is active
  if (WritingModeIsActive) {
    // if 'DBName' does not exist, it will be automatically created now
    preferences.begin(DBName, false);
    // 'VariableName' does exist
    if (SavedValue != 999999) {
      // 'VariableName' does exist + 'NewValue' was passed
      if (NewValue != 999999) {
        // 'VariableName' does exist + 'NewValue' was passed + 'NewValue' != 'SavedValue'
        if (SavedValue != NewValue) {
          // update 'VariableName' with 'NewValue' in database
          preferences.putFloat(VariableName, NewValue);
          Serial.printf("NVS / variable '%s' overwritten, old value: ", VariableName);
          Serial.print(SavedValue, 3);
          Serial.print(", new value: ");
          Serial.print(NewValue, 3);
          Serial.println();
          ReturnValue = NewValue;
        }
        // 'VariableName' does exist + 'NewValue' was passed + 'NewValue' = 'SavedValue'
        else {
          // no change of 'VariableName' in database
          Serial.printf("NVS / variable '%s' without change of identical value: ", VariableName);
          Serial.print(SavedValue, 3);
          Serial.println();
          ReturnValue = SavedValue;
        }
      }
      // 'VariableName' does exist + 'NewValue' was not passed
      else {
        // no change of 'VariableName' in database
        Serial.printf("NVS / variable '%s' without change of value: ", VariableName);
        Serial.print(SavedValue, 3);
        Serial.println();
        ReturnValue = SavedValue;
      }
    }
    // 'VariableName' does not exist
    else {
      // 'VariableName' does not exist + 'NewValue' was passed
      if (NewValue != 999999) {
        // store 'VariableName' with 'NewValue' in database
        preferences.putFloat(VariableName, NewValue);
        Serial.printf("NVS / variable '%s' created with new value: ", VariableName);
        Serial.print(NewValue, 3);
        Serial.println();
        ReturnValue = NewValue;
      }
      // 'VariableName' does not exist + 'NewValue' was not passed
      else {
        // store 'VariableName' with 'DefaultValue' in database
        preferences.putFloat(VariableName, DefaultValue);
        Serial.printf("NVS / variable '%s' created with default value: ", VariableName);
        Serial.print(DefaultValue, 3);
        Serial.println();
        ReturnValue = DefaultValue;
      }
    }
    // close storage
    preferences.end();
  }
  return ReturnValue;
}

// NVS - Format database completely
void NVSFormat() {
  nvs_flash_erase(); // delete partition
  nvs_flash_init(); // initialize partition
  Serial.println("NVS DB / the partition was formatted!");
  Serial.println("NVS DB / comment again the function 'NVSFormat()', build and upload the program!");
  while(true); // function is intentionally stuck in infinite loop
}

// serial monitor - empty buffer to avoid overflow
void EmptySerialBuffer() {
  while (Serial.available() > 0) {
    char c = Serial.read(); // each read character is automatically removed from the buffer
  }
}

// -------------------------------------------------------------------
// one time program code for initialization
// -------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  Serial.println("-----");
  // to format the NVS, comment out the following line
  // NVSFormat();

  // initialize LED_BUILTIN as output and switch off LED
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);
  
  // establishing of connections (WiFi, MTTQ, NTP)
  WiFiEventHandlersSetup();
  WiFiStartConnection();
  
  // wait for 'WiFiActions' to complete
  while (isConnecting) {
    delay(100); // short delay to avoid unnecessary load on CPU
  }

  // check time phase
  if (NTPTimeDecimal()>=StartTimeDay && NTPTimeDecimal()<StartTimeNight) {
    isDay = true;
  }
  else {
    isDay = false;
  }

  // NVS - read stored values
  LEDStatus = NVSControlInteger(NVSDBName, NVSVarLEDStatus, false, NVSStdLEDStatus);
  if (isDay) {
    LEDBrightness = NVSControlInteger(NVSDBName, NVSVarLEDBrightnessDay, false, NVSStdLEDBrightnessDay);
    LEDQuickness = NVSControlInteger(NVSDBName, NVSVarLEDQuicknessDay, false, NVSStdLEDQuicknessDay);
    LEDTau = NVSControlFloat(NVSDBName, NVSVarLEDTauDay, false, NVSStdLEDTauDay);
    LEDColorTopR = NVSControlInteger(NVSDBName, NVSVarLEDColorTopDayR, false, NVSStdLEDColorTopDayR);
    LEDColorTopG = NVSControlInteger(NVSDBName, NVSVarLEDColorTopDayG, false, NVSStdLEDColorTopDayG);
    LEDColorTopB = NVSControlInteger(NVSDBName, NVSVarLEDColorTopDayB, false, NVSStdLEDColorTopDayB);
    LEDColorBottomR = NVSControlInteger(NVSDBName, NVSVarLEDColorBottomDayG, false, NVSStdLEDColorBottomDayR);
    LEDColorBottomG = NVSControlInteger(NVSDBName, NVSVarLEDColorBottomDayG, false, NVSStdLEDColorBottomDayG);
    LEDColorBottomB = NVSControlInteger(NVSDBName, NVSVarLEDColorBottomDayB, false, NVSStdLEDColorBottomDayB);
  }
  else {
    LEDBrightness = NVSControlInteger(NVSDBName, NVSVarLEDBrightnessNight, false, NVSStdLEDBrightnessNight);
    LEDQuickness = NVSControlInteger(NVSDBName, NVSVarLEDQuicknessNight, false, NVSStdLEDQuicknessNight);
    LEDTau = NVSControlFloat(NVSDBName, NVSVarLEDTauNight, false, NVSStdLEDTauNight);
    LEDColorTopR = NVSControlInteger(NVSDBName, NVSVarLEDColorTopNightR, false, NVSStdLEDColorTopNightR);
    LEDColorTopG = NVSControlInteger(NVSDBName, NVSVarLEDColorTopNightG, false, NVSStdLEDColorTopNightG);
    LEDColorTopB = NVSControlInteger(NVSDBName, NVSVarLEDColorTopNightB, false, NVSStdLEDColorTopNightB);
    LEDColorBottomR = NVSControlInteger(NVSDBName, NVSVarLEDColorBottomNightG, false, NVSStdLEDColorBottomNightR);
    LEDColorBottomG = NVSControlInteger(NVSDBName, NVSVarLEDColorBottomNightG, false, NVSStdLEDColorBottomNightG);
    LEDColorBottomB = NVSControlInteger(NVSDBName, NVSVarLEDColorBottomNightB, false, NVSStdLEDColorBottomNightB);
  }
  Serial.println("-----");
}

// -------------------------------------------------------------------
// program code for infinite loop
// -------------------------------------------------------------------
void loop() {
  mqttClient.loop();
  // the microcontroller runs regularly without monitor, therefore it is necessary to keep the buffer empty!
  EmptySerialBuffer();
  if (NTPTimeDecimal()>=StartTimeDay && NTPTimeDecimal()<StartTimeNight) {
    // daytime - infinite loop
    // ...

    if (!OneTimeCodeExecutedDay) {
      // daytime - one time code
      Serial.println("Daytime is active!");
      Serial.println("-----");
      OneTimeCodeExecutedDay = true; // this code was executed, don't do it again for this phase
      OneTimeCodeExecutedNight = false; // initialization for the next nighttime phase
      isDay = true;
    }
  }
  else {
    // nighttime - infinite loop
    // ...

    if (!OneTimeCodeExecutedNight) {
      // nighttime - one time code
      Serial.println("Nighttime is active!");
      Serial.println("-----");
      OneTimeCodeExecutedDay = false; // initialization for the next daytime phase
      OneTimeCodeExecutedNight = true; // this code was executed, don't do it again for this phase
      isDay = false;
    }
  }
}