// -------------------------------------------------------------------
// General Settings
// -------------------------------------------------------------------

// WiFi status
bool isConnecting = false;

// MQTT - server settings
const char* MQTTServer = "192.168.178.25";
const int MQTTPort = 1883;

// MQTT - subscribed topics
const char* MQTTTopicStartTimeDay       = "StartTimeDay";   // 1:5..13:9 = 01:05..13:09
const char* MQTTTopicStartTimeNight     = "StartTimeNight"; // 1:5..13:9 = 01:05..13:09
const char* MQTTTopicTimePhase          = "TimePhase";      // 0: nighttime; 1: daytime
const char* MQTTTopicLEDStatus          = "LEDStatus";      // 0: Lights ON; 1: Lights Off
const char* MQTTTopicLEDBrightness      = "LEDBrightness";  // 0..100 = Lights off..full intensity
const char* MQTTTopicLEDAmplifier       = "LEDAmplifier";   // -100..0..100 = slow..linear..fast
const char* MQTTTopicLEDTauThousand     = "LEDTauThousand"; // 5125..8200 = 41 pxl / 8 periods * 1000..
                                                            //            ..41 pxl / 5 periods * 1000 =
                                                            //              faster increase..slower increase
const char* MQTTTopicLEDColorTop        = "LEDColorTop";    // [  5, 55,255]
const char* MQTTTopicLEDColorBottom     = "LEDColorBottom"; // [  5, 55,255]
const char* MQTTTopicLEDColorWhite      = "LEDColorWhite";  // 0..100 = white off..white full intensity
const char* MQTTTopicUpdate             = "Update";         // 1 = update

// NTP - Server
const char* NTPServer = "pool.ntp.org";
const long  gmtOffset_sec = 3600;
const int   daylightOffset_sec = 3600;

// NVS - DB
const char* NVSDBName = "NVSDB";

// NVS - variable names
const char* NVSVarStartTimeDayHours     = "Value01";
const char* NVSVarStartTimeDayMinutes   = "Value02";
const char* NVSVarStartTimeNightHours   = "Value03";
const char* NVSVarStartTimeNightMinutes = "Value04";
const char* NVSVarLEDStatusDay          = "Value05";
const char* NVSVarLEDStatusNight        = "Value06";
const char* NVSVarLEDBrightnessDay      = "Value07";
const char* NVSVarLEDBrightnessNight    = "Value08";
const char* NVSVarLEDAmplifierDay       = "Value09";
const char* NVSVarLEDAmplifierNight     = "Value10";
const char* NVSVarLEDTauDay             = "Value11";
const char* NVSVarLEDTauNight           = "Value12";
const char* NVSVarLEDColorTopDayR       = "Value13";
const char* NVSVarLEDColorTopDayG       = "Value14";
const char* NVSVarLEDColorTopDayB       = "Value15";
const char* NVSVarLEDColorTopNightR     = "Value16";
const char* NVSVarLEDColorTopNightG     = "Value17";
const char* NVSVarLEDColorTopNightB     = "Value18";
const char* NVSVarLEDColorBottomDayR    = "Value19";
const char* NVSVarLEDColorBottomDayG    = "Value20";
const char* NVSVarLEDColorBottomDayB    = "Value21";
const char* NVSVarLEDColorBottomNightR  = "Value22";
const char* NVSVarLEDColorBottomNightG  = "Value23";
const char* NVSVarLEDColorBottomNightB  = "Value24";
const char* NVSVarLEDColorWhiteDay      = "Value25";
const char* NVSVarLEDColorWhiteNight    = "Value26";

// NVS - standard values
int NVSStdStartTimeDayHours = 9;
int NVSStdStartTimeDayMinutes = 0;
int NVSStdStartTimeNightHours = 22;
int NVSStdStartTimeNightMinutes = 30;
int NVSStdLEDStatusDay = 1;
int NVSStdLEDStatusNight = 1;
int NVSStdLEDBrightnessDay = 70;
int NVSStdLEDBrightnessNight = 30;
int NVSStdLEDAmplifierDay = 0;
int NVSStdLEDAmplifierNight = 0;
int NVSStdLEDTauDay = 5125;
int NVSStdLEDTauNight = 5125;
int NVSStdLEDColorTopDayR= 0;
int NVSStdLEDColorTopDayG = 193;
int NVSStdLEDColorTopDayB = 255;
int NVSStdLEDColorTopNightR= 77;
int NVSStdLEDColorTopNightG = 0;
int NVSStdLEDColorTopNightB = 26;
int NVSStdLEDColorBottomDayR = 194;
int NVSStdLEDColorBottomDayG = 255;
int NVSStdLEDColorBottomDayB = 0;
int NVSStdLEDColorBottomNightR = 0;
int NVSStdLEDColorBottomNightG = 60;
int NVSStdLEDColorBottomNightB = 82;
int NVSStdLEDColorWhiteDay = 70;
int NVSStdLEDColorWhiteNight = 30;

// Timer
float StartTimeDay;
float StartTimeNight;
int StartTimeDayHours;
int StartTimeDayMinutes;
int StartTimeNightHours;
int StartTimeNightMinutes;
int TimePhase;
bool OneTimeCodeExecutedDay = false;
bool OneTimeCodeExecutedNight = false;

// LED strip calculation program
const int LEDPixelCount = 41;
const int LEDPin = 27;
int LEDStatus;
int LEDBrightness;
int LEDAmplifier;
int LEDTauThousand;
float LEDTau;
int LEDColorTopR;
int LEDColorTopG;
int LEDColorTopB;
int LEDColorBottomR;
int LEDColorBottomG;
int LEDColorBottomB;
int LEDColorWhite;