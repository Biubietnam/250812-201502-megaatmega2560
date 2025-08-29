#include <SPI.h>
#include <Arduino.h>
#include <SdFat.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <Servo.h>
#include <RTClib.h>
#include <ArduinoJson.h>
#include <StreamUtils.h>

#define SD_CS 11
#define TFT_CS 10
#define TFT_RST 8
#define TFT_DC 9
#define MOTOR_1 22
#define MOTOR_2 24
#define MOTOR_3 26
#define MOTOR_4 28
#define DROP_BTN 30
#define FSR_PIN A4

#define MAX_SCHEDULES 12  // Increased from 6 to 12 to accommodate more tubes and schedules
#define MAX_GROUPED 12    // Increased from 6 to 12 to match MAX_SCHEDULES
#define MAX_MEDS_PER_TIME 3  // Reduced from 5
#define TEMP_BUFFER_SIZE 64  // Fixed buffer size

Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);
RTC_DS3231 rtc;
SdFat SD;
File file;
Servo servo1, servo2, servo3, servo4;

bool filestat = false;
bool receiving = false;
unsigned long receiveStartTime = 0;
unsigned long lastByteTime = 0;
File streamingFile;
bool streamingActive = false;

char notificationMessage[200] = "";
unsigned long notificationStartTime = 0;
volatile bool sdBusy = false;
DateTime rtctime;

int currentMenuPage = 0;
unsigned long lastMenuUpdate = 0;
bool showNotification = false;
bool motorStates[4] = {false, false, false, false};

struct TubeMapping {
  char tubeName[8];  // Fixed size instead of String
  int servoIndex;
  int motorPin;
  Servo *servo;
};

TubeMapping tubeMappings[4] = {
    {"tube1", 0, MOTOR_1, &servo1},
    {"tube2", 1, MOTOR_2, &servo2},
    {"tube3", 2, MOTOR_3, &servo3},
    {"tube4", 3, MOTOR_4, &servo4}
};

struct MedicationTime {
  char time[6];        // "HH:MM" format
  char dosage[16];     // Fixed size for dosage
  char medication[24]; // Fixed size for medication name
  char tube[8];        // Fixed size for tube name
  int amount;
};

MedicationTime schedules[MAX_SCHEDULES]; // Increased array size
int scheduleCount = 0;

struct GroupedMedication {
  char time[6];                           // "HH:MM" format
  char medications[MAX_MEDS_PER_TIME][24]; // Fixed size arrays
  char dosages[MAX_MEDS_PER_TIME][16];
  char tubes[MAX_MEDS_PER_TIME][8];
  int amounts[MAX_MEDS_PER_TIME];
  int count;
};

GroupedMedication groupedSchedules[MAX_GROUPED]; // Increased array size
int groupedCount = 0;

bool setupMode = false;
int currentTubeSetup = 0;
int totalTubesNeeded = 0;
char setupInstructions[200];
bool waitingForDropButton = false;

static bool triggerSetupAfterBT = false;

void openServo(Servo &servo, int standbyPos = 91, int openPos = 45) {
  Serial.println(F("Opening servo")); // Using F() macro for flash storage
  servo.write(openPos);
  delay(600);
  servo.write(standbyPos);
}

void closeServo(Servo &servo, int standbyPos = 91, int closePos = 135) {
  Serial.println(F("Closing servo")); // Using F() macro
  servo.write(closePos);
  delay(600);
  servo.write(standbyPos);
}

void triggerMotor(int motorPin, bool turnOn) {
  if (turnOn) {
    Serial.print(F("Starting motor on pin ")); // Using F() macro
    Serial.println(motorPin);
    digitalWrite(motorPin, HIGH);
  } else {
    Serial.print(F("Stopping motor on pin ")); // Using F() macro
    Serial.println(motorPin);
    digitalWrite(motorPin, LOW);
  }
}

TubeMapping *getTubeMapping(const char* tubeName) {
  for (int i = 0; i < 4; i++) {
    if (strcmp(tubeMappings[i].tubeName, tubeName) == 0) {
      return &tubeMappings[i];
    }
  }
  return nullptr;
}

inline bool acquireSD() {
  digitalWrite(TFT_CS, HIGH);
  delayMicroseconds(5);
  return SD.begin(SD_CS);
}

