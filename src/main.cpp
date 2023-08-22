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
void LEDColorControl();

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
      mqttClient.subscribe(MQTTTopicLEDAmplifier);
      mqttClient.subscribe(MQTTTopicLEDTauThousand);
      mqttClient.subscribe(MQTTTopicLEDColorTop);
      mqttClient.subscribe(MQTTTopicLEDColorBottom);
      mqttClient.subscribe(MQTTTopicLEDColorWhite);
      mqttClient.subscribe(MQTTTopicUpdate);
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
    int StartTimeDayHoursNew = 0;
    int StartTimeDayMinutesNew = 0;
    // read message and store values
    for (int i=0;i<MessageLength;i++) {
      if (Message[i] == ':') {
        minutesStart = true;
        i++; // skip this character
      }
      if (!minutesStart) {
        StartTimeDayHoursNew = StartTimeDayHoursNew * 10 + ((char)Message[i] -'0');
      }
      else {
        StartTimeDayMinutesNew = StartTimeDayMinutesNew * 10 + ((char)Message[i] -'0');
      }
    }
    if (StartTimeDayHours != StartTimeDayHoursNew || StartTimeDayMinutes != StartTimeDayMinutesNew) {
      StartTimeDayHours = StartTimeDayHoursNew;
      StartTimeDayMinutes = StartTimeDayMinutesNew;
      // build float for easier comparison with actual time
      StartTimeDay = StartTimeDayHours + static_cast<float>(StartTimeDayMinutes) / 60.0;
      Serial.printf("MQTT / message received on topic '%s': %.3f\n", TopicName, StartTimeDay);
      Serial.println("-----");
      // store 'NewValue' in NVS database, but hours and minutes separately for higher precision
      NVSControlInteger(NVSDBName, NVSVarStartTimeDayHours, true, NVSStdStartTimeDayHours, StartTimeDayHours);
      NVSControlInteger(NVSDBName, NVSVarStartTimeDayMinutes, true, NVSStdStartTimeDayMinutes, StartTimeDayMinutes);
      Serial.println("-----");
    }
    else {
      Serial.printf("MQTT / identical incoming message for '%s' ignored!\n", TopicName);
      Serial.println("-----");
    }
  }
  // -------------------------------------------------------------------
  // topic is 'StartTimeNight'
  // -------------------------------------------------------------------
  else if (strcmp(TopicName, MQTTTopicStartTimeNight) == 0) {
    bool minutesStart = false;
    int StartTimeNightHoursNew = 0;
    int StartTimeNightMinutesNew = 0;
    // read message and store values
    for (int i=0;i<MessageLength;i++) {
      if (Message[i] == ':') {
        minutesStart = true;
        i++; // skip this character
      }
      if (!minutesStart) {
        StartTimeNightHoursNew = StartTimeNightHoursNew * 10 + ((char)Message[i] -'0');
      }
      else {
        StartTimeNightMinutesNew = StartTimeNightMinutesNew * 10 + ((char)Message[i] -'0');
      }
    }
    if (StartTimeNightHours != StartTimeNightHoursNew || StartTimeNightMinutes != StartTimeNightMinutesNew) {
      StartTimeNightHours = StartTimeNightHoursNew;
      StartTimeNightMinutes = StartTimeNightMinutesNew;
      // build float for easier comparison with actual time
      StartTimeNight = StartTimeNightHours + static_cast<float>(StartTimeNightMinutes) / 60.0;
      Serial.printf("MQTT / message received on topic '%s': %.3f\n", TopicName, StartTimeNight);
      Serial.println("-----");
      // store 'NewValue' in NVS database, but hours and minutes separately for higher precision
      NVSControlInteger(NVSDBName, NVSVarStartTimeNightHours, true, NVSStdStartTimeNightHours, StartTimeNightHours);
      NVSControlInteger(NVSDBName, NVSVarStartTimeNightMinutes, true, NVSStdStartTimeNightMinutes, StartTimeNightMinutes);
      Serial.println("-----");
    }
    else {
      Serial.printf("MQTT / identical incoming message for '%s' ignored!\n", TopicName);
      Serial.println("-----");
    }
  }
  // -------------------------------------------------------------------
  // topic is 'LEDStatus', day and night
  // -------------------------------------------------------------------
  else if (strcmp(TopicName, MQTTTopicLEDStatus) == 0) {
    int LEDStatusNew = 0;
    // read message and store value
    for (int i=0;i<MessageLength;i++) {
      LEDStatusNew = LEDStatusNew * 10 + ((char)Message[i] -'0');
    }
    if (LEDStatus != LEDStatusNew) {
      LEDStatus = LEDStatusNew;
      Serial.printf("MQTT / message received on topic '%s': %d\n", TopicName, LEDStatus);
      Serial.println("-----");
      // store 'NewValue' in NVS database according to time phase
      if (isDayPhase) {
        NVSControlInteger(NVSDBName, NVSVarLEDStatusDay, true, NVSStdLEDStatusDay, LEDStatus);
      }
      else {
        NVSControlInteger(NVSDBName, NVSVarLEDStatusNight, true, NVSStdLEDStatusNight, LEDStatus);
      }
      Serial.println("-----");
      LEDColorControl();
    }
    else {
      Serial.printf("MQTT / identical incoming message for '%s' ignored!\n", TopicName);
      Serial.println("-----");
    }
  }
  // -------------------------------------------------------------------
  // topic is 'LEDBrightness', day and night
  // -------------------------------------------------------------------
  else if (strcmp(TopicName, MQTTTopicLEDBrightness) == 0) {
    int LEDBrightnessNew = 0;
    // read message and store value
    for (int i=0;i<MessageLength;i++) {
      LEDBrightnessNew = LEDBrightnessNew * 10 + ((char)Message[i] -'0');
    }
    if (LEDBrightness != LEDBrightnessNew) {
      LEDBrightness = LEDBrightnessNew;
      Serial.printf("MQTT / message received on topic '%s': %d\n", TopicName, LEDBrightness);
      Serial.println("-----");
      // store 'NewValue' in NVS database according to time phase
      if (isDayPhase) {
        NVSControlInteger(NVSDBName, NVSVarLEDBrightnessDay, true, NVSStdLEDBrightnessDay, LEDBrightness);
      }
      else {
        NVSControlInteger(NVSDBName, NVSVarLEDBrightnessNight, true, NVSStdLEDBrightnessNight, LEDBrightness);
      }
      Serial.println("-----");
      LEDColorControl();
    }
    else {
      Serial.printf("MQTT / identical incoming message for '%s' ignored!\n", TopicName);
      Serial.println("-----");
    }
  }
  // -------------------------------------------------------------------
  // topic is 'LEDAmplifier', day and night
  // -------------------------------------------------------------------
  else if (strcmp(TopicName, MQTTTopicLEDAmplifier) == 0) {
    int LEDAmplifierNew = 0;
    bool isNegative = false;
    // check if first character is minus sign
    if (Message[0] == '-') {
      isNegative = true;
    }
    // start the loop at 0 or 1, depending on whether there is a minus sign
    int startIndex = isNegative ? 1 : 0;
    // read message and store value
    for (int i=startIndex;i<MessageLength;i++) {
      LEDAmplifierNew = LEDAmplifierNew * 10 + ((char)Message[i] -'0');
    }
    if (isNegative) {
      LEDAmplifierNew = LEDAmplifierNew * -1;
    }
    if (LEDAmplifier != LEDAmplifierNew) {
      LEDAmplifier = LEDAmplifierNew;
      Serial.printf("MQTT / message received on topic '%s': %d\n", TopicName, LEDAmplifier);
      Serial.println("-----");
      // store 'NewValue' in NVS database according to time phase
      if (isDayPhase) {
        NVSControlInteger(NVSDBName, NVSVarLEDAmplifierDay, true, NVSStdLEDAmplifierDay, LEDAmplifier);
      }
      else {
        NVSControlInteger(NVSDBName, NVSVarLEDAmplifierNight, true, NVSStdLEDAmplifierNight, LEDAmplifier);
      }
      Serial.println("-----");
      LEDColorControl();
    }
    else {
      Serial.printf("MQTT / identical incoming message for '%s' ignored!\n", TopicName);
      Serial.println("-----");
    }
  }
  // -------------------------------------------------------------------
  // topic is 'LEDTauThousand', day and night
  // -------------------------------------------------------------------
  else if (strcmp(TopicName, MQTTTopicLEDTauThousand) == 0) {
    int LEDTauThousandNew = 0;
    // read message and store value
    for (int i=0;i<MessageLength;i++) {
      LEDTauThousandNew = LEDTauThousandNew * 10.0 + ((char)Message[i] -'0');
    }
    if(LEDTauThousand != LEDTauThousandNew){
      LEDTauThousand = LEDTauThousandNew;
      // build float (devide integer by 1000) for LED program
      LEDTau = LEDTauThousand / 1000.0;
      Serial.printf("MQTT / message received on topic '%s': %.3f\n", TopicName, LEDTau);
      Serial.println("-----");
      // store 'NewValue' in NVS database according to time phase
      if (isDayPhase) {
        NVSControlInteger(NVSDBName, NVSVarLEDTauDay, true, NVSStdLEDTauDay, LEDTauThousand);
      }
      else {
        NVSControlInteger(NVSDBName, NVSVarLEDTauNight, true, NVSStdLEDTauNight, LEDTauThousand);
      }
      Serial.println("-----");
      LEDColorControl();
    }
    else {
      Serial.printf("MQTT / identical incoming message for '%s' ignored!\n", TopicName);
      Serial.println("-----");
    }
  }
  // -------------------------------------------------------------------
  // topic is 'LEDColorTop', day and night
  // -------------------------------------------------------------------
  else if (strcmp(TopicName, MQTTTopicLEDColorTop) == 0) {
    int LEDColorTopRNew = 0;
    int LEDColorTopGNew = 0;
    int LEDColorTopBNew = 0;
    // read message and store values
    for (int i=1;i<=3;i++) {
      if ((char)Message[i] != ' ') {
        LEDColorTopRNew = LEDColorTopRNew * 10 + ((char)Message[i] -'0');
      }
    }
    for (int i=5;i<=7;i++) {
      if ((char)Message[i] != ' ') {
        LEDColorTopGNew = LEDColorTopGNew * 10 + ((char)Message[i] -'0');
      }
    }
    for (int i=9;i<=11;i++) {
      if ((char)Message[i] != ' ') {
        LEDColorTopBNew = LEDColorTopBNew * 10 + ((char)Message[i] -'0');
      }
    }
    if (LEDColorTopR != LEDColorTopRNew || LEDColorTopG != LEDColorTopGNew || LEDColorTopB != LEDColorTopBNew) {
      LEDColorTopR = LEDColorTopRNew;
      LEDColorTopG = LEDColorTopGNew;
      LEDColorTopB = LEDColorTopBNew;
      Serial.printf("MQTT / message received on topic '%s': %d, %d, %d\n", TopicName, LEDColorTopR, LEDColorTopG, LEDColorTopB);
      Serial.println("-----");
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
      LEDColorControl();
    }
    else {
      Serial.printf("MQTT / identical incoming message for '%s' ignored!\n", TopicName);
      Serial.println("-----");
    }
  }
  // -------------------------------------------------------------------
  // topic is 'LEDColorBottom', day and night
  // -------------------------------------------------------------------
  else if (strcmp(TopicName, MQTTTopicLEDColorBottom) == 0) {
    int LEDColorBottomRNew = 0;
    int LEDColorBottomGNew = 0;
    int LEDColorBottomBNew = 0;
    // read message and store values
    for (int i=1;i<=3;i++) {
      if ((char)Message[i] != ' ') {
      LEDColorBottomRNew = LEDColorBottomRNew * 10 + ((char)Message[i] -'0');
      }
    }
    for (int i=5;i<=7;i++) {
      if ((char)Message[i] != ' ') {
      LEDColorBottomGNew = LEDColorBottomGNew * 10 + ((char)Message[i] -'0');
      }
    }
    for (int i=9;i<=11;i++) {
      if ((char)Message[i] != ' ') {
      LEDColorBottomBNew = LEDColorBottomBNew * 10 + ((char)Message[i] -'0');
      }
    }
    if (LEDColorBottomR != LEDColorBottomRNew || LEDColorBottomG != LEDColorBottomGNew || LEDColorBottomB != LEDColorBottomBNew) {
      LEDColorBottomR = LEDColorBottomRNew;
      LEDColorBottomG = LEDColorBottomGNew;
      LEDColorBottomB = LEDColorBottomBNew;
      Serial.printf("MQTT / message received on topic '%s': %d, %d, %d\n", TopicName, LEDColorBottomR, LEDColorBottomG, LEDColorBottomB);
      Serial.println("-----");
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
      LEDColorControl();
    }
    else {
      Serial.printf("MQTT / identical incoming message for '%s' ignored!\n", TopicName);
      Serial.println("-----");
    }
  }
  // -------------------------------------------------------------------
  // topic is 'LEDColorWhite', day and night
  // -------------------------------------------------------------------
  else if (strcmp(TopicName, MQTTTopicLEDColorWhite) == 0) {
    int LEDColorWhiteNew = 0;
    // read message and store value
    for (int i=0;i<MessageLength;i++) {
      LEDColorWhiteNew = LEDColorWhiteNew * 10 + ((char)Message[i] -'0');
    }
    if (LEDColorWhite != LEDColorWhiteNew) {
      LEDColorWhite = LEDColorWhiteNew;
      Serial.printf("MQTT / message received on topic '%s': %d\n", TopicName, LEDColorWhite);
      Serial.println("-----");
      // store 'NewValue' in NVS database according to time phase
      if (isDayPhase) {
        NVSControlInteger(NVSDBName, NVSVarLEDColorWhiteDay, true, NVSStdLEDColorWhiteDay, LEDColorWhite);
      }
      else {
        NVSControlInteger(NVSDBName, NVSVarLEDColorWhiteNight, true, NVSStdLEDColorWhiteNight, LEDColorWhite);
      }
      Serial.println("-----");
      LEDColorControl();
    }
    else {
      Serial.printf("MQTT / identical incoming message for '%s' ignored!\n", TopicName);
      Serial.println("-----");
    }
  }
  // -------------------------------------------------------------------
  // topic is 'Update'
  // -------------------------------------------------------------------
  else if (strcmp(TopicName, MQTTTopicUpdate) == 0) {
    Serial.printf("MQTT / message received on topic '%s'\n", TopicName);
    Serial.println("MQTT / start sending settings...");
    Serial.println("-----");
    MQTTSendSettings();
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

  // LEDAmplifier
  message = std::to_string(LEDAmplifier);
  mqttClient.publish(MQTTTopicLEDAmplifier, message.c_str());
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

  // LEDColorWhite
  message = std::to_string(LEDColorWhite);
  mqttClient.publish(MQTTTopicLEDColorWhite, message.c_str());
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

// NTP - check time phase
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
    Serial.println("Configuration / timer settings loaded!");
    Serial.println("-----");
  }
  if (ReadTimePhaseSettings) {
    // check time phase
    bool isDayPhase = NTPCheckTimePhase();
    if (isDayPhase) {
      LEDStatus = NVSControlInteger(NVSDBName, NVSVarLEDStatusDay, true, NVSStdLEDStatusDay);
      LEDBrightness = NVSControlInteger(NVSDBName, NVSVarLEDBrightnessDay, true, NVSStdLEDBrightnessDay);
      LEDAmplifier = NVSControlInteger(NVSDBName, NVSVarLEDAmplifierDay, true, NVSStdLEDAmplifierDay);
      LEDTauThousand = NVSControlInteger(NVSDBName, NVSVarLEDTauDay, true, NVSStdLEDTauDay);
      // build float (devide integer by 1000) for LED program
      LEDTau = LEDTauThousand / 1000.0;
      LEDColorTopR = NVSControlInteger(NVSDBName, NVSVarLEDColorTopDayR, true, NVSStdLEDColorTopDayR);
      LEDColorTopG = NVSControlInteger(NVSDBName, NVSVarLEDColorTopDayG, true, NVSStdLEDColorTopDayG);
      LEDColorTopB = NVSControlInteger(NVSDBName, NVSVarLEDColorTopDayB, true, NVSStdLEDColorTopDayB);
      LEDColorBottomR = NVSControlInteger(NVSDBName, NVSVarLEDColorBottomDayR, true, NVSStdLEDColorBottomDayR);
      LEDColorBottomG = NVSControlInteger(NVSDBName, NVSVarLEDColorBottomDayG, true, NVSStdLEDColorBottomDayG);
      LEDColorBottomB = NVSControlInteger(NVSDBName, NVSVarLEDColorBottomDayB, true, NVSStdLEDColorBottomDayB);
      LEDColorWhite = NVSControlInteger(NVSDBName, NVSVarLEDColorWhiteDay, true, NVSStdLEDColorWhiteDay);
      MQTTSendSettings();
      Serial.println("-----");
      Serial.println("Configuration / daytime LED settings loaded!");
      Serial.println("-----");
    }
    else {
      LEDStatus = NVSControlInteger(NVSDBName, NVSVarLEDStatusNight, true, NVSStdLEDStatusNight);
      LEDBrightness = NVSControlInteger(NVSDBName, NVSVarLEDBrightnessNight, true, NVSStdLEDBrightnessNight);
      LEDAmplifier = NVSControlInteger(NVSDBName, NVSVarLEDAmplifierNight, true, NVSStdLEDAmplifierNight);
      LEDTauThousand = NVSControlInteger(NVSDBName, NVSVarLEDTauNight, true, NVSStdLEDTauNight);
      // build float (devide integer by 1000) for LED program
      LEDTau = LEDTauThousand / 1000.0;
      LEDColorTopR = NVSControlInteger(NVSDBName, NVSVarLEDColorTopNightR, true, NVSStdLEDColorTopNightR);
      LEDColorTopG = NVSControlInteger(NVSDBName, NVSVarLEDColorTopNightG, true, NVSStdLEDColorTopNightG);
      LEDColorTopB = NVSControlInteger(NVSDBName, NVSVarLEDColorTopNightB, true, NVSStdLEDColorTopNightB);
      LEDColorBottomR = NVSControlInteger(NVSDBName, NVSVarLEDColorBottomNightR, true, NVSStdLEDColorBottomNightR);
      LEDColorBottomG = NVSControlInteger(NVSDBName, NVSVarLEDColorBottomNightG, true, NVSStdLEDColorBottomNightG);
      LEDColorBottomB = NVSControlInteger(NVSDBName, NVSVarLEDColorBottomNightB, true, NVSStdLEDColorBottomNightB);
      LEDColorWhite = NVSControlInteger(NVSDBName, NVSVarLEDColorWhiteNight, true, NVSStdLEDColorWhiteNight);
      MQTTSendSettings();
      Serial.println("-----");
      Serial.println("Configuration / nighttime LED settings loaded!");
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

// a function to create mesmerizing LED brilliance and vibrant color shifts,
// setting the perfect mood for contented shrimps to thrive
void LEDColorControl() {
  int LEDColorTopNewR;
  int LEDColorTopNewG;
  int LEDColorTopNewB;
  int LEDColorBottomNewR;
  int LEDColorBottomNewG;
  int LEDColorBottomNewB;
  int LEDColorWLimit;
  int LEDColorWMax = 0;
  double LEDColorWBalanceFactor;
  int LEDColorDeltaR;
  int LEDColorDeltaG;
  int LEDColorDeltaB;
  double LEDBrightnessReduceFactor;
  double LEDAmplifierYFast;
  double LEDAmplifierYSlow;
  double LEDAmplifierYLinear;
  double LEDAmplifierY;
  int LEDColorTempR;
  int LEDColorTempG;
  int LEDColorTempB;
  int LEDColorTempW;

  // internal lambda function for limiting led color values to 255
  auto LimitTo255 = [](double Value) -> int {
    if (Value < 0) {
      Value = 0;
    }
    else if (Value > 255) {
      Value = 255;
    }
    return static_cast<int>(Value);
  };

  // internal lambda function for limiting led color brightness
  auto CalculateBrigthnessReduceFactor = [&](int R, int G, int B) -> double {
    double reduceFactor;
    int LEDColorMaxFound = R;
    if (G > LEDColorMaxFound) {
      LEDColorMaxFound = G;
    }
    if (B > LEDColorMaxFound) {
      LEDColorMaxFound = B;
    }
    int LEDColorLimit = static_cast<int>(255 * LEDBrightness / 100);
    if (LEDColorMaxFound > LEDColorLimit) {
      reduceFactor = static_cast<double>(LEDColorLimit) / LEDColorMaxFound;
    }
    else {
      reduceFactor = 1.0;
    }
    return reduceFactor;
  };

  if (LEDStatus != 0) {
    LEDBrightnessReduceFactor = CalculateBrigthnessReduceFactor(LEDColorBottomR, LEDColorBottomG, LEDColorBottomB);
    LEDColorBottomNewR = static_cast<int>(LEDBrightnessReduceFactor * static_cast<double>(LEDColorBottomR));
    LEDColorBottomNewG = static_cast<int>(LEDBrightnessReduceFactor * static_cast<double>(LEDColorBottomG));
    LEDColorBottomNewB = static_cast<int>(LEDBrightnessReduceFactor * static_cast<double>(LEDColorBottomB));
    
    LEDBrightnessReduceFactor = CalculateBrigthnessReduceFactor(LEDColorTopR, LEDColorTopG, LEDColorTopB);
    LEDColorTopNewR = static_cast<int>(LEDBrightnessReduceFactor * static_cast<double>(LEDColorTopR));
    LEDColorTopNewG = static_cast<int>(LEDBrightnessReduceFactor * static_cast<double>(LEDColorTopG));
    LEDColorTopNewB = static_cast<int>(LEDBrightnessReduceFactor * static_cast<double>(LEDColorTopB));
    
    LEDColorDeltaR = LEDColorBottomNewR - LEDColorTopNewR;
    LEDColorDeltaG = LEDColorBottomNewG - LEDColorTopNewG;
    LEDColorDeltaB = LEDColorBottomNewB - LEDColorTopNewB;

    LEDColorWLimit = 255 * LEDColorWhite / 100.0;
    
    Serial.println("LED / starting the LED strip configuration...");
    Serial.println("-----");
    // the first 'for' loop only calculates the maximum average 'LEDColorWMax'
    for (int i = 1; i <= LEDPixelCount; ++i) {
      if (i == 1) {
        LEDColorTempR = LEDColorBottomNewR;
        LEDColorTempG = LEDColorBottomNewG;
        LEDColorTempB = LEDColorBottomNewB;
      }
      else if (i > 1 && i != LEDPixelCount) {
        LEDAmplifierYFast = 1 - exp((-i + 1) / LEDTau);
        LEDAmplifierYSlow = (1.0 / exp(1.0 / LEDTau * LEDPixelCount)) * exp(1.0 / LEDTau * i);
        LEDAmplifierYLinear = (1.0 / (LEDPixelCount - 1)) * i - (1.0 / (LEDPixelCount - 1));
        if (LEDAmplifier >= 0) {
          LEDAmplifierY = LEDAmplifierYFast * LEDAmplifier / 100 + LEDAmplifierYLinear * (100 - LEDAmplifier) / 100;
        } else {
          LEDAmplifierY = LEDAmplifierYSlow * abs(LEDAmplifier) / 100 + LEDAmplifierYLinear * (100 - abs(LEDAmplifier)) / 100;
        }
        LEDColorTempR = LimitTo255(LEDColorBottomNewR - LEDColorDeltaR * LEDAmplifierY);
        LEDColorTempG = LimitTo255(LEDColorBottomNewG - LEDColorDeltaG * LEDAmplifierY);
        LEDColorTempB = LimitTo255(LEDColorBottomNewB - LEDColorDeltaB * LEDAmplifierY);
      }
      else {
        LEDColorTempR = LEDColorTopNewR;
        LEDColorTempG = LEDColorTopNewG;
        LEDColorTempB = LEDColorTopNewB;
      }
      LEDColorTempW = (LEDColorTempR + LEDColorTempG + LEDColorTempB) / 3;
      if (LEDColorTempW > LEDColorWMax) {
        LEDColorWMax = LEDColorTempW;
      }
    }
    LEDColorWBalanceFactor = static_cast<double>(LEDColorWLimit) / static_cast<double>(LEDColorWMax);
    // the second 'for' loop calculates all colors as before, but also the balanced white
    for (int i = 1; i <= LEDPixelCount; ++i) {
      if (i == 1) {
        LEDColorTempR = LEDColorBottomNewR;
        LEDColorTempG = LEDColorBottomNewG;
        LEDColorTempB = LEDColorBottomNewB;
      }
      else if (i > 1 && i != LEDPixelCount) {
        LEDAmplifierYFast = 1 - exp((-i + 1) / LEDTau);
        LEDAmplifierYSlow = (1.0 / exp(1.0 / LEDTau * LEDPixelCount)) * exp(1.0 / LEDTau * i);
        LEDAmplifierYLinear = (1.0 / (LEDPixelCount - 1)) * i - (1.0 / (LEDPixelCount - 1));
        if (LEDAmplifier >= 0) {
          LEDAmplifierY = LEDAmplifierYFast * LEDAmplifier / 100 + LEDAmplifierYLinear * (100 - LEDAmplifier) / 100;
        } else {
          LEDAmplifierY = LEDAmplifierYSlow * abs(LEDAmplifier) / 100 + LEDAmplifierYLinear * (100 - abs(LEDAmplifier)) / 100;
        }
        LEDColorTempR = LimitTo255(LEDColorBottomNewR - LEDColorDeltaR * LEDAmplifierY);
        LEDColorTempG = LimitTo255(LEDColorBottomNewG - LEDColorDeltaG * LEDAmplifierY);
        LEDColorTempB = LimitTo255(LEDColorBottomNewB - LEDColorDeltaB * LEDAmplifierY);
      }
      else {
        LEDColorTempR = LEDColorTopNewR;
        LEDColorTempG = LEDColorTopNewG;
        LEDColorTempB = LEDColorTopNewB;
      }
      LEDColorTempW = LimitTo255(((LEDColorTempR + LEDColorTempG + LEDColorTempB) / 3) * LEDColorWBalanceFactor);
      // configure the 'LEDStrip'
      //LEDStrip.SetPixelColor(i, RgbwColor(LEDColorTempR, LEDColorTempG, LEDColorTempB, LEDColorTempW));
      Serial.printf("LED / %2d: [%3d,%3d,%3d,%3d]\n", i, LEDColorTempR, LEDColorTempG, LEDColorTempB, LEDColorTempW);
    }
  }
  else {
    // set the color of each led to '0'
    for (int i = 1; i <= LEDPixelCount; ++i) {
      // configure the 'LEDStrip'
      //LEDStrip.SetPixelColor(i, RgbwColor(0, 0, 0, 0));
      Serial.printf("LED / %2d: [  0,  0,  0,  0]\n", i);
    }
  }
  // activate the 'LEDStrip'
  //LEDStrip.Show();
  Serial.println("-----");
  Serial.println("LED / the LED strip configuration is activated!");
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

  // initialize 'LEDStrip'
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
      Serial.println("Timer / daytime is active!");
      Serial.println("-----");
      OneTimeCodeExecutedDay = true; // this code was executed, don't do it again for this phase
      OneTimeCodeExecutedNight = false; // initialization for the next nighttime phase
      NVSReadSettings(false, true); // phase changed, read new time phase settings
      LEDColorControl();
    }
  }
  else {
    // nighttime - infinite loop
    // ...

    if (!OneTimeCodeExecutedNight) {
      // nighttime - one time code
      Serial.println("Timer / nighttime is active!");
      Serial.println("-----");
      OneTimeCodeExecutedDay = false; // initialization for the next daytime phase
      OneTimeCodeExecutedNight = true; // this code was executed, don't do it again for this phase
      NVSReadSettings(false, true); // phase changed, read new time phase settings
      LEDColorControl();
    }
  }
}