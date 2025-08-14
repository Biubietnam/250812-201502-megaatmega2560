#include <SPI.h>
#include <Arduino.h>
#include <SdFat.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <Servo.h>
#include <RTClib.h>
#include <ArduinoJson.h>

#define SD_CS 11
#define TFT_CS 10
#define TFT_RST 8
#define TFT_DC 9
#define MOTOR_1 22
#define MOTOR_2 24
#define MOTOR_3 26
#define MOTOR_4 28
#define DROP_BTN 30

Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);
RTC_DS3231 rtc;
SdFat SD;
File file;

String jsonBuffer = "";
bool filestat = false;
bool receiving = false;
DateTime rtctime;

Servo servo1;
Servo servo2;
Servo servo3;
Servo servo4;

// Menu state variables
int currentMenuPage = 0;
unsigned long lastMenuUpdate = 0;
bool showNotification = false;
String notificationMessage = "";
unsigned long notificationStartTime = 0;

// Schedule data structure
struct MedicationTime
{
  String time;
  String dosage;
  String medication;
  String tube;
  int amount;
};

MedicationTime schedules[10]; // Max 10 schedule entries
int scheduleCount = 0;

//  Added structure for grouping medications by time
struct GroupedMedication
{
  String time;
  String medications[5]; // Max 5 medications per time slot
  String dosages[5];
  String tubes[5];
  int amounts[5];
  int count;
};

GroupedMedication groupedSchedules[10];
int groupedCount = 0;

void openServo(Servo &servo, int standbyPos = 91, int openPos = 135)
{
  servo.write(openPos);
  delay(300);
  servo.write(standbyPos);
}

void closeServo(Servo &servo, int standbyPos = 91, int closePos = 45)
{
  servo.write(closePos);
  delay(300);
  servo.write(standbyPos);
}

// ================= NPN Motor Trigger =================
void triggerMotor(int motorPin, unsigned long durationMs)
{
  digitalWrite(motorPin, HIGH); // Turn motor ON via NPN transistor
  delay(durationMs);
  digitalWrite(motorPin, LOW); // Turn motor OFF
}
// ---------------- Animation Functions ----------------
void drawLoadingBar(int progress, int x, int y, int width, int height)
{
  // Draw border
  tft.drawRect(x, y, width, height, ST77XX_WHITE);

  // Fill progress
  int fillWidth = (progress * (width - 2)) / 100;
  if (fillWidth > 0)
  {
    tft.fillRect(x + 1, y + 1, fillWidth, height - 2, ST77XX_GREEN);
  }
}

void drawSpinner(int x, int y, int radius, int angle)
{
  // Clear previous spinner
  tft.fillCircle(x, y, radius + 2, ST77XX_BLACK);

  // Draw spinner segments
  for (int i = 0; i < 8; i++)
  {
    int segmentAngle = (angle + i * 45) % 360;
    int brightness = 255 - (i * 30);
    if (brightness < 50)
      brightness = 50;

    int x1 = x + (radius - 3) * cos(segmentAngle * PI / 180);
    int y1 = y + (radius - 3) * sin(segmentAngle * PI / 180);
    int x2 = x + radius * cos(segmentAngle * PI / 180);
    int y2 = y + radius * sin(segmentAngle * PI / 180);

    uint16_t color = tft.color565(brightness, brightness, brightness);
    tft.drawLine(x1, y1, x2, y2, color);
    tft.drawLine(x1 + 1, y1, x2 + 1, y2, color);
  }
}