void dispenseFromTube(const char* tubeName) {
  TubeMapping *mapping = getTubeMapping(tubeName);
  if (mapping == nullptr) {
    Serial.print(F("Unknown tube: "));
    Serial.println(tubeName);
    return;
  }

  Serial.print(F("Dispensing from "));
  Serial.println(tubeName);

  float initialWeight = (analogRead(FSR_PIN) /1.504761904761905);
  Serial.print(F("Initial weight: "));
  Serial.print(initialWeight, 1);
  Serial.println(F(" g"));

  openServo(*mapping->servo);
  delay(500);

  triggerMotor(mapping->motorPin, true);
  motorStates[mapping->servoIndex] = true;

  unsigned long startTime = millis();
  bool dispensingComplete = false;

  while (!dispensingComplete && (millis() - startTime < 10000)) {
    float currentWeight = (analogRead(FSR_PIN) / 1.504761904761905);
    float weightIncrease = currentWeight - initialWeight;

    Serial.print(F("Current weight: "));
    Serial.print(currentWeight, 1);
    Serial.print(F(" g, Increase: "));
    Serial.print(weightIncrease, 1);
    Serial.println(F(" g"));

    if (weightIncrease >= 5.0) {
      dispensingComplete = true;
      Serial.println(F("Target weight reached!"));
    }
    delay(100);
  }

  triggerMotor(mapping->motorPin, false);
  motorStates[mapping->servoIndex] = false;

  delay(500);
  closeServo(*mapping->servo);

  Serial.print(F("Dispensing complete for "));
  Serial.println(tubeName);
}

void handleDispensing() {
  Serial.println(F("DROP button pressed - starting dispensing sequence"));

  char currentTime[6];
  sprintf(currentTime, "%02d:%02d", rtctime.hour(), rtctime.minute());

  GroupedMedication *currentGroup = nullptr;
  for (int i = 0; i < groupedCount; i++) {
    if (strcmp(groupedSchedules[i].time, currentTime) == 0) { // Using strcmp
      currentGroup = &groupedSchedules[i];
      break;
    }
  }

  if (currentGroup == nullptr) {
    Serial.println(F("No medications scheduled for current time"));
    return;
  }

  for (int i = 0; i < currentGroup->count; i++) {
    Serial.print(F("Dispensing medication "));
    Serial.print(i + 1);
    Serial.print(F(" of "));
    Serial.print(currentGroup->count);
    Serial.print(F(": "));
    Serial.println(currentGroup->medications[i]);

    dispenseFromTube(currentGroup->tubes[i]);

    if (i < currentGroup->count - 1) {
      Serial.println(F("Waiting before next tube..."));
      delay(2000);
    }
  }

  Serial.println(F("Dispensing sequence complete"));
  showNotification = false;
}

void drawLoadingBar(int progress, int x, int y, int width, int height) {
  tft.drawRect(x, y, width, height, ST77XX_WHITE);
  int fillWidth = (progress * (width - 2)) / 100;
  if (fillWidth > 0) {
    tft.fillRect(x + 1, y + 1, fillWidth, height - 2, ST77XX_GREEN);
  }
}

void drawSpinner(int x, int y, int radius, int angle) {
  tft.fillCircle(x, y, radius + 2, ST77XX_BLACK);
  
  for (int i = 0; i < 8; i++) {
    int segmentAngle = (angle + i * 45) % 360;
    int brightness = 255 - (i * 30);
    if (brightness < 50) brightness = 50;

    int x1 = x + (radius - 3) * cos(segmentAngle * PI / 180);
    int y1 = y + (radius - 3) * sin(segmentAngle * PI / 180);
    int x2 = x + radius * cos(segmentAngle * PI / 180);
    int y2 = y + radius * sin(segmentAngle * PI / 180);

    uint16_t color = tft.color565(brightness, brightness, brightness);
    tft.drawLine(x1, y1, x2, y2, color);
    tft.drawLine(x1 + 1, y1, x2 + 1, y2, color);
  }
}

int findMatchingBrace(const String &str, int start) {
  int braceCount = 0;
  for (int i = start; i < str.length(); i++) {
    if (str[i] == '{') braceCount++;
    else if (str[i] == '}') {
      braceCount--;
      if (braceCount == 0) return i;
    }
  }
  return -1;
}

void animatedIntro() {
  tft.fillScreen(ST77XX_BLACK);
  tft.setRotation(3);
  
  tft.setTextSize(3);
  tft.setTextColor(ST77XX_WHITE);
  tft.setCursor(50, 100);
  tft.println(F("MedDispenser"));
  
  tft.setTextSize(1);
  tft.setCursor(80, 140);
  tft.println(F("Initializing..."));
  
  delay(2000);
}

void groupMedicationsByTime() {
  groupedCount = 0;

  for (int i = 0; i < scheduleCount; i++) {
    int groupIndex = -1;

    for (int j = 0; j < groupedCount; j++) {
      if (strcmp(groupedSchedules[j].time, schedules[i].time) == 0) {
        groupIndex = j;
        break;
      }
    }

    if (groupIndex == -1) {
      groupIndex = groupedCount;
      strcpy(groupedSchedules[groupIndex].time, schedules[i].time); // Using strcpy
      groupedSchedules[groupIndex].count = 0;
      groupedCount++;
    }

    int medIndex = groupedSchedules[groupIndex].count;
    if (medIndex < MAX_MEDS_PER_TIME) { // Using new constant
      strcpy(groupedSchedules[groupIndex].medications[medIndex], schedules[i].medication);
      strcpy(groupedSchedules[groupIndex].dosages[medIndex], schedules[i].dosage);
      strcpy(groupedSchedules[groupIndex].tubes[medIndex], schedules[i].tube);
      groupedSchedules[groupIndex].amounts[medIndex] = schedules[i].amount;
      groupedSchedules[groupIndex].count++;
    }
  }
}

