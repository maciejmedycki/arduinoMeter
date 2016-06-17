#include <Wire.h>
#include <DS1307RTC.h>
#include <SPI.h>
#include <SD.h>
#include <LiquidCrystal.h>
#include <Time.h>
#include <Timezone.h>

LiquidCrystal lcd(4, 5, 6, 7, 8, 9);

TimeChangeRule utc1 = {"UTC1", Last, Sun, Mar, 2, 120};  //UTC + 1 hours
TimeChangeRule utc2 = {"UTC2", Last, Sun, Oct, 3, 60};   //UTC + 2 hours
Timezone myTimeZone(utc1, utc2);

TimeChangeRule *tcr;        //pointer to the time change rule, use to get myTimeZone abbrev
time_t utc, localTime;

static const uint8_t currentPin = A0;
static const uint8_t voltagePin = A1;
#define interruptPin  2

#define voltageDividerRatio  14.58150834 //voltage divider ratio

#define serialOutputInterval 2000 //serial output interval in ms
#define LCDOutputInterval    3000 //LCD display change interval in ms
#define saveToSdInterval     300000//Save to SD card interval in ms



float currentMapped;
float voltageMapped;

unsigned long lastSerialOutput = 0;//when data was last displayed on Serial Port
unsigned long lastLCDOutput = 0;//when data was last changed on LCD Display
unsigned long lastSaveToSd = 0;//when data was saved to SD last time

bool sdInitialized = false;//indicates whether SD card was sucessfully initialized
bool sdError       = false;//indicates if error occured while saving to sd Card

unsigned long currentMillis = 0;

//RPM volatiles
volatile unsigned long lastPulseMicros = 0;
volatile unsigned int  rpm = 0;

void setup() {
  //serial
  Serial.begin(115200);
  while (!Serial) {
    ; // wait for serial port to connect. Needed for native USB port only
  }

  //lcd
  lcd.begin(16, 2);

  lcd.setCursor(0, 0);
  lcd.print("Hello Leszek");
  Serial.println("Hello Leszek");

  //RTC
  setSyncProvider(RTC.get);   // the function to get the time from the RTC

  if (timeStatus() != timeSet) {
    delay(500);
    Serial.println("Unable to sync time with the RTC");
    lcd.clear();
    lcd.print("RTC sync error!");
  }
  /*else
    Serial.println("RTC has set the system time");
  */

  //interrupt for RPM
  pinMode(interruptPin, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(interruptPin), pulseISR, FALLING);

  //init SD card
  initSD();
}

void loop() {
  currentMillis = millis();

  //rtc date time
  utc = now();
  localTime = myTimeZone.toLocal(utc, &tcr);

  String utcString = timeToString(utc);
  String localTimeString = timeToString(localTime);

  //creating datalogger string
  //datetime
  String dataString = utcString + "," + localTimeString;//stores data to be displayed on serial port and stored to SD card

  //current
  float currentRaw = analogReadOversample(currentPin);//66mV/A  0A = 2,5V 1A = 2,566V -1A = 2,434V 0V=0 5V=1023
  currentMapped = (510. - currentRaw) * 0.074054326254; //calculation in mV and then (XmV/(66mV/1A))
  dataString += "," + String(currentRaw) + "," + String(currentMapped);

  //voltage
  float voltageRaw = analogReadOversample(voltagePin);
  voltageMapped = (voltageDividerRatio * voltageRaw * 0.00488758) ;//V
  dataString += "," + String(voltageRaw) + "," + String(voltageMapped);

  //rpm
  dataString += "," + String(rpm);

  //Printout data to serial
  printOnSerial(dataString);

  //lcd display data
  updateLcd(String(currentMapped, 1) + "A " + String(rpm) + " " + String(voltageMapped, 1) + "V", localTimeString);//TODO: what about RPMs?

  //saving to sd card
  saveToSdCard(dataString, localTimeString);

  //setup date and time throught serial port data. Use YYYY-MM-DD HH:mm:SS format add T prefix (e.g. T2016-05-16 08:12:23). Remember to add zeros.
  setupDateTimeFromSerialPort();
}