void animatedIntro()
{
  tft.fillScreen(ST77XX_BLACK);
  tft.setRotation(3);

  // Phase 1: System startup text (0-800ms)
  tft.setTextSize(2);
  tft.setTextColor(ST77XX_CYAN);
  tft.setCursor(50, 50);
  tft.println("SYSTEM STARTUP");

  // Animated dots
  for (int i = 0; i < 3; i++)
  {
    delay(200);
    tft.print(".");
  }
  delay(200);

  // Phase 2: Component initialization (800-1600ms)
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextSize(1);
  tft.setTextColor(ST77XX_GREEN);

  String components[] = {"TFT Display", "SD Card", "RTC Module", "Servo Motors"};
  for (int i = 0; i < 4; i++)
  {
    tft.setCursor(20, 40 + i * 20);
    tft.print("[");
    delay(50);
    tft.print("OK");
    delay(50);
    tft.print("] ");
    tft.print(components[i]);
    delay(1000);
  }

  tft.fillScreen(ST77XX_BLACK);
  tft.setTextSize(2);
  tft.setTextColor(ST77XX_WHITE);
  tft.setCursor(80, 60);
  tft.println("LOADING");

  int barX = 40;
  int barY = 100;
  int barWidth = 200;
  int barHeight = 20;

  int spinnerX = (tft.width() / 2);
  int spinnerY = barY + barHeight + 45;
  int spinnerRadius = 15;

  for (int progress = 0; progress <= 100; progress += 2)
  {
    drawLoadingBar(progress, barX, barY, barWidth, barHeight);
    drawSpinner(spinnerX, spinnerY, spinnerRadius, progress * 18);

    tft.fillRect(120, 130, 40, 16, ST77XX_BLACK);
    tft.setTextSize(1);
    tft.setTextColor(ST77XX_YELLOW);
    tft.setCursor(120, 130);
    tft.print(progress);
    tft.print("%");

    delay(24);
  }

  tft.fillScreen(ST77XX_BLACK);
  tft.setTextSize(3);
  tft.setTextColor(ST77XX_WHITE);

  String initText = "READY";
  int charWidth = 6 * 3;
  int textWidth = initText.length() * charWidth;
  int x = (tft.width() - textWidth) / 2;
  int y = (tft.height() - 30) / 2;

  // Animated border
  for (int i = 0; i < 5; i++)
  {
    uint16_t color = (i % 2 == 0) ? ST77XX_GREEN : ST77XX_BLACK;
    tft.drawRect(x - 10, y - 10, textWidth + 20, 44, color);
    delay(40);
  }

  tft.setCursor(x, y);
  tft.println(initText);
  delay(1000);
}
void groupMedicationsByTime()
{
  groupedCount = 0;

  for (int i = 0; i < scheduleCount; i++)
  {
    String currentTime = schedules[i].time;
    int groupIndex = -1;

    // Check if time already exists in grouped schedules
    for (int j = 0; j < groupedCount; j++)
    {
      if (groupedSchedules[j].time == currentTime)
      {
        groupIndex = j;
        break;
      }
    }

    // If time doesn't exist, create new group
    if (groupIndex == -1)
    {
      groupIndex = groupedCount;
      groupedSchedules[groupIndex].time = currentTime;
      groupedSchedules[groupIndex].count = 0;
      groupedCount++;
    }

    // Add medication to the group
    int medIndex = groupedSchedules[groupIndex].count;
    if (medIndex < 5)
    { // Max 5 medications per time slot
      groupedSchedules[groupIndex].medications[medIndex] = schedules[i].medication;
      groupedSchedules[groupIndex].dosages[medIndex] = schedules[i].dosage;
      groupedSchedules[groupIndex].tubes[medIndex] = schedules[i].tube;
      groupedSchedules[groupIndex].amounts[medIndex] = schedules[i].amount;
      groupedSchedules[groupIndex].count++;
    }
  }
}
// ---------------- JSON Parsing Functions ----------------
bool loadScheduleData()
{
  File f = SD.open("data.json", FILE_READ);
  if (!f)
  {
    Serial.println("Cannot find data.json");
    return false;
  }

  String jsonStr;
  while (f.available())
  {
    jsonStr += (char)f.read();
  }
  f.close();

  StaticJsonDocument<2048> doc;
  DeserializationError err = deserializeJson(doc, jsonStr);

  if (err)
  {
    Serial.print("JSON parsing error: ");
    Serial.println(err.c_str());
    return false;
  }

  // Parse schedule data
  scheduleCount = 0;
  JsonArray medications = doc.as<JsonArray>();

  for (JsonObject med : medications)
  {
    String tube = med["tube"];
    String type = med["type"];
    int amount = med["amount"];

    JsonArray times = med["time_to_take"];
    for (JsonObject timeObj : times)
    {
      if (scheduleCount < 10)
      {
        schedules[scheduleCount].tube = tube;
        schedules[scheduleCount].medication = type;
        schedules[scheduleCount].amount = amount;
        schedules[scheduleCount].time = timeObj["time"].as<String>();
        schedules[scheduleCount].dosage = timeObj["dosage"].as<String>();
        scheduleCount++;
      }
    }
  }

  //  Group medications by time
  groupMedicationsByTime();

  Serial.print("Loaded ");
  Serial.print(scheduleCount);
  Serial.println(" medication schedules");
  return true;
}

