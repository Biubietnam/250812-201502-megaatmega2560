#include <SPI.h>
#include <../lib/Servo/src/Servo.h>
#include <Arduino.h>
#include <SdFat.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <../lib/Servo/src/Servo.h>
#include <RTClib.h>
#include <ArduinoJson.h>

#define SD_CS 53
#define SERVO_PIN1 A0
#define SERVO_PIN2 A1
#define SERVO_PIN3 A2
#define SERVO_PIN4 A3

#define TFT_CS 10
#define TFT_RST 8
#define TFT_DC 9

#define MOTOR_PIN1 22 // Thêm chân điều khiển transistor
#define MOTOR_PIN2 24
#define MOTOR_PIN3 26
#define MOTOR_PIN4 28
DateTime rtctime;
Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_RST, TFT_DC);
RTC_DS3231 rtc;
SdFat SD;
File file;
Servo myServo;

String jsonBuffer = "";

bool filestat = false;

bool checkJsonFile()
{
  File f = SD.open("data.json", FILE_READ);
  if (!f)
  {
    Serial.println("Không tìm thấy data.json");
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
    Serial.print("Lỗi cú pháp JSON: ");
    Serial.println(err.c_str());
    return false;
  }

  Serial.println("JSON hợp lệ!");
  return true;
}

void setup()
{
  tft.init(240, 280);
  tft.setRotation(3);
  Serial.begin(9600);
  Serial1.begin(9600);

  tft.fillScreen(ST77XX_BLACK);
  tft.setCursor(120, 140);
  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(2);
  String inittext = "INITIALIZATION";
  int w = inittext.length() * 6 * 2;
  int h = 8 * 2;
  tft.drawRect(120 - 2, 140 - 2, w + 4, h + 4, ST77XX_WHITE);
  tft.print(inittext);
  filestat=checkJsonFile();
  delay(2000);

  pinMode(MOTOR_PIN1, OUTPUT);    // Khai báo chân 22 là OUTPUT
  digitalWrite(MOTOR_PIN1, HIGH); // ✅ Bật motor trước

  myServo.attach(SERVO_PIN1);
  myServo.attach(SERVO_PIN2);
  myServo.attach(SERVO_PIN3);
  myServo.attach(SERVO_PIN4);

  myServo.write(45); // ✅ Quay trái
  delay(500);        // Quay 0.5 giây
  myServo.write(90); // Dừng

  delay(10000); // ⏳ Chờ 10 giây

  myServo.write(135); // ✅ Quay phải
  delay(500);         // Quay 0.5 giây
  myServo.write(90);  // Dừng

  digitalWrite(MOTOR_PIN1, LOW); // ✅ Tắt motor sau 10s

  if (!SD.begin(SD_CS))
  {
    Serial.println("Không thể khởi động thẻ SD!");
    while (1)
      ;
  }
  Serial.println("Thẻ nhớ đã sẵn sàng.");
}

void showmainmeny()
{
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextSize(2);
  tft.setTextColor(ST77XX_WHITE);
  tft.setCursor(0, 0);
  tft.println(rtctime.hour() + ":" + rtctime.minute());
  tft.setCursor(0, 20);
  tft.println("Prep Status" + filestat ? "Set" : "Stanby");
}

bool receiving = false;

void loop()
{
  rtctime = rtc.now();
  while (Serial1.available())
  {
    char c = Serial1.read();

    if (!receiving)
    {
      if (jsonBuffer.endsWith("#START#"))
      {
        jsonBuffer = "";
        receiving = true;
      }
      else
      {
        jsonBuffer += c;
      }
    }
    else
    {
      jsonBuffer += c;
      if (jsonBuffer.endsWith("#END#"))
      {
        jsonBuffer.replace("#END#", "");
        Serial.println("\nĐã nhận đủ JSON!");
        saveJsonToSD(jsonBuffer);
        jsonBuffer = "";
        receiving = false;
      }
    }
  }
}
void saveJsonToSD(String data)
{
  file = SD.open("data.json", FILE_WRITE);
  if (file)
  {
    file.println(data);
    file.close();
    Serial.println("Đã ghi vào SD:");
    Serial.println(data);
  }
  else
  {
    Serial.println("Lỗi khi mở file SD!");
  }
}