bool startStreamingSave() {
  const char *tmpName = "data.tmp";

  if (sdBusy) {
    Serial.println(F("startStreamingSave: SD busy, abort.")); // Using F() macro
    return false;
  }
  sdBusy = true;

  digitalWrite(TFT_CS, HIGH);
  delay(5);
  if (!acquireSD()) {
    Serial.println(F("SD.begin failed")); // Using F() macro
    sdBusy = false;
    return false;
  }

  if (!SD.begin(SD_CS)) {
    Serial.println(F("startStreamingSave: SD.begin() failed.")); // Using F() macro
    sdBusy = false;
    return false;
  }

  if (SD.exists(tmpName)) {
    SD.remove(tmpName);
    delay(100);
  }

  streamingFile = SD.open(tmpName, O_WRITE | O_CREAT | O_TRUNC);
  if (!streamingFile) {
    Serial.println(F("startStreamingSave: ERROR opening temp for write!")); // Using F() macro
    sdBusy = false;
    return false;
  }

  streamingActive = true;
  Serial.println(F("Started streaming save to SD")); // Using F() macro
  return true;
}

bool writeStreamingChunk(const String &chunk) {
  if (!streamingActive || !streamingFile) {
    return false;
  }

  size_t written = streamingFile.print(chunk);
  streamingFile.flush();

  if (written != chunk.length()) {
    Serial.println(F("writeStreamingChunk: ERROR incomplete write!")); // Using F() macro
    return false;
  }
  return true;
}

bool finishStreamingSave() {
  if (!streamingActive) return false;

  const char *tmpName = "data.tmp";
  const char *finalName = "data.json";

  streamingFile.sync();
  streamingFile.close();
  streamingActive = false;
  delay(50);

  if (SD.exists(finalName)) {
    bool removed = false;
    for (int attempt = 0; attempt < 6; ++attempt) {
      delay(40);
      removed = SD.remove(finalName);
      Serial.print(F("finishStreamingSave: remove final attempt ")); // Using F() macro
      Serial.print(attempt);
      Serial.print(F(" -> "));
      Serial.println(removed ? F("ok") : F("fail"));
      if (removed) break;
    }
  }

  bool renamed = SD.rename(tmpName, finalName);
  Serial.print(F("finishStreamingSave: rename -> ")); // Using F() macro
  Serial.println(renamed ? F("ok") : F("fail"));

  if (!renamed) {
    Serial.println(F("finishStreamingSave: fallback copy starting...")); // Using F() macro
    File r = SD.open(tmpName, FILE_READ);
    if (!r) {
      Serial.println(F("finishStreamingSave: fallback: cannot open temp for read.")); // Using F() macro
      sdBusy = false;
      return false;
    }

    if (SD.exists(finalName)) {
      SD.remove(finalName);
      delay(10);
    }

    File f2 = SD.open(finalName, O_WRITE | O_CREAT | O_TRUNC);
    if (!f2) {
      Serial.println(F("finishStreamingSave: fallback: cannot open final for write.")); // Using F() macro
      r.close();
      sdBusy = false;
      return false;
    }

    char buffer[32];
    while (r.available()) {
      int bytesRead = r.readBytes(buffer, sizeof(buffer));
      f2.write((const uint8_t *)buffer, bytesRead);
    }

    f2.sync();
    f2.close();
    r.close();
    Serial.println(F("finishStreamingSave: fallback copy complete")); // Using F() macro

    if (SD.exists(tmpName)) {
      SD.remove(tmpName);
    }
  }

  sdBusy = false;
  Serial.println(F("Streaming save completed successfully")); // Using F() macro
  delay(500);
  return true;
}

int timeToMinutes(const char* timeStr) {
  int hours, minutes;
  if (sscanf(timeStr, "%d:%d", &hours, &minutes) != 2) {
    return -1;
  }
  return hours * 60 + minutes;
}

int findNextMedication() {
  int currentMinutes = rtctime.hour() * 60 + rtctime.minute();
  int closestIndex = -1;
  int minDifference = 24 * 60;

  for (int i = 0; i < groupedCount; i++) {
    int scheduleMinutes = timeToMinutes(groupedSchedules[i].time);
    if (scheduleMinutes == -1) continue;

    int difference = scheduleMinutes - currentMinutes;
    if (difference < 0) difference += 24 * 60;

    if (difference < minDifference) {
      minDifference = difference;
      closestIndex = i;
    }
  }
  return closestIndex;
}