//handler for interruptPin interrupt
void pulseISR() {
  unsigned long currentMicros = micros();
  rpm = (60000000 / (unsigned long)(currentMicros - lastPulseMicros));
  lastPulseMicros = currentMicros;
}

//initializes SD card
void initSD() {
  sdInitialized = SD.begin(10);
  if (sdInitialized) {
    Serial.println("SD Card initialized.");
  }
  else {
    Serial.println("SD Card failed or not present");
  }
}

//prints on serial port every serialOutputInterval
void  printOnSerial(String dataString) {
  if ((unsigned long)(currentMillis - lastSerialOutput) >= serialOutputInterval || lastSerialOutput == 0) {
    lastSerialOutput = currentMillis;
    Serial.println(dataString);
  }
}

//updates LCD display every LCDOutputInterval
void updateLcd(String firstLine, String secondLine) { //prints out data on LCD display
  if ((unsigned long)(currentMillis - lastLCDOutput) >= LCDOutputInterval || lastLCDOutput == 0) {
    lastLCDOutput = currentMillis;
    lcd.clear();
    lcd.print(firstLine);
    lcd.setCursor(0, 1);
    if (!sdInitialized) {
      secondLine = "SD init error " + secondLine;
    }
    if (sdInitialized && !sdError) {
      secondLine = "SD write error " + secondLine;
    }
    lcd.print(secondLine);
    int moveCount = max(firstLine.length(), secondLine.length());
    for (int i = 16; i <= moveCount; i++) {
      delay(300);
      lcd.scrollDisplayLeft();
    }
    rpm = 0;//just to clear if there is no more movement detected
  }
}

//saves to SD card every saveToSdInterval
void saveToSdCard(String dataString, String localTimeString) {
  if ((unsigned long)(currentMillis - lastSaveToSd) >= saveToSdInterval || lastSaveToSd == 0) {
    lastSaveToSd = currentMillis;
    if (!sdInitialized) {
      initSD();
    }
    if (sdInitialized) {
      if (!SD.exists("datalog")) {
        SD.mkdir("datalog");
      }
      String fileName = "datalog/" + localTimeString.substring(2, 10) + ".csv";
      File dataFile = SD.open(fileName, FILE_WRITE);
      // if the file is available, write to it:
      if (dataFile) {
        sdError = false;
        dataFile.println(dataString);
        dataFile.close();
        Serial.println("Data saved to SD file " + fileName);
      }
      else { // file save error
        Serial.println("Error on saving to " + fileName);
        sdError = true;
      }
    }
  }
}

//reads serial for setting date and time(YYYY-MM-DD HH:mm:SS) add T prefix (e.g. T2016-05-16 08:12:23) use all the zeros
void setupDateTimeFromSerialPort() {
  if (Serial.available() > 0) {
    String incomingDateTime = Serial.readString();
    if (incomingDateTime.startsWith("T") && incomingDateTime.length() == 20) {
      Serial.println("Setting up time to: " + incomingDateTime);
      setTime(incomingDateTime.substring(12, 14).toInt(), incomingDateTime.substring(15, 17).toInt(), incomingDateTime.substring(18).toInt(),
              incomingDateTime.substring(9, 11).toInt(), incomingDateTime.substring(6, 8).toInt(), incomingDateTime.substring(1, 5).toInt()); //hr,min,sec,day,month,year
      RTC.set(now());
    }
  }
}

//performs some analogRead with simple oversampling
float analogReadOversample(int pin) {
  int sum = 0;
  for (int i = 0; i < 10; i++) {
    sum += analogRead(pin);
    delay(10);
  }
  return sum / 10.;
}

//helper for formatting time to string
String timeToString(time_t t) {
  return formatToPlaces(year(t), 4) + "-" + formatToPlaces(month(t), 2) + "-" + formatToPlaces(day(t), 2) + " " + formatToPlaces(hour(t), 2) + ":" + formatToPlaces(minute(t), 2) + ":" + formatToPlaces(second(t), 2);
}

//formats int value to given places
String formatToPlaces(int value, int places) {
  String s = String(value);
  while (s.length() != places) {
    s = "0" + s;
  }
  return s;
}

