#define BLYNK_TEMPLATE_ID "TMPL68qZLXAt6"
#define BLYNK_TEMPLATE_NAME "Automatic Feeding"

#include <WiFi.h>
#include <BlynkSimpleEsp32.h>
#include <TimeLib.h>
#include <WidgetRTC.h>

char auth[] = "JQb1gSt6rFdu2E3SbcHT4CXNLJiMOZw8";
char ssid[] = "PLDTHOMEFIBRK2dcK";
char pass[] = "PLDTWIFIDYTd3";

#define RXp2 16
#define TXp2 17
HardwareSerial espSerial(2);
#define FOOD_LEVEL_PIN V1
#define FOOD_STATUS_PIN V2
#define MORNING_TIME_PIN V3
#define AFTERNOON_TIME_PIN V4
#define FEEDING_STATUS_PIN V5
#define MANUAL_FEED_PIN V6


WidgetRTC rtc;
int morningHour, morningMin;
int afternoonHour, afternoonMin;
bool morningFed = false;
bool afternoonFed = false;

BlynkTimer timer;

int lastFoodPercentage = 0; 


BLYNK_CONNECTED() {
  rtc.begin();
  Blynk.syncVirtual(MORNING_TIME_PIN, AFTERNOON_TIME_PIN);
}

BLYNK_WRITE(MORNING_TIME_PIN) {
  TimeInputParam t(param);
  if (t.hasStartTime()) {
    morningHour = t.getStartHour();
    morningMin = t.getStartMinute();
    Serial.println("Morning feeding time set to: " + String(morningHour) + ":" + String(morningMin));
    char timeString[6];
    sprintf(timeString, "%02d:%02d", morningHour, morningMin);
    Serial.println("Sending to Arduino: TIME_MORNING:" + String(timeString) + "\\n");
    espSerial.println("TIME_MORNING:" + String(timeString));
    espSerial.println();
  }
}

BLYNK_WRITE(MANUAL_FEED_PIN) {
  int buttonState = param.asInt();

  if (buttonState == 1) {
    Serial.println("Manual feeding button pressed");
    Blynk.virtualWrite(FEEDING_STATUS_PIN, "Manual feeding");
    Blynk.logEvent("feeding_event", "Manual feeding initiated");
    espSerial.println("FEED");
    espSerial.println();
  }
}

BLYNK_WRITE(AFTERNOON_TIME_PIN) {
  TimeInputParam t(param);
  if (t.hasStartTime()) {
    afternoonHour = t.getStartHour();
    afternoonMin = t.getStartMinute();
    Serial.println("Afternoon feeding time set to: " + String(afternoonHour) + ":" + String(afternoonMin));
    char timeString[6];
    sprintf(timeString, "%02d:%02d", afternoonHour, afternoonMin);
    Serial.println("Sending to Arduino: TIME_AFTERNOON:" + String(timeString) + "\\n");
    espSerial.println("TIME_AFTERNOON:" + String(timeString));
    espSerial.println();
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println("=== ESP32 Food Monitoring System ===");
  espSerial.begin(9600, SERIAL_8N1, RXp2, TXp2);
  WiFi.begin(ssid, pass);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
  }

  Blynk.begin(auth, ssid, pass);
  Serial.println("Connected to Blynk server!");
  setSyncInterval(10 * 60);
  timer.setInterval(1000L, checkFeedingSchedule);
}


void checkFeedingSchedule() {
  int currentHour = hour();
  int currentMin = minute();

  if (currentHour == morningHour && currentMin == morningMin && !morningFed) {
    Serial.println("Morning feeding time reached");
    Blynk.virtualWrite(FEEDING_STATUS_PIN, "Morning feeding time");
    Blynk.logEvent("feeding_event", "Morning feeding time reached");
    morningFed = true;
    espSerial.println("FEED");
    espSerial.println();
  }

  if (currentHour == afternoonHour && currentMin == afternoonMin && !afternoonFed) {
    Serial.println("Afternoon feeding time reached");
    Blynk.virtualWrite(FEEDING_STATUS_PIN, "Afternoon feeding time");
    Blynk.logEvent("feeding_event", "Afternoon feeding time reached");
    afternoonFed = true;
    espSerial.println("FEED");
    espSerial.println();
  }

  if (currentHour == 0 && currentMin == 0) {
    morningFed = false;
    afternoonFed = false;
  }
}



void loop() {
  Blynk.run();
  timer.run();
  if (espSerial.available()) {
    String data = espSerial.readStringUntil('\n');
    data.trim();
    Serial.println("Received data: " + data);

    if (data.startsWith("FOOD_LEVEL:")) {
      int firstColon = data.indexOf(':');
      int secondColon = data.indexOf(':', firstColon + 1);
      String percentageStr = data.substring(firstColon + 1, secondColon);
      String status = data.substring(secondColon + 1);
      int foodPercentage = percentageStr.toInt();
      lastFoodPercentage = foodPercentage; 

      Blynk.virtualWrite(FOOD_LEVEL_PIN, foodPercentage);
      Blynk.virtualWrite(FOOD_STATUS_PIN, status);
      String statusText = "Food Level: " + String(foodPercentage) + "% - " + status;
      Blynk.virtualWrite(FEEDING_STATUS_PIN, statusText);
      Blynk.logEvent("food_level", statusText);
      if (status == "LOW") {
        Serial.println("Food level is low. Sending notification...");
        Blynk.logEvent("low_food", "Food level is low! Currently at " + String(foodPercentage) + "%");
      }
    } 
    else if (data.startsWith("FEEDING_START:")) {
      String period = data.substring(data.indexOf(':') + 1);
      String statusText = period + " feeding in progress...";
      Blynk.virtualWrite(FEEDING_STATUS_PIN, statusText);
      Blynk.logEvent("feeding_status", statusText);
    } 
    else if (data.startsWith("FEEDING_COMPLETE:")) {
      String period = data.substring(data.indexOf(':') + 1);
      String statusText = period + " feeding completed";
      Blynk.virtualWrite(FEEDING_STATUS_PIN, statusText);
      Blynk.logEvent("feeding_status", statusText);
      espSerial.println("UPDATE");
      espSerial.println();
    } 
    else if (data.startsWith("HOURLY_FOOD_LEVEL:")) {  
      String foodLevelStr = data.substring(data.indexOf(':') + 1);
      int hourlyFoodLevel = foodLevelStr.toInt();
      Serial.print("Received Hourly Food Level: ");
      Serial.println(hourlyFoodLevel);
      Blynk.virtualWrite(FOOD_LEVEL_PIN, hourlyFoodLevel); 

    } else {
      Serial.print("Unexpected data format: ");
      Serial.println(data);
    }
  }

  delay(50);
}