bool checkMedicationTime() {
  char currentTime[6];
  sprintf(currentTime, "%02d:%02d", rtctime.hour(), rtctime.minute());

  for (int i = 0; i < groupedCount; i++) {
    if (strcmp(groupedSchedules[i].time, currentTime) == 0) { // Using strcmp
      if (groupedSchedules[i].count == 1) {
        snprintf(notificationMessage, sizeof(notificationMessage), 
                "TIME TO TAKE: %s - %s", 
                groupedSchedules[i].medications[0], 
                groupedSchedules[i].dosages[0]);
      } else {
        snprintf(notificationMessage, sizeof(notificationMessage), 
                "TIME TO TAKE %d MEDS: %s (%s)", 
                groupedSchedules[i].count,
                groupedSchedules[i].medications[0], 
                groupedSchedules[i].dosages[0]);
        
        if (groupedSchedules[i].count > 1 && strlen(notificationMessage) < 150) {
          char temp[50];
          snprintf(temp, sizeof(temp), " + %s (%s)", 
                  groupedSchedules[i].medications[1], 
                  groupedSchedules[i].dosages[1]);
          strncat(notificationMessage, temp, sizeof(notificationMessage) - strlen(notificationMessage) - 1);
        }
      }
      return true;
    }
  }
  return false;
}

bool loadScheduleData() {
  if (sdBusy) {
    Serial.println(F("loadScheduleData: SD busy, abort")); // Using F() macro
    return false;
  }
  sdBusy = true;

  digitalWrite(TFT_CS, HIGH);
  delayMicroseconds(5);
  if (!acquireSD()) {
    Serial.println(F("loadScheduleData: SD.begin failed")); // Using F() macro
    sdBusy = false;
    return false;
  }

  File f = SD.open("data.json", FILE_READ);
  if (!f) {
    Serial.println(F("Cannot find data.json")); // Using F() macro
    sdBusy = false;
    return false;
  }

  size_t fileSize = f.size();
  Serial.print(F("loadScheduleData: fileSize = ")); // Using F() macro
  Serial.println(fileSize);
  if (fileSize == 0) {
    Serial.println(F("loadScheduleData: file empty")); // Using F() macro
    f.close();
    sdBusy = false;
    return false;
  }

  StaticJsonDocument<128> filter;
  filter[0]["tube"] = true;
  filter[0]["type"] = true;
  filter[0]["amount"] = true;
  filter[0]["time_to_take"][0]["time"] = true;
  filter[0]["time_to_take"][0]["dosage"] = true;

  scheduleCount = 0;

  StaticJsonDocument<1024> doc;
  ReadBufferingStream in(f, 32); // Reduced buffer size from 64 to 32
  DeserializationError err = deserializeJson(doc, in, DeserializationOption::Filter(filter));

  f.close();

  if (err) {
    Serial.print(F("JSON parse error: ")); // Using F() macro
    Serial.println(err.c_str());
    sdBusy = false;
    return false;
  }

  if (!doc.is<JsonArray>()) {
    Serial.println(F("JSON root is not an array")); // Using F() macro
    sdBusy = false;
    return false;
  }

  JsonArray arr = doc.as<JsonArray>();
  for (JsonObject med : arr) {
    const char* tubeC = med["tube"] | "";
    const char* typeC = med["type"] | "";
    int amount = med["amount"] | 0;

    JsonArray times = med["time_to_take"].as<JsonArray>();
    if (times.isNull()) continue;

    for (JsonObject t : times) {
      if (scheduleCount >= MAX_SCHEDULES) break; // Using new constant
      const char* timeC = t["time"] | "";
      const char* dosageC = t["dosage"] | "";

      strncpy(schedules[scheduleCount].tube, tubeC, sizeof(schedules[scheduleCount].tube) - 1);
      schedules[scheduleCount].tube[sizeof(schedules[scheduleCount].tube) - 1] = '\0';
      
      strncpy(schedules[scheduleCount].medication, typeC, sizeof(schedules[scheduleCount].medication) - 1);
      schedules[scheduleCount].medication[sizeof(schedules[scheduleCount].medication) - 1] = '\0';
      
      schedules[scheduleCount].amount = amount;
      
      strncpy(schedules[scheduleCount].time, timeC, sizeof(schedules[scheduleCount].time) - 1);
      schedules[scheduleCount].time[sizeof(schedules[scheduleCount].time) - 1] = '\0';
      
      strncpy(schedules[scheduleCount].dosage, dosageC, sizeof(schedules[scheduleCount].dosage) - 1);
      schedules[scheduleCount].dosage[sizeof(schedules[scheduleCount].dosage) - 1] = '\0';
      
      scheduleCount++;
    }
  }

  sdBusy = false;
  groupMedicationsByTime();
  Serial.print(F("Loaded ")); // Using F() macro
  Serial.print(scheduleCount);
  Serial.println(F(" medication schedules"));

  return scheduleCount > 0;
}

