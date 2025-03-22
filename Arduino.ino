#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <SoftwareSerial.h>
#include <RTClib.h>
#include <EEPROM.h>
#include <Adafruit_PWMServoDriver.h>

#define TRIG_PIN 9
#define ECHO_PIN 10
#define CONTAINER_HEIGHT 33
#define EMPTY_CONTAINER_DISTANCE 33

#define SERVO_FREQ 50
#define SERVO_0 0
#define SERVO_1 5
#define SERVO_2 8
#define SERVO_MIN_PULSE 102
#define SERVO_MAX_PULSE 491

#define LOW_FOOD_THRESHOLD 30 //food level
#define DISPLAY_TOGGLE_INTERVAL 60000 
#define CRITICAL_FOOD_THRESHOLD 5 // low level
#define EEPROM_FEEDING_TIMES_ADDR 0 

char statusBuffer[4]; 
char timeBuffer[9];   
char dateBuffer[11];  

SoftwareSerial espSerial(2, 3);
LiquidCrystal_I2C lcd(0x27, 16, 2);
RTC_DS3231 rtc;
Adafruit_PWMServoDriver pwm = Adafruit_PWMServoDriver();
unsigned long previousMillis = 0;
const long interval = 2000;
unsigned long displayToggleMillis = 0;
unsigned long dailyUpdateTime = 0;
const long dailyInterval = 86400000;
unsigned long lastSerialDebugTime = 0;
const long serialDebugInterval = 60000;
unsigned long lastHourlyCheckTime = 0;
const long hourlyInterval = 3600000; 

int morningHour, morningMin;
int afternoonHour, afternoonMin;
bool feedingInProgress = false;
bool displayMode = true; 
unsigned long lastMorningFeedTime = 0;
unsigned long lastAfternoonFeedTime = 0;
const long feedingCooldown = 60000;
int lastSentFoodPercentage = -1;  
int lastHourRecorded = -1;  
void setup() {
  Serial.begin(9600);
  espSerial.begin(9600);
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  Wire.begin();
  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(F("Food Monitor"));
  lcd.setCursor(0, 1);
  lcd.print(F("Starting..."));
  delay(2000);

  if (!rtc.begin()) {
    Serial.println(F("No RTC found"));
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(F("RTC ERROR"));
    delay(2000);
  }

  if (rtc.lostPower()) {
    rtc.adjust(DateTime(2024, 1, 1, 0, 0, 0));
  }
  pwm.begin();
  pwm.setPWMFreq(SERVO_FREQ);
  delay(10);
#ifdef ESP8266
  EEPROM.begin(512);
#endif
  loadFeedingTimes();
  updateDisplay();
  espSerial.println(F("ARDUINO_READY"));
}

void loop() {
  unsigned long currentMillis = millis();
  if (currentMillis - displayToggleMillis >= DISPLAY_TOGGLE_INTERVAL) {
    displayToggleMillis = currentMillis;
    if (!feedingInProgress) {
      displayMode = !displayMode; 
      lcd.clear(); 
      updateDisplay(); 
    }
  }
  if (currentMillis - dailyUpdateTime >= dailyInterval || dailyUpdateTime == 0) {
    dailyUpdateTime = currentMillis;
    updateFoodLevel(true);  
  }
  checkHourlyUpdate();
  checkFeedingSchedule();

  if (espSerial.available() > 0) {
    String command = espSerial.readStringUntil('\n');
    command.trim();
    Serial.println("Received from ESP32: " + command);
    if (command.startsWith("TIME_MORNING:")) {
      parseFeedingTime(command, morningHour, morningMin);
      saveFeedingTimes();
      Serial.println("Morning time updated: " + String(morningHour) + ":" + String(morningMin));
      espSerial.println("TIME_MORNING_CONFIRMED");
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Morning Feed: ");
      lcd.setCursor(0, 1);
      lcd.print(formatTime(morningHour, morningMin));
      delay(2000);
      updateDisplay();
    }
    else if (command.startsWith("TIME_AFTERNOON:")) {
      parseFeedingTime(command, afternoonHour, afternoonMin);
      saveFeedingTimes();
      Serial.println("Afternoon time updated: " + String(afternoonHour) + ":" + String(afternoonMin));
      espSerial.println("TIME_AFTERNOON_CONFIRMED");

      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Afternoon Feed: ");
      lcd.setCursor(0, 1);
      lcd.print(formatTime(afternoonHour, afternoonMin));
      delay(2000);
      updateDisplay();
    }
    else if (command == "UPDATE") {
      updateFoodLevel(true);
      espSerial.println("UPDATE_CONFIRMED");
      updateDisplay();
    }
    else if (command == "FEED" && !feedingInProgress) {
      startFeeding("Manual");
      espSerial.println("FEED_CONFIRMED");
    }
    else if (command == "GET_HOURLY_DATA") {
    }
    else if (command == "UPDATE_FOOD_CHART") {
    } 
  }
  if (currentMillis - previousMillis >= interval) {
    previousMillis = currentMillis;
    if (!feedingInProgress) {
      updateDisplay();
    }
  }
}

