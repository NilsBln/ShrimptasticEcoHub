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
#include <NeoPixelBus.h>

// -------------------------------------------------------------------
// objects
// -------------------------------------------------------------------

// NVS object
Preferences preferences;
// WiFi object
WiFiClient wifiClient;
// MQTT object
PubSubClient mqttClient(wifiClient);
// LED Strip SK6812
NeoPixelBus<NeoGrbwFeature, NeoEsp32I2s1X8Sk6812Method> LEDStrip(LEDPixelCount, LEDPin);

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
void MQTTSendSettings();
void NTPGetServerTime();
void NTPDateTime();
float NTPTimeDecimal();
bool NTPCheckTimePhase();
int NVSControlInteger(const char* DBName, const char* VariableName, bool WritingModeIsActive, int DefaultValue, int NewValue);
float NVSControlFloat(const char* DBName, const char* VariableName, bool WritingModeIsActive, float DefaultValue, float NewValue);
void NVSReadSettings(bool ReadTimeSettings, bool ReadTimePhaseSettings);
void NVSFormat();
void EmptySerialBuffer();
void LEDControl();

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
      mqttClient.subscribe(MQTTTopicStartTimeDay);
      mqttClient.subscribe(MQTTTopicStartTimeNight);
      mqttClient.subscribe(MQTTTopicLEDStatus);
      mqttClient.subscribe(MQTTTopicLEDBrightness);
      mqttClient.subscribe(MQTTTopicLEDQuickness);
      mqttClient.subscribe(MQTTTopicLEDTauThousand);
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
  // check time phase
  bool isDayPhase = NTPCheckTimePhase();
  // -------------------------------------------------------------------
  // topic is 'StartTimeDay'
  // -------------------------------------------------------------------
  if (strcmp(TopicName, MQTTTopicStartTimeDay) == 0) {
    bool minutesStart = false;
    StartTimeDayHours = 0;
    StartTimeDayMinutes = 0;
    // read message and store values
    for (int i=0;i<MessageLength;i++) {
      if (Message[i] == ':') {
        minutesStart = true;
        i++; // skip this character
      }
      if (!minutesStart) {
        StartTimeDayHours = StartTimeDayHours * 10 + ((char)Message[i] -'0');
      }
      else {
        StartTimeDayMinutes = StartTimeDayMinutes * 10 + ((char)Message[i] -'0');
      }
    }
    // build float for easier comparison with actual time
    StartTimeDay = StartTimeDayHours + static_cast<float>(StartTimeDayMinutes) / 60.0;
    Serial.printf("MQTT / message received on topic '%s': %.3f\n", TopicName, StartTimeDay);
    Serial.println("");
    // store 'NewValue' in NVS database, but hours and minutes separately for higher precision
    NVSControlInteger(NVSDBName, NVSVarStartTimeDayHours, true, NVSStdStartTimeDayHours, StartTimeDayHours);
    NVSControlInteger(NVSDBName, NVSVarStartTimeDayMinutes, true, NVSStdStartTimeDayMinutes, StartTimeDayMinutes);
    Serial.println("-----");
  }
  // -------------------------------------------------------------------
  // topic is 'StartTimeNight'
  // -------------------------------------------------------------------
  else if (strcmp(TopicName, MQTTTopicStartTimeNight) == 0) {
    bool minutesStart = false;
    StartTimeNightHours = 0;
    StartTimeNightMinutes = 0;
    // read message and store values
    for (int i=0;i<MessageLength;i++) {
      if (Message[i] == ':') {
        minutesStart = true;
        i++; // skip this character
      }
      if (!minutesStart) {
        StartTimeNightHours = StartTimeNightHours * 10 + ((char)Message[i] -'0');
      }
      else {
        StartTimeNightMinutes = StartTimeNightMinutes * 10 + ((char)Message[i] -'0');
      }
    }
    // build float for easier comparison with actual time
    StartTimeNight = StartTimeNightHours + static_cast<float>(StartTimeNightMinutes) / 60.0;
    Serial.printf("MQTT / message received on topic '%s': %.3f\n", TopicName, StartTimeNight);
    Serial.println("");
    // store 'NewValue' in NVS database, but hours and minutes separately for higher precision
    NVSControlInteger(NVSDBName, NVSVarStartTimeNightHours, true, NVSStdStartTimeNightHours, StartTimeNightHours);
    NVSControlInteger(NVSDBName, NVSVarStartTimeNightMinutes, true, NVSStdStartTimeNightMinutes, StartTimeNightMinutes);
    Serial.println("-----");
  }
  // -------------------------------------------------------------------
  // topic is 'LEDStatus', day and night
  // -------------------------------------------------------------------
  else if (strcmp(TopicName, MQTTTopicLEDStatus) == 0) {
    LEDStatus = 0;
    // read message and store value
    for (int i=0;i<MessageLength;i++) {
      LEDStatus = LEDStatus * 10 + ((char)Message[i] -'0');
    }
    Serial.printf("MQTT / message received on topic '%s': %d\n", TopicName, LEDStatus);
    Serial.println("");
    // store 'NewValue' in NVS database according to time phase
    if (isDayPhase) {
      NVSControlInteger(NVSDBName, NVSVarLEDStatusDay, true, NVSStdLEDStatusDay, LEDStatus);
    }
    else {
      NVSControlInteger(NVSDBName, NVSVarLEDStatusNight, true, NVSStdLEDStatusNight, LEDStatus);
    }
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
  // topic is 'LEDBrightness', day and night
  // -------------------------------------------------------------------
  else if (strcmp(TopicName, MQTTTopicLEDBrightness) == 0) {
    LEDBrightness = 0;
    // read message and store value
    for (int i=0;i<MessageLength;i++) {
      LEDBrightness = LEDBrightness * 10 + ((char)Message[i] -'0');
    }
    Serial.printf("MQTT / message received on topic '%s': %d\n", TopicName, LEDBrightness);
    Serial.println("");
    // store 'NewValue' in NVS database according to time phase
    if (isDayPhase) {
      NVSControlInteger(NVSDBName, NVSVarLEDBrightnessDay, true, NVSStdLEDBrightnessDay, LEDBrightness);
    }
    else {
      NVSControlInteger(NVSDBName, NVSVarLEDBrightnessNight, true, NVSStdLEDBrightnessNight, LEDBrightness);
    }
    Serial.println("-----");
  }
  // -------------------------------------------------------------------
  // topic is 'LEDQuickness', day and night
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
    // store 'NewValue' in NVS database according to time phase
    if (isDayPhase) {
      NVSControlInteger(NVSDBName, NVSVarLEDQuicknessDay, true, NVSStdLEDQuicknessDay, LEDQuickness);
    }
    else {
      NVSControlInteger(NVSDBName, NVSVarLEDQuicknessNight, true, NVSStdLEDQuicknessNight, LEDQuickness);
    }
    Serial.println("-----");
  }
  // -------------------------------------------------------------------
  // topic is 'LEDTauThousand', day and night
  // -------------------------------------------------------------------
  else if (strcmp(TopicName, MQTTTopicLEDTauThousand) == 0) {
    LEDTauThousand = 0;
    // read message and store value
    for (int i=0;i<MessageLength;i++) {
      LEDTauThousand = LEDTauThousand * 10.0 + ((char)Message[i] -'0');
    }
    // build float (devide integer by 1000) for LED program
    LEDTau = LEDTauThousand / 1000.0;
    Serial.printf("MQTT / message received on topic '%s': %.3f\n", TopicName, LEDTau);
    Serial.println("");
    // store 'NewValue' in NVS database according to time phase
    if (isDayPhase) {
      NVSControlInteger(NVSDBName, NVSVarLEDTauDay, true, NVSStdLEDTauDay, LEDTauThousand);
    }
    else {
      NVSControlInteger(NVSDBName, NVSVarLEDTauNight, true, NVSStdLEDTauNight, LEDTauThousand);
    }
    Serial.println("-----");
  }
  // -------------------------------------------------------------------
  // topic is 'LEDColorTop', day and night
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
    // store 'NewValue' in NVS database according to time phase
    if (isDayPhase) {
      NVSControlInteger(NVSDBName, NVSVarLEDColorTopDayR, true, NVSStdLEDColorTopDayR, LEDColorTopR);
      NVSControlInteger(NVSDBName, NVSVarLEDColorTopDayG, true, NVSStdLEDColorTopDayG, LEDColorTopG);
      NVSControlInteger(NVSDBName, NVSVarLEDColorTopDayB, true, NVSStdLEDColorTopDayB, LEDColorTopB);
    }
    else {
      NVSControlInteger(NVSDBName, NVSVarLEDColorTopNightR, true, NVSStdLEDColorTopNightR, LEDColorTopR);
      NVSControlInteger(NVSDBName, NVSVarLEDColorTopNightG, true, NVSStdLEDColorTopNightG, LEDColorTopG);
      NVSControlInteger(NVSDBName, NVSVarLEDColorTopNightB, true, NVSStdLEDColorTopNightB, LEDColorTopB);
    }
    Serial.println("-----");
  }
  // -------------------------------------------------------------------
  // topic is 'LEDColorBottom', day and night
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
    // store 'NewValue' in NVS database according to time phase
    if (isDayPhase) {
      NVSControlInteger(NVSDBName, NVSVarLEDColorBottomDayR, true, NVSStdLEDColorBottomDayR, LEDColorBottomR);
      NVSControlInteger(NVSDBName, NVSVarLEDColorBottomDayG, true, NVSStdLEDColorBottomDayG, LEDColorBottomG);
      NVSControlInteger(NVSDBName, NVSVarLEDColorBottomDayB, true, NVSStdLEDColorBottomDayB, LEDColorBottomB);
    }
    else {
      NVSControlInteger(NVSDBName, NVSVarLEDColorBottomNightR, true, NVSStdLEDColorBottomNightR, LEDColorBottomR);
      NVSControlInteger(NVSDBName, NVSVarLEDColorBottomNightG, true, NVSStdLEDColorBottomNightG, LEDColorBottomG);
      NVSControlInteger(NVSDBName, NVSVarLEDColorBottomNightB, true, NVSStdLEDColorBottomNightB, LEDColorBottomB);
    }
    Serial.println("-----");
  }
}