void drawHeader() {
  tft.fillRect(0, 0, 320, 35, ST77XX_BLUE);

  tft.setTextSize(2);
  tft.setTextColor(ST77XX_WHITE);
  tft.setCursor(10, 8);
  if (rtctime.hour() < 10) tft.print("0");
  tft.print(rtctime.hour());
  tft.print(":");
  if (rtctime.minute() < 10) tft.print("0");
  tft.print(rtctime.minute());

  tft.setTextSize(1);
  tft.setCursor(10, 22);
  tft.print(rtctime.day());
  tft.print("/");
  tft.print(rtctime.month());
  tft.print("/");
  tft.print(rtctime.year());

  tft.setTextSize(1);
  tft.setCursor(200, 8);
  tft.print(F("STATUS: ")); // Using F() macro
  tft.setTextColor(filestat ? ST77XX_GREEN : ST77XX_RED);
  tft.print(filestat ? F("READY") : F("ERROR")); // Using F() macro

  tft.fillRect(290, 8, 20, 12, ST77XX_GREEN);
  tft.drawRect(289, 7, 22, 14, ST77XX_WHITE);
  tft.fillRect(311, 10, 3, 8, ST77XX_WHITE);
}

void drawGroupedMedicationCard(int x, int y, int width, int height, GroupedMedication group, bool isNext = false) {
  uint16_t cardColor = isNext ? ST77XX_YELLOW : ST77XX_WHITE;
  uint16_t textColor = isNext ? ST77XX_BLACK : ST77XX_BLACK;

  tft.fillRoundRect(x, y, width, height, 8, cardColor);
  tft.drawRoundRect(x, y, width, height, 8, isNext ? ST77XX_RED : ST77XX_BLUE);

  tft.setTextSize(2);
  tft.setTextColor(textColor);
  tft.setCursor(x + 8, y + 8);
  tft.print(group.time);

  if (group.count > 1) {
    tft.setTextSize(1);
    tft.setTextColor(ST77XX_RED);
    tft.setCursor(x + width - 50, y + 8);
    tft.print(group.count);
    tft.print(F(" MEDS")); // Using F() macro
  }

  tft.setTextSize(1);
  tft.setTextColor(textColor);
  tft.setCursor(x + 8, y + 32);
  tft.print(group.medications[0]);
  tft.print(F(" - ")); // Using F() macro
  tft.print(group.dosages[0]);

  if (group.count > 1) {
    tft.setCursor(x + 8, y + 45);
    tft.print(group.medications[1]);
    tft.print(F(" - ")); // Using F() macro
    tft.print(group.dosages[1]);
  }

  if (group.count > 2) {
    tft.setCursor(x + 8, y + 58);
    tft.print(F("+ ")); // Using F() macro
    tft.print(group.count - 2);
    tft.print(F(" more medications")); // Using F() macro
  } else if (group.count <= 2) {
    tft.setCursor(x + 8, y + 58);
    tft.print(group.tubes[0]);
    if (group.count == 2) {
      tft.print(F(", ")); // Using F() macro
      tft.print(group.tubes[1]);
    }
  }

  if (isNext) {
    tft.setTextSize(1);
    tft.setTextColor(ST77XX_RED);
    tft.setCursor(x + width - 35, y + height - 15);
    tft.print(F("NEXT")); // Using F() macro
  }
}

void drawNotification() {
  if (!showNotification) return;

  int notifHeight = 80;
  if (strlen(notificationMessage) > 50) {
    notifHeight = 100;
  }

  tft.fillRect(10, 80, 300, notifHeight, ST77XX_RED);
  tft.drawRect(9, 79, 302, notifHeight + 2, ST77XX_WHITE);

  bool blink = (millis() / 500) % 2;
  uint16_t textColor = blink ? ST77XX_WHITE : ST77XX_YELLOW;

  tft.setTextSize(1);
  tft.setTextColor(textColor);
  tft.setCursor(15, 90);
  tft.print(F("MEDICATION ALERT!")); // Using F() macro

  tft.setTextSize(1);
  int lineY = 105;
  int charsPerLine = 35;
  
  int msgLen = strlen(notificationMessage);
  int pos = 0;
  
  while (pos < msgLen && lineY < 80 + notifHeight - 20) {
    int lineEnd = pos + charsPerLine;
    if (lineEnd > msgLen) lineEnd = msgLen;
    
    if (lineEnd < msgLen) {
      while (lineEnd > pos && notificationMessage[lineEnd] != ' ') {
        lineEnd--;
      }
      if (lineEnd == pos) lineEnd = pos + charsPerLine;
    }
    
    tft.setCursor(15, lineY);
    for (int i = pos; i < lineEnd; i++) {
      tft.print(notificationMessage[i]);
    }
    
    pos = lineEnd;
    if (pos < msgLen && notificationMessage[pos] == ' ') pos++;
    lineY += 12;
  }

  tft.setTextSize(1);
  tft.setCursor(15, 80 + notifHeight - 25);
  tft.print(F("Press DROP button to dispense")); // Using F() macro

  tft.setCursor(15, 80 + notifHeight - 15);
  tft.print(F("Auto-dismiss in ")); // Using F() macro
  tft.print(300 - (millis() - notificationStartTime) / 1000);
  tft.print(F("s")); // Using F() macro

  if (millis() - notificationStartTime > 300000) {
    showNotification = false;
  }
}