//  New function to group medications by time

// Convert time string to minutes since midnight
int timeToMinutes(String timeStr)
{
  int colonIndex = timeStr.indexOf(':');
  if (colonIndex == -1)
    return -1;

  int hours = timeStr.substring(0, colonIndex).toInt();
  int minutes = timeStr.substring(colonIndex + 1).toInt();
  return hours * 60 + minutes;
}

//  Modified to work with grouped medications
int findNextMedication()
{
  int currentMinutes = rtctime.hour() * 60 + rtctime.minute();
  int closestIndex = -1;
  int minDifference = 24 * 60; // Max difference is 24 hours

  for (int i = 0; i < groupedCount; i++)
  {
    int scheduleMinutes = timeToMinutes(groupedSchedules[i].time);
    if (scheduleMinutes == -1)
      continue;

    int difference = scheduleMinutes - currentMinutes;
    if (difference < 0)
      difference += 24 * 60; // Next day

    if (difference < minDifference)
    {
      minDifference = difference;
      closestIndex = i;
    }
  }

  return closestIndex;
}

//  Modified to handle multiple medications at same time
bool checkMedicationTime()
{
  String currentTime = "";
  if (rtctime.hour() < 10)
    currentTime += "0";
  currentTime += String(rtctime.hour());
  currentTime += ":";
  if (rtctime.minute() < 10)
    currentTime += "0";
  currentTime += String(rtctime.minute());

  for (int i = 0; i < groupedCount; i++)
  {
    if (groupedSchedules[i].time == currentTime)
    {
      // Build notification message for multiple medications
      if (groupedSchedules[i].count == 1)
      {
        notificationMessage = "TIME TO TAKE: " + groupedSchedules[i].medications[0] + " - " + groupedSchedules[i].dosages[0];
      }
      else
      {
        notificationMessage = "TIME TO TAKE " + String(groupedSchedules[i].count) + " MEDS: ";
        for (int j = 0; j < groupedSchedules[i].count; j++)
        {
          if (j > 0)
            notificationMessage += " + ";
          notificationMessage += groupedSchedules[i].medications[j] + " (" + groupedSchedules[i].dosages[j] + ")";
        }
      }
      return true;
    }
  }
  return false;
}

// ---------------- Display Functions ----------------
void drawHeader()
{
  // Header background
  tft.fillRect(0, 0, 320, 35, ST77XX_BLUE);

  // Current time
  tft.setTextSize(2);
  tft.setTextColor(ST77XX_WHITE);
  tft.setCursor(10, 8);
  if (rtctime.hour() < 10)
    tft.print("0");
  tft.print(rtctime.hour());
  tft.print(":");
  if (rtctime.minute() < 10)
    tft.print("0");
  tft.print(rtctime.minute());

  // Date
  tft.setTextSize(1);
  tft.setCursor(10, 22);
  tft.print(rtctime.day());
  tft.print("/");
  tft.print(rtctime.month());
  tft.print("/");
  tft.print(rtctime.year());

  // Status indicator
  tft.setTextSize(1);
  tft.setCursor(200, 8);
  tft.print("STATUS: ");
  tft.setTextColor(filestat ? ST77XX_GREEN : ST77XX_RED);
  tft.print(filestat ? "READY" : "ERROR");

  // Battery/connection indicator (decorative)
  tft.fillRect(290, 8, 20, 12, ST77XX_GREEN);
  tft.drawRect(289, 7, 22, 14, ST77XX_WHITE);
  tft.fillRect(311, 10, 3, 8, ST77XX_WHITE);
}