// MQTT - send new setting on time phase shift
void MQTTSendSettings() {
  std::string message;
  std::string PaddingR;
  std::string PaddingG;
  std::string PaddingB;

  auto resetVariables = [&]() {
    message = "";
    PaddingR = "";
    PaddingG = "";
    PaddingB = "";
  };

  // StartTimeDay
  message = std::to_string(StartTimeDayHours) + ":" + (StartTimeDayMinutes < 10 ? "0" : "") + std::to_string(StartTimeDayMinutes);
  mqttClient.publish(MQTTTopicStartTimeDay, message.c_str());
  resetVariables();

  // StartTimeNight
  message = std::to_string(StartTimeNightHours) + ":" + (StartTimeNightMinutes < 10 ? "0" : "") + std::to_string(StartTimeNightMinutes);
  mqttClient.publish(MQTTTopicStartTimeNight, message.c_str());
  resetVariables();

  // TimePhase
  message = std::to_string(TimePhase);
  mqttClient.publish(MQTTTopicTimePhase, message.c_str());
  resetVariables();

  // LEDStatus
  message = std::to_string(LEDStatus);
  mqttClient.publish(MQTTTopicLEDStatus, message.c_str());
  resetVariables();

  // LEDBrightness
  message = std::to_string(LEDBrightness);
  mqttClient.publish(MQTTTopicLEDBrightness, message.c_str());
  resetVariables();

  // LEDQuickness
  message = std::to_string(LEDQuickness);
  mqttClient.publish(MQTTTopicLEDQuickness, message.c_str());
  resetVariables();

  // LEDTau
  message = std::to_string(LEDTauThousand);
  mqttClient.publish(MQTTTopicLEDTauThousand, message.c_str());
  resetVariables();

  // LEDColorTop
  PaddingR = (LEDColorTopR < 10 ? "  " : (LEDColorTopR < 100 ? " " : ""));
  PaddingG = (LEDColorTopG < 10 ? "  " : (LEDColorTopG < 100 ? " " : ""));
  PaddingB = (LEDColorTopB < 10 ? "  " : (LEDColorTopB < 100 ? " " : ""));
  message = "[" +
            PaddingR + std::to_string(LEDColorTopR) + "," +
            PaddingG + std::to_string(LEDColorTopG) + "," +
            PaddingB + std::to_string(LEDColorTopB) + "]";
  mqttClient.publish(MQTTTopicLEDColorTop, message.c_str());
  resetVariables();

  // LEDColorBottom
  PaddingR = (LEDColorBottomR < 10 ? "  " : (LEDColorBottomR < 100 ? " " : ""));
  PaddingG = (LEDColorBottomG < 10 ? "  " : (LEDColorBottomG < 100 ? " " : ""));
  PaddingB = (LEDColorBottomB < 10 ? "  " : (LEDColorBottomB < 100 ? " " : ""));
  message = "[" +
            PaddingR + std::to_string(LEDColorBottomR) + "," +
            PaddingG + std::to_string(LEDColorBottomG) + "," +
            PaddingB + std::to_string(LEDColorBottomB) + "]";
  mqttClient.publish(MQTTTopicLEDColorBottom, message.c_str());
  resetVariables();
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

// NTP - check day phase
bool NTPCheckTimePhase() {
  bool isDayPhase = ((NTPTimeDecimal() >= StartTimeDay && NTPTimeDecimal() < StartTimeNight) ||
                    (NTPTimeDecimal() >= StartTimeDay && StartTimeNight < StartTimeDay) ||
                    (NTPTimeDecimal() < StartTimeNight && StartTimeDay > StartTimeNight));
  TimePhase = isDayPhase ? 1 : 0;
  return isDayPhase;
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

// NVS - read stored values from NVS depending on daytime
void NVSReadSettings(bool ReadTimeSettings, bool ReadTimePhaseSettings) {
  if (ReadTimeSettings) {
    StartTimeDayHours = NVSControlInteger(NVSDBName, NVSVarStartTimeDayHours, true, NVSStdStartTimeDayHours);
    StartTimeDayMinutes = NVSControlInteger(NVSDBName, NVSVarStartTimeDayMinutes, true, NVSStdStartTimeDayMinutes);
    StartTimeNightHours = NVSControlInteger(NVSDBName, NVSVarStartTimeNightHours, true, NVSStdStartTimeNightHours);
    StartTimeNightMinutes = NVSControlInteger(NVSDBName, NVSVarStartTimeNightMinutes, true, NVSStdStartTimeNightMinutes);
    // build floats for easier comparison with actual time
    StartTimeDay = StartTimeDayHours + static_cast<float>(StartTimeDayMinutes) / 60.0;
    StartTimeNight = StartTimeNightHours + static_cast<float>(StartTimeNightMinutes) / 60.0;
    Serial.println("-----");
  }
  if (ReadTimePhaseSettings) {
    // check time phase
    bool isDayPhase = NTPCheckTimePhase();
    if (isDayPhase) {
      LEDStatus = NVSControlInteger(NVSDBName, NVSVarLEDStatusDay, true, NVSStdLEDStatusDay);
      LEDBrightness = NVSControlInteger(NVSDBName, NVSVarLEDBrightnessDay, true, NVSStdLEDBrightnessDay);
      LEDQuickness = NVSControlInteger(NVSDBName, NVSVarLEDQuicknessDay, true, NVSStdLEDQuicknessDay);
      LEDTauThousand = NVSControlInteger(NVSDBName, NVSVarLEDTauDay, true, NVSStdLEDTauDay);
      // build float (devide integer by 1000) for LED program
      LEDTau = LEDTauThousand / 1000.0;
      LEDColorTopR = NVSControlInteger(NVSDBName, NVSVarLEDColorTopDayR, true, NVSStdLEDColorTopDayR);
      LEDColorTopG = NVSControlInteger(NVSDBName, NVSVarLEDColorTopDayG, true, NVSStdLEDColorTopDayG);
      LEDColorTopB = NVSControlInteger(NVSDBName, NVSVarLEDColorTopDayB, true, NVSStdLEDColorTopDayB);
      LEDColorBottomR = NVSControlInteger(NVSDBName, NVSVarLEDColorBottomDayR, true, NVSStdLEDColorBottomDayR);
      LEDColorBottomG = NVSControlInteger(NVSDBName, NVSVarLEDColorBottomDayG, true, NVSStdLEDColorBottomDayG);
      LEDColorBottomB = NVSControlInteger(NVSDBName, NVSVarLEDColorBottomDayB, true, NVSStdLEDColorBottomDayB);
      MQTTSendSettings();
      Serial.println("LED / daytime settings loaded");
      Serial.println("-----");
    }
    else {
      LEDStatus = NVSControlInteger(NVSDBName, NVSVarLEDStatusNight, true, NVSStdLEDStatusNight);
      LEDBrightness = NVSControlInteger(NVSDBName, NVSVarLEDBrightnessNight, true, NVSStdLEDBrightnessNight);
      LEDQuickness = NVSControlInteger(NVSDBName, NVSVarLEDQuicknessNight, true, NVSStdLEDQuicknessNight);
      LEDTauThousand = NVSControlInteger(NVSDBName, NVSVarLEDTauNight, true, NVSStdLEDTauNight);
      // build float (devide integer by 1000) for LED program
      LEDTau = LEDTauThousand / 1000.0;
      LEDColorTopR = NVSControlInteger(NVSDBName, NVSVarLEDColorTopNightR, true, NVSStdLEDColorTopNightR);
      LEDColorTopG = NVSControlInteger(NVSDBName, NVSVarLEDColorTopNightG, true, NVSStdLEDColorTopNightG);
      LEDColorTopB = NVSControlInteger(NVSDBName, NVSVarLEDColorTopNightB, true, NVSStdLEDColorTopNightB);
      LEDColorBottomR = NVSControlInteger(NVSDBName, NVSVarLEDColorBottomNightR, true, NVSStdLEDColorBottomNightR);
      LEDColorBottomG = NVSControlInteger(NVSDBName, NVSVarLEDColorBottomNightG, true, NVSStdLEDColorBottomNightG);
      LEDColorBottomB = NVSControlInteger(NVSDBName, NVSVarLEDColorBottomNightB, true, NVSStdLEDColorBottomNightB);
      MQTTSendSettings();
      Serial.println("LED / nighttime settings loaded");
      Serial.println("-----");
    }
  }
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

// LED - amazing color gradient
void LEDControl() {
  // set color
  //LEDStrip.SetPixelColor(0, RgbwColor(LEDColorBottomR, LEDColorBottomG, LEDColorBottomB, 0));
  //LEDStrip.SetPixelColor(41, RgbwColor(LEDColorTopR, LEDColorTopG, LEDColorTopB, 0));
  // show color
  //LEDStrip.Show();
  Serial.println("LED / light has been adjusted");
  Serial.println("-----");
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

  // NVS - read time settings
  NVSReadSettings(true, false);

  // initialize LEDStrip
  //LEDStrip.Begin();
  //LEDStrip.Show();

}

// -------------------------------------------------------------------
// program code for infinite loop
// -------------------------------------------------------------------
void loop() {
  mqttClient.loop();
  // the microcontroller runs regularly without monitor, therefore it is necessary to keep the buffer empty!
  EmptySerialBuffer();
  // check time phase
  bool isDayPhase = NTPCheckTimePhase();
  if (isDayPhase) {
    // daytime - infinite loop
    // ...

    if (!OneTimeCodeExecutedDay) {
      // daytime - one time code
      Serial.println("Status / daytime is active!");
      Serial.println("-----");
      OneTimeCodeExecutedDay = true; // this code was executed, don't do it again for this phase
      OneTimeCodeExecutedNight = false; // initialization for the next nighttime phase
      NVSReadSettings(false, true); // phase changed, read new time phase settings
      LEDControl();
    }
  }
  else {
    // nighttime - infinite loop
    // ...

    if (!OneTimeCodeExecutedNight) {
      // nighttime - one time code
      Serial.println("Status / nighttime is active!");
      Serial.println("-----");
      OneTimeCodeExecutedDay = false; // initialization for the next daytime phase
      OneTimeCodeExecutedNight = true; // this code was executed, don't do it again for this phase
      NVSReadSettings(false, true); // phase changed, read new time phase settings
      LEDControl();
    }
  }
}