void startTubeSetupMode() {
  setupMode = true;
  currentTubeSetup = 0;
  waitingForDropButton = false;
  
  totalTubesNeeded = 0;
  char uniqueTubes[10][8]; // Array to store unique tube names (max 10 tubes)
  
  // Go through all schedules and collect unique tube names
  for (int i = 0; i < scheduleCount; i++) {
    bool tubeExists = false;
    
    // Check if this tube name already exists in our unique list
    for (int j = 0; j < totalTubesNeeded; j++) {
      if (strcmp(uniqueTubes[j], schedules[i].tube) == 0) {
        tubeExists = true;
        break;
      }
    }
    
    // If tube doesn't exist in our list, add it
    if (!tubeExists && totalTubesNeeded < 10) {
      strcpy(uniqueTubes[totalTubesNeeded], schedules[i].tube);
      totalTubesNeeded++;
    }
  }
  
  Serial.println(F("Starting tube setup mode"));
  Serial.print(F("Total unique tubes to configure: "));
  Serial.println(totalTubesNeeded);
  
  // Debug: Print all unique tubes found
  Serial.println(F("Unique tubes found:"));
  for (int i = 0; i < totalTubesNeeded; i++) {
    Serial.print(F("- "));
    Serial.println(uniqueTubes[i]);
  }
}

void showTubeSetupScreen() {
  tft.fillScreen(ST77XX_BLACK);
  drawHeader();
  
  // Title
  tft.setTextSize(2);
  tft.setTextColor(ST77XX_YELLOW);
  tft.setCursor(50, 50);
  tft.print(F("TUBE SETUP"));
  
  // Progress indicator
  tft.setTextSize(1);
  tft.setTextColor(ST77XX_WHITE);
  tft.setCursor(20, 80);
  tft.print(F("Tube "));
  tft.print(currentTubeSetup + 1);
  tft.print(F(" of "));
  tft.print(totalTubesNeeded);
  
  // Current medication info
  if (currentTubeSetup < groupedCount) {
    tft.setTextSize(1);
    tft.setTextColor(ST77XX_CYAN);
    tft.setCursor(20, 100);
    tft.print(F("Put this medication:"));
    
    tft.setTextSize(1);
    tft.setTextColor(ST77XX_WHITE);
    tft.setCursor(20, 120);
    
    // Display medication name and dosage
    if (currentTubeSetup < groupedCount && groupedSchedules[currentTubeSetup].count > 0) {
      tft.print(groupedSchedules[currentTubeSetup].medications[0]);
      tft.setCursor(20, 135);
      tft.print(groupedSchedules[currentTubeSetup].dosages[0]);
    }
    
    tft.setTextSize(1);
    tft.setTextColor(ST77XX_GREEN);
    tft.setCursor(20, 160);
    tft.print(F("Into TUBE "));
    tft.print(currentTubeSetup + 1);
  }
  
  // Instructions
  tft.setTextSize(1);
  tft.setTextColor(ST77XX_YELLOW);
  tft.setCursor(20, 190);
  if (waitingForDropButton) {
    bool blink = (millis() / 500) % 2;
    if (blink) {
      tft.print(F("Press DROP button when done"));
    }
  } else {
    tft.print(F("Place medication in tube"));
    tft.setCursor(20, 205);
    tft.print(F("then press DROP button"));
  }
  
  // Progress bar
  int barWidth = 280;
  int barHeight = 10;
  int barX = 20;
  int barY = 230;
  
  tft.drawRect(barX, barY, barWidth, barHeight, ST77XX_WHITE);
  int progress = (currentTubeSetup * barWidth) / totalTubesNeeded;
  tft.fillRect(barX + 1, barY + 1, progress, barHeight - 2, ST77XX_GREEN);
}

void handleTubeSetupButton() {
  Serial.print(F("Tube "));
  Serial.print(currentTubeSetup + 1);
  Serial.println(F(" setup completed"));
  
  currentTubeSetup++;
  waitingForDropButton = false;
  
  if (currentTubeSetup >= totalTubesNeeded) {
    // Setup complete
    setupMode = false;
    Serial.println(F("Tube setup completed! System ready for automatic dispensing."));
    
    // Show completion message
    tft.fillScreen(ST77XX_BLACK);
    drawHeader();
    tft.setTextSize(2);
    tft.setTextColor(ST77XX_GREEN);
    tft.setCursor(50, 100);
    tft.print(F("SETUP"));
    tft.setCursor(50, 130);
    tft.print(F("COMPLETE!"));
    
    tft.setTextSize(1);
    tft.setTextColor(ST77XX_WHITE);
    tft.setCursor(20, 170);
    tft.print(F("System ready for"));
    tft.setCursor(20, 185);
    tft.print(F("automatic dispensing"));
    
    delay(3000);
  } else {
    waitingForDropButton = false;
  }
}