//  Modified to display grouped medications
void drawGroupedMedicationCard(int x, int y, int width, int height, GroupedMedication group, bool isNext = false)
{
  // Card background
  uint16_t cardColor = isNext ? ST77XX_YELLOW : ST77XX_WHITE;
  uint16_t textColor = isNext ? ST77XX_BLACK : ST77XX_BLACK;

  tft.fillRoundRect(x, y, width, height, 8, cardColor);
  tft.drawRoundRect(x, y, width, height, 8, isNext ? ST77XX_RED : ST77XX_BLUE);

  // Time (larger, prominent)
  tft.setTextSize(2);
  tft.setTextColor(textColor);
  tft.setCursor(x + 8, y + 8);
  tft.print(group.time);

  // Medication count indicator
  if (group.count > 1)
  {
    tft.setTextSize(1);
    tft.setTextColor(ST77XX_RED);
    tft.setCursor(x + width - 50, y + 8);
    tft.print(group.count);
    tft.print(" MEDS");
  }

  // Show first medication prominently
  tft.setTextSize(1);
  tft.setTextColor(textColor);
  tft.setCursor(x + 8, y + 32);
  tft.print(group.medications[0]);
  tft.print(" - ");
  tft.print(group.dosages[0]);

  // Show second medication if exists
  if (group.count > 1)
  {
    tft.setCursor(x + 8, y + 45);
    tft.print(group.medications[1]);
    tft.print(" - ");
    tft.print(group.dosages[1]);
  }

  // Show "+X more" if more than 2 medications
  if (group.count > 2)
  {
    tft.setCursor(x + 8, y + 58);
    tft.print("+ ");
    tft.print(group.count - 2);
    tft.print(" more medications");
  }
  else if (group.count <= 2)
  {
    // Show tube info for single/double medications
    tft.setCursor(x + 8, y + 58);
    tft.print(group.tubes[0]);
    if (group.count == 2)
    {
      tft.print(", ");
      tft.print(group.tubes[1]);
    }
  }

  // Next indicator
  if (isNext)
  {
    tft.setTextSize(1);
    tft.setTextColor(ST77XX_RED);
    tft.setCursor(x + width - 35, y + height - 15);
    tft.print("NEXT");
  }
}

//  Enhanced notification display for multiple medications
void drawNotification()
{
  if (!showNotification)
    return;

  // Calculate notification height based on message length
  int notifHeight = 80;
  if (notificationMessage.length() > 50)
  {
    notifHeight = 100;
  }

  // Notification background
  tft.fillRect(10, 80, 300, notifHeight, ST77XX_RED);
  tft.drawRect(9, 79, 302, notifHeight + 2, ST77XX_WHITE);

  // Blinking effect
  bool blink = (millis() / 500) % 2;
  uint16_t textColor = blink ? ST77XX_WHITE : ST77XX_YELLOW;

  tft.setTextSize(1);
  tft.setTextColor(textColor);
  tft.setCursor(15, 90);
  tft.print("MEDICATION ALERT!");

  // Split long messages across multiple lines
  tft.setTextSize(1);
  int lineY = 105;
  int charsPerLine = 35;
  String msg = notificationMessage;

  while (msg.length() > 0 && lineY < 80 + notifHeight - 20)
  {
    String line = msg.substring(0, min((int)msg.length(), charsPerLine));

    // Try to break at word boundary
    if (msg.length() > charsPerLine)
    {
      int lastSpace = line.lastIndexOf(' ');
      if (lastSpace > 0)
      {
        line = msg.substring(0, lastSpace);
      }
    }

    tft.setCursor(15, lineY);
    tft.print(line);

    msg = msg.substring(line.length());
    if (msg.startsWith(" "))
      msg = msg.substring(1); // Remove leading space
    lineY += 12;
  }

  tft.setTextSize(1);
  tft.setCursor(15, 80 + notifHeight - 15);
  tft.print("Auto-dismiss in ");
  tft.print(30 - (millis() - notificationStartTime) / 1000);
  tft.print("s");

  // Auto-dismiss after 30 seconds
  if (millis() - notificationStartTime > 30000)
  {
    showNotification = false;
  }
}