void checkHourlyUpdate() {
  DateTime now = rtc.now();
  unsigned long currentMillis = millis();
  if (currentMillis - lastHourlyCheckTime >= hourlyInterval || lastHourlyCheckTime == 0) {
    lastHourlyCheckTime = currentMillis;
    float distance = measureDistance();
    int foodPercentage = 100 - ((distance / CONTAINER_HEIGHT) * 100);
    foodPercentage = constrain(foodPercentage, 0, 100);
    espSerial.print(F("HOURLY_FOOD_LEVEL:"));
    espSerial.println(foodPercentage);

    Serial.print(F("Hourly Food Level sent: "));
    Serial.println(foodPercentage);
  }
}

void updateDisplay() {
  if (displayMode) {
    updateFoodDisplay();
  } else {
    updateTimeDisplay();
  }
}

void updateFoodDisplay() {
  float distance = measureDistance();
  int foodPercentage = 100 - ((distance / CONTAINER_HEIGHT) * 100);
  foodPercentage = constrain(foodPercentage, 0, 100);
  if (foodPercentage <= LOW_FOOD_THRESHOLD) {
    strcpy(statusBuffer, "LOW");
  } else {
    strcpy(statusBuffer, "OK");
  }

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(F("Food Level: "));
  lcd.print(foodPercentage);
  lcd.print('%');
  lcd.setCursor(0, 1);
  lcd.print(F("Status: "));
  lcd.print(statusBuffer);
}

void updateTimeDisplay() {
  DateTime now = rtc.now();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(F("Time: "));
  String formattedTime = formatTime(now.hour(), now.minute());
  lcd.print(formattedTime);
  lcd.setCursor(0, 1);
  lcd.print(F("Date: "));
  formatDate(now.day(), now.month(), now.year(), dateBuffer);
  lcd.print(dateBuffer);
}

void updateFoodLevel(bool sendToESP32) {
  float distance = measureDistance();
  int foodPercentage = 100 - ((distance / CONTAINER_HEIGHT) * 100);
  foodPercentage = constrain(foodPercentage, 0, 100);
  if (foodPercentage != lastSentFoodPercentage) {
    if (foodPercentage <= LOW_FOOD_THRESHOLD) {
      strcpy(statusBuffer, "LOW");
    } else {
      strcpy(statusBuffer, "OK");
    }

    if (sendToESP32) {
      espSerial.print(F("FOOD_LEVEL:"));
      espSerial.print(foodPercentage);
      espSerial.print(':');
      espSerial.println(statusBuffer);
    }

    lastSentFoodPercentage = foodPercentage;
  } else {
    Serial.println("Food level unchanged, skipping update.");
  }
}