void showMainMenu() {
  tft.fillScreen(ST77XX_BLACK);
  drawHeader();

  if (!setupMode && triggerSetupAfterBT && filestat && groupedCount > 0) {
    startTubeSetupMode();
    triggerSetupAfterBT = false; // Reset the flag
  }

  if (setupMode) {
    showTubeSetupScreen();
    return; // Pause other tasks when in setup mode
  }

  if (checkMedicationTime() && !showNotification) {
    showNotification = true;
    notificationStartTime = millis();
  }

  if (showNotification) {
    drawNotification();
    return;
  }

  int contentY = 40;

  if (!filestat || groupedCount == 0) {
    tft.setTextSize(2);
    tft.setTextColor(ST77XX_RED);
    tft.setCursor(50, contentY + 50);
    tft.print(F("NO SCHEDULE DATA")); // Using F() macro

    tft.setTextSize(1);
    tft.setTextColor(ST77XX_WHITE);
    tft.setCursor(50, contentY + 80);
    tft.print(F("Please load medication")); // Using F() macro
    tft.setCursor(50, contentY + 95);
    tft.print(F("schedule via app")); // Using F() macro
    return;
  }

  int nextMedIndex = findNextMedication();

  tft.setTextSize(1);
  tft.setTextColor(ST77XX_CYAN);
  tft.setCursor(10, contentY + 5);
  tft.print(F("MEDICATION SCHEDULE")); // Using F() macro

  int cardY = contentY + 25;
  int cardsShown = 0;

  if (nextMedIndex != -1) {
    drawGroupedMedicationCard(10, cardY, 300, 75, groupedSchedules[nextMedIndex], true);
    cardY += 85;
    cardsShown++;
  }

  for (int i = 0; i < groupedCount && cardsShown < 3; i++) {
    if (i != nextMedIndex) {
      drawGroupedMedicationCard(10, cardY, 300, 75, groupedSchedules[i], false);
      cardY += 85;
      cardsShown++;
    }
  }

  tft.setTextSize(1);
  tft.setTextColor(ST77XX_CYAN);
  tft.setCursor(10, 260);
  tft.print(F("Total schedules: ")); // Using F() macro
  tft.print(groupedCount);
  tft.print(F(" (")); // Using F() macro
  tft.print(scheduleCount);
  tft.print(F(" doses)")); // Using F() macro

  tft.setCursor(200, 260);
  tft.print(F("Auto-refresh: 5s")); // Using F() macro
}

bool checkJsonFile() {
  File f = SD.open("data.json", FILE_READ);
  if (!f) {
    Serial.println(F("Cannot find data.json")); // Using F() macro
    return false;
  }

  String jsonStr;
  while (f.available()) jsonStr += (char)f.read();
  f.close();

  StaticJsonDocument<512> doc;
  DeserializationError err = deserializeJson(doc, jsonStr);

  if (err) {
    Serial.print(F("JSON syntax error: ")); // Using F() macro
    Serial.println(err.c_str());
    return false;
  }

  Serial.println(F("JSON is valid!")); // Using F() macro
  return true;
}

bool initSD() {
  pinMode(SD_CS, OUTPUT);
  pinMode(TFT_CS, OUTPUT);
  digitalWrite(SD_CS, LOW);
  digitalWrite(TFT_CS, HIGH);
  delay(50);

  for (int i = 0; i < 5; i++) {
    if (SD.begin(SD_CS)) {
      Serial.println(F("SD initialized.")); // Using F() macro
      delay(10);
      return true;
    }
    Serial.println(F("SD init failed, retrying...")); // Using F() macro
    delay(200);
  }
  return false;
}

void setup() {
  Serial.begin(9600);
  Serial1.begin(9600);

  pinMode(SD_CS, OUTPUT);
  pinMode(TFT_CS, OUTPUT);
  pinMode(DROP_BTN, INPUT_PULLUP);
  
  SPI.begin();
  digitalWrite(SD_CS, HIGH);
  digitalWrite(TFT_CS, HIGH);
  tft.init(240, 280);

  animatedIntro();

  servo1.attach(A0);
  servo2.attach(A1);
  servo3.attach(A2);
  servo4.attach(A3);

  servo1.write(91);
  servo2.write(91);
  servo3.write(90);
  servo4.write(90);

  pinMode(MOTOR_1, OUTPUT);
  pinMode(MOTOR_2, OUTPUT);
  pinMode(MOTOR_3, OUTPUT);
  pinMode(MOTOR_4, OUTPUT);
  delay(200);

  if (!initSD()) {
    Serial.println(F("Cannot initialize SD card!")); // Using F() macro
    while (1);
  }

  Serial.println(F("SD card ready.")); // Using F() macro
  filestat = loadScheduleData();

  if (!rtc.begin()) {
    Serial.println(F("RTC not found!")); // Using F() macro
  }
  rtc.adjust(DateTime(2025, 8, 15, 18, 59, 0));
  showMainMenu();
}