void showMainMenu()
{
  tft.fillScreen(ST77XX_BLACK);

  // Draw header
  drawHeader();

  // Check for medication time notification
  if (checkMedicationTime() && !showNotification)
  {
    showNotification = true;
    notificationStartTime = millis();
  }

  // Show notification if active
  if (showNotification)
  {
    drawNotification();
    return;
  }

  // Main content area starts at y=40
  int contentY = 40;

  if (!filestat || groupedCount == 0)
  {
    // Error state
    tft.setTextSize(2);
    tft.setTextColor(ST77XX_RED);
    tft.setCursor(50, contentY + 50);
    tft.print("NO SCHEDULE DATA");

    tft.setTextSize(1);
    tft.setTextColor(ST77XX_WHITE);
    tft.setCursor(50, contentY + 80);
    tft.print("Please load medication");
    tft.setCursor(50, contentY + 95);
    tft.print("schedule via app");
    return;
  }

  // Find next medication group
  int nextMedIndex = findNextMedication();

  // Title
  tft.setTextSize(1);
  tft.setTextColor(ST77XX_CYAN);
  tft.setCursor(10, contentY + 5);
  tft.print("MEDICATION SCHEDULE");

  // Show next 3 medication groups
  int cardY = contentY + 25;
  int cardsShown = 0;

  // Show next medication group first (highlighted)
  if (nextMedIndex != -1)
  {
    drawGroupedMedicationCard(10, cardY, 300, 75, groupedSchedules[nextMedIndex], true);
    cardY += 85;
    cardsShown++;
  }

  // Show other upcoming medication groups
  for (int i = 0; i < groupedCount && cardsShown < 3; i++)
  {
    if (i != nextMedIndex)
    {
      drawGroupedMedicationCard(10, cardY, 300, 75, groupedSchedules[i], false);
      cardY += 85;
      cardsShown++;
    }
  }

  // Footer info
  tft.setTextSize(1);
  tft.setTextColor(ST77XX_CYAN);
  tft.setCursor(10, 260);
  tft.print("Total schedules: ");
  tft.print(groupedCount);
  tft.print(" (");
  tft.print(scheduleCount);
  tft.print(" doses)");

  // System info
  tft.setCursor(200, 260);
  tft.print("Auto-refresh: 5s");
}

// ... existing code ...

bool checkJsonFile()
{
  File f = SD.open("data.json", FILE_READ);
  if (!f)
  {
    Serial.println("Cannot find data.json");
    return false;
  };

  String jsonStr;
  while (f.available())
    jsonStr += (char)f.read();
  f.close();

  StaticJsonDocument<2048> doc;
  DeserializationError err = deserializeJson(doc, jsonStr);

  if (err)
  {
    Serial.print("JSON syntax error: ");
    Serial.println(err.c_str());
    return false;
  }

  Serial.println("JSON is valid!");
  return true;
}

void saveJsonToSD(String data)
{
  digitalWrite(SD_CS, LOW);
  file = SD.open("data.json", FILE_WRITE);
  if (file)
  {
    file.println(data);
    file.close();
    Serial.println("Written to SD:");
    Serial.println(data);
  }
  else
  {
    Serial.println("Error opening SD file!");
  }
  digitalWrite(SD_CS, HIGH);
}

bool initSD()
{
  for (int i = 0; i < 5; i++)
  { // Try up to 5 times
    digitalWrite(SD_CS, LOW);
    digitalWrite(TFT_CS, HIGH);
    if (SD.begin(SD_CS))
    {
      Serial.println("SD initialized.");
      return true;
    }
    Serial.println("SD init failed, retrying...");
    delay(200);
  }
  return false;
}

void setup()
{
  Serial.begin(9600);
  Serial1.begin(9600);

  pinMode(SD_CS, OUTPUT);
  pinMode(TFT_CS, OUTPUT);

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

  if (!initSD())
  {
    Serial.println("Cannot initialize SD card!");
    while (1)
      ;
  }

  Serial.println("SD card ready.");
  filestat = checkJsonFile();

  if (filestat)
  {
    loadScheduleData();
  }

  if (!rtc.begin())
  {
    Serial.println("RTC not found!");
  }
  showMainMenu();
}

void loop()
{
  rtctime = rtc.now();

  static unsigned long lastUpdate = 0;
  const unsigned long refreshInterval = 5000;

  if (millis() - lastUpdate >= refreshInterval)
  {
    showMainMenu();
    lastUpdate = millis();
  }

  while (Serial1.available())
  {
    char c = Serial1.read();
    jsonBuffer += c;

    if (!receiving)
    {
      int startIndex = jsonBuffer.indexOf("#START#");
      if (startIndex != -1)
      {
        receiving = true;
        jsonBuffer = jsonBuffer.substring(startIndex + 7);
      }
    }
    else
    {
      int endIndex = jsonBuffer.indexOf("#END#");
      if (endIndex != -1)
      {
        String payload = jsonBuffer.substring(0, endIndex);
        Serial.println("\nReceived complete JSON!");
        saveJsonToSD(payload);
        filestat = checkJsonFile();
        if (filestat)
        {
          loadScheduleData();
        }
        jsonBuffer = jsonBuffer.substring(endIndex + 5);
        receiving = false;
      }
    }

    if (jsonBuffer.length() > 4096)
    {
      Serial.println("Warning: buffer too large, clearing!");
      jsonBuffer = "";
      receiving = false;
    }
  }
}