void checkFeedingSchedule() {
  DateTime now = rtc.now();
  unsigned long currentTime = millis();
  static bool morningChecked = false;
  static bool afternoonChecked = false;

  if (!feedingInProgress) {
    if (now.hour() == morningHour && now.minute() == morningMin) {
      if (!morningChecked && (currentTime - lastMorningFeedTime > feedingCooldown)) {
        Serial.println("Morning Feed Triggered!");
        lastMorningFeedTime = currentTime;
        startFeeding(1);
        morningChecked = true;
      }
    } else {
      morningChecked = false;
    }

    if (now.hour() == afternoonHour && now.minute() == afternoonMin) {
      if (!afternoonChecked && (currentTime - lastAfternoonFeedTime > feedingCooldown)) {
        Serial.println("Afternoon Feed Triggered!");
        lastAfternoonFeedTime = currentTime;
        startFeeding(2);
        afternoonChecked = true;
      }
    } else {
      afternoonChecked = false;
    }
  }
}


void startFeeding(String period) {
  float distance = measureDistance();
  int foodPercentage = 100 - ((distance / CONTAINER_HEIGHT) * 100);
  foodPercentage = constrain(foodPercentage, 0, 100);

  if (foodPercentage <= CRITICAL_FOOD_THRESHOLD) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(F("Low Food Level"));
    lcd.setCursor(0, 1);
    lcd.print(F("Refill needed"));
    espSerial.println(F("FEED_BLOCKED:LOW_FOOD"));

    delay(3000);
    updateDisplay();
    return;
  }

  feedingInProgress = true;
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(period);
  lcd.print(F(" Feeding..."));
  lcd.setCursor(0, 1);
  lcd.print(F("Dispensing food"));

  espSerial.print(F("FEEDING_START:"));
  espSerial.println(period);
  dispenseFood();
  feedingInProgress = false;
  espSerial.print(F("FEEDING_COMPLETE:"));
  espSerial.println(period);
  updateFoodLevel(true);

  updateDisplay();
}


void startFeeding(uint8_t period) {
  static const char *periodNames[] = {"Manual", "Morning", "Afternoon"};
  float distance = measureDistance();
  int foodPercentage = 100 - ((distance / CONTAINER_HEIGHT) * 100);
  foodPercentage = constrain(foodPercentage, 0, 100);
  if (foodPercentage <= CRITICAL_FOOD_THRESHOLD) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(F("Low Food Level"));
    lcd.setCursor(0, 1);
    lcd.print(F("Refill needed"));
    espSerial.println(F("FEED_BLOCKED:LOW_FOOD"));

    delay(3000);
    updateDisplay();
    return;
  }

  feedingInProgress = true;
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(periodNames[period]);
  lcd.print(F(" Feeding..."));
  lcd.setCursor(0, 1);
  lcd.print(F("Dispensing food"));
  espSerial.print(F("FEEDING_START:"));
  espSerial.println(periodNames[period]);
  dispenseFood();
  feedingInProgress = false;
  espSerial.print(F("FEEDING_COMPLETE:"));
  espSerial.println(periodNames[period]);
  updateFoodLevel(true);

  updateDisplay();
}

void moveServoSmooth(uint8_t channel, int startAngle, int endAngle, int stepDelay) {
  int stepSize = 10;
  if (startAngle < endAngle) {
    for (int angle = startAngle; angle <= endAngle; angle += stepSize) {
      setServoAngle(channel, angle);
      delay(stepDelay);
    }
    setServoAngle(channel, endAngle);
  } else {
    for (int angle = startAngle; angle >= endAngle; angle -= stepSize) {
      setServoAngle(channel, angle);
      delay(stepDelay);
    }
    setServoAngle(channel, endAngle);
  }
}