void loop() {
  rtctime = rtc.now();

  if (digitalRead(DROP_BTN) == LOW) {
    delay(50);
    if (digitalRead(DROP_BTN) == LOW) {
      if (setupMode) {
        handleTubeSetupButton();
      } else if (showNotification) {
        handleDispensing();
      }
      delay(500);
    }
  }

  static unsigned long lastUpdate = 0;
  const unsigned long refreshInterval = 5000;

  static int byteCounter = 0;
  static char tempBuffer[TEMP_BUFFER_SIZE + 1] = "";
  static int bufferPos = 0;

  while (Serial1.available()) {
    char c = Serial1.read();
    
    if (bufferPos < TEMP_BUFFER_SIZE) {
      tempBuffer[bufferPos++] = c;
      tempBuffer[bufferPos] = '\0';
    }
    
    byteCounter++;
    lastByteTime = millis();

    if (!receiving) {
      char* startPos = strstr(tempBuffer, "#START#");
      if (startPos != nullptr) {
        receiving = true;
        receiveStartTime = millis();
        
        int startOffset = startPos - tempBuffer + 7;
        int remainingLen = bufferPos - startOffset;
        if (remainingLen > 0) {
          memmove(tempBuffer, tempBuffer + startOffset, remainingLen);
          bufferPos = remainingLen;
          tempBuffer[bufferPos] = '\0';
        } else {
          bufferPos = 0;
          tempBuffer[0] = '\0';
        }

        if (!startStreamingSave()) {
          Serial.println(F("Failed to start streaming save")); // Using F() macro
          receiving = false;
          bufferPos = 0;
          continue;
        }
        Serial.println(F("Started receiving JSON data...")); // Using F() macro
      } else if (bufferPos >= TEMP_BUFFER_SIZE - 8) {
        memmove(tempBuffer, tempBuffer + TEMP_BUFFER_SIZE - 16, 16);
        bufferPos = 16;
        tempBuffer[bufferPos] = '\0';
      }
    } else {
      char* endPos = strstr(tempBuffer, "#END#");
      if (endPos != nullptr) {
        int finalLen = endPos - tempBuffer;
        if (finalLen > 0) {
          tempBuffer[finalLen] = '\0';
          String finalChunk = String(tempBuffer);
          writeStreamingChunk(finalChunk);
        }

        bool saved = finishStreamingSave();
        Serial.println(F("\nReceived complete JSON!")); // Using F() macro

        if (saved) {
          delay(2000);
          bool loaded = false;
          for (int attempt = 1; attempt <= 3; attempt++) {
            loaded = loadScheduleData();
            if (loaded) {
              Serial.print(F("Schedule loaded successfully after BT transfer (try ")); // Using F() macro
              Serial.print(attempt);
              Serial.println(F(").")); // Using F() macro
              currentTubeSetup = 0;
              setupMode = false;
              triggerSetupAfterBT = true;
              break;
            } else {
              Serial.print(F("Schedule load failed after BT transfer (try ")); // Using F() macro
              Serial.print(attempt);
              Serial.println(F("). Retrying...")); // Using F() macro
              delay(500);
            }
          }
          filestat = loaded;
          delay(2000);
        } else {
          filestat = false;
          Serial.println(F("Failed to save JSON to SD.")); // Using F() macro
        }

        int endOffset = (endPos - tempBuffer) + 5;
        int remainingLen = bufferPos - endOffset;
        if (remainingLen > 0) {
          memmove(tempBuffer, tempBuffer + endOffset, remainingLen);
          bufferPos = remainingLen;
          tempBuffer[bufferPos] = '\0';
        } else {
          bufferPos = 0;
          tempBuffer[0] = '\0';
        }
        
        receiving = false;
        Serial.println(F("Complete")); // Using F() macro
        Serial1.write('A');
      } else {
        if (bufferPos >= TEMP_BUFFER_SIZE - 8) {
          String chunkToWrite = String(tempBuffer).substring(0, TEMP_BUFFER_SIZE / 2);
          writeStreamingChunk(chunkToWrite);
          
          int keepLen = bufferPos - (TEMP_BUFFER_SIZE / 2);
          memmove(tempBuffer, tempBuffer + (TEMP_BUFFER_SIZE / 2), keepLen);
          bufferPos = keepLen;
          tempBuffer[bufferPos] = '\0';
        }
      }
    }

    if (byteCounter >= 32) {
      Serial1.write('A');
      byteCounter = 0;
    }
  }

  if (receiving) {
    if (millis() - lastByteTime > 5000) {
      Serial.println(F("Timeout: no new data, aborting streaming save.")); // Using F() macro
      if (streamingActive) {
        streamingFile.close();
        streamingActive = false;
        sdBusy = false;
      }
      bufferPos = 0;
      tempBuffer[0] = '\0';
      receiving = false;
    } else if (millis() - receiveStartTime > 20000) {
      Serial.println(F("Timeout: transmission too long, aborting streaming save.")); // Using F() macro
      if (streamingActive) {
        streamingFile.close();
        streamingActive = false;
        sdBusy = false;
      }
      bufferPos = 0;
      tempBuffer[0] = '\0';
      receiving = false;
    }
  }

  if (!receiving && millis() - lastUpdate >= refreshInterval) {
    showMainMenu();
    lastUpdate = millis();
  }
}
