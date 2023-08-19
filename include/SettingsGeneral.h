// -------------------------------------------------------------------
// General Settings
// -------------------------------------------------------------------

// WiFi - connecting status
bool isConnecting = false;

// MQTT - server settings
const char* MQTTServer = "192.168.178.25";
const int MQTTPort = 1883;

// MQTT - subscribed topics
const char* MQTTTopicLEDStatus          = "LEDStatus";      // 0: Night; 1: Day
const char* MQTTTopicLEDBrightness      = "LEDBrightness";  // 0..100 = Lights off .. full intensity
const char* MQTTTopicLEDQuickness       = "LEDQuickness";   // -100..0..100 = slow..linear..fast
const char* MQTTTopicLEDTau             = "LEDTau";         // 5125..8200 = (faster increase) 41/8*1000..41/5*1000 (slower increase)
const char* MQTTTopicLEDColorTop        = "LEDColorTop";    // [255,255,255]
const char* MQTTTopicLEDColorBottom     = "LEDColorBottom"; // [255,255,255]

// NTP - Server
const char* NTPServer = "pool.ntp.org";
const long  gmtOffset_sec = 3600;
const int   daylightOffset_sec = 3600;

// NVS - DB
const char* NVSDBName = "NVSDB";

// NVS - variable names
const char* NVSVarLEDStatus             = "Value01";
const char* NVSVarLEDBrightnessDay      = "Value02";
const char* NVSVarLEDBrightnessNight    = "Value03";
const char* NVSVarLEDQuicknessDay       = "Value04";
const char* NVSVarLEDQuicknessNight     = "Value05";
const char* NVSVarLEDTauDay             = "Value06";
const char* NVSVarLEDTauNight           = "Value07";
const char* NVSVarLEDColorTopDayR       = "Value08";
const char* NVSVarLEDColorTopDayG       = "Value09";
const char* NVSVarLEDColorTopDayB       = "Value10";
const char* NVSVarLEDColorTopNightR     = "Value11";
const char* NVSVarLEDColorTopNightG     = "Value12";
const char* NVSVarLEDColorTopNightB     = "Value13";
const char* NVSVarLEDColorBottomDayR    = "Value14";
const char* NVSVarLEDColorBottomDayG    = "Value15";
const char* NVSVarLEDColorBottomDayB    = "Value16";
const char* NVSVarLEDColorBottomNightR  = "Value17";
const char* NVSVarLEDColorBottomNightG  = "Value18";
const char* NVSVarLEDColorBottomNightB  = "Value19";

// NVS - standard values
int NVSStdLEDStatus = 0;
int NVSStdLEDBrightnessDay = 70;
int NVSStdLEDBrightnessNight = 30;
int NVSStdLEDQuicknessDay = 0;
int NVSStdLEDQuicknessNight = 0;
float NVSStdLEDTauDay = 5.125;
float NVSStdLEDTauNight = 5.125;
int NVSStdLEDColorTopDayR= 0;
int NVSStdLEDColorTopDayG = 193;
int NVSStdLEDColorTopDayB = 255;
int NVSStdLEDColorTopNightR= 255;
int NVSStdLEDColorTopNightG = 0;
int NVSStdLEDColorTopNightB = 119;
int NVSStdLEDColorBottomDayR = 194;
int NVSStdLEDColorBottomDayG = 255;
int NVSStdLEDColorBottomDayB = 0;
int NVSStdLEDColorBottomNightR = 118;
int NVSStdLEDColorBottomNightG = 0;
int NVSStdLEDColorBottomNightB = 255;

// Timer
const float StartTimeDay = 13.50;
const float StartTimeNight = 23.50;
bool OneTimeCodeExecutedDay = false;
bool OneTimeCodeExecutedNight = false;
bool isDay = false;

// LED strip calculation program
const int NumLED = 41;
int LEDStatus;
int LEDBrightness;
int LEDBrightnessCorrection;
int LEDQuickness;
float LEDTau;
int LEDColorTopR;
int LEDColorTopG;
int LEDColorTopB;
int LEDColorBottomR;
int LEDColorBottomG;
int LEDColorBottomB;
int LEDColorDeltaR;
int LEDColorDeltaG;
int LEDColorDeltaB;