void dispenseFood() {
  Serial.println("Starting dispenseFood()");

  Serial.println("Moving Servo 0 to 100 degrees");
  moveServoSmooth(SERVO_0, 0, 100, 1);
  delay(5000);  
  Serial.println("Returning Servo 0 to 0 degrees");
  moveServoSmooth(SERVO_0, 100, 0, 1); 
  delay(500);  

  Serial.println("Moving Servo 1 to 100 degrees");
  moveServoSmooth(SERVO_1, 0, 100, 0);
  delay(5000);  
  Serial.println("Returning Servo 1 to 0 degrees");
  moveServoSmooth(SERVO_1, 100, 0, 0);
  delay(500); 

  Serial.println("Moving Servo 2 to 100 degrees");
  moveServoSmooth(SERVO_2, 0, 100, 0); 
  delay(5000);  
  Serial.println("Returning Servo 2 to 0 degrees");
  moveServoSmooth(SERVO_2, 100, 0, 0);
  delay(500);   

  Serial.println("Finished dispenseFood()");
}


void setServoAngle(uint8_t channel, int angle) {
  angle = constrain(angle, 0, 180);
  int pulse = map(angle, 0, 180, SERVO_MIN_PULSE, SERVO_MAX_PULSE);
  pwm.setPWM(channel, 0, pulse);
  Serial.print("Servo Channel: ");
  Serial.print(channel);
  Serial.print(" Angle: ");
  Serial.print(angle);
  Serial.print(" Pulse: ");
  Serial.println(pulse);
}

void parseFeedingTime(String command, int &hour, int &minute) {
  int colonPos = command.indexOf(':');
  if (colonPos > 0) {
    String timeStr = command.substring(colonPos + 1);
    int timeColonPos = timeStr.indexOf(':');
    if (timeColonPos > 0) {
      hour = timeStr.substring(0, timeColonPos).toInt();
      minute = timeStr.substring(timeColonPos + 1).toInt();
      Serial.println("Parsed time: " + String(hour) + ":" + String(minute));
    }
  }
}

float measureDistance() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);
  long duration = pulseIn(ECHO_PIN, HIGH);
  float distance = duration * 0.034 / 2;
  return constrain(distance, 0, EMPTY_CONTAINER_DISTANCE);
}

int getFoodPercentage(float distance) {
  if (distance >= EMPTY_CONTAINER_DISTANCE) {
    return 0;  
  }

  int foodPercentage = 100 - ((distance / EMPTY_CONTAINER_DISTANCE) * 100);
  return constrain(foodPercentage, 0, 100);
}

String formatTime(int hour, int minute) {
  String timeString = "";
  if (hour < 10) timeString += "0";
  timeString += String(hour);
  timeString += ":";
  if (minute < 10) timeString += "0";
  timeString += String(minute);
  Serial.print("Formatted Time: ");
  Serial.println(timeString);
  return timeString;
}

void formatDate(uint8_t day, uint8_t month, uint16_t year, char *buffer) {
  sprintf(buffer, "%02d/%02d/%04d", day, month, year);
}

void saveFeedingTimes() {
  EEPROM.write(EEPROM_FEEDING_TIMES_ADDR, morningHour);
  EEPROM.write(EEPROM_FEEDING_TIMES_ADDR + 1, morningMin);
  EEPROM.write(EEPROM_FEEDING_TIMES_ADDR + 2, afternoonHour);
  EEPROM.write(EEPROM_FEEDING_TIMES_ADDR + 3, afternoonMin);
#ifdef ESP8266
  EEPROM.commit();
#endif
}

void loadFeedingTimes() {
#ifdef ESP8266
  EEPROM.begin(512);
#endif
  morningHour = EEPROM.read(EEPROM_FEEDING_TIMES_ADDR);
  morningMin = EEPROM.read(EEPROM_FEEDING_TIMES_ADDR + 1);
  afternoonHour = EEPROM.read(EEPROM_FEEDING_TIMES_ADDR + 2);
  afternoonMin = EEPROM.read(EEPROM_FEEDING_TIMES_ADDR + 3);

  if (morningHour > 23 || morningMin > 59 || afternoonHour > 23 || afternoonMin > 59) {
    morningHour = 8;
    morningMin = 0;
    afternoonHour = 17;
    afternoonMin = 0;
  }
}