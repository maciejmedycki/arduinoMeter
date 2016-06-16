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

const byte currentPin = A0;
const byte voltagePin = A1;
const byte interruptPin = 2;

String utcString;
String localTimeString;

float currentRaw;
float currentMapped;

float voltageRaw ;
float voltageMapped;

unsigned int pulsesCount;

String dataString ;//stores data to be displayed on LCD and serial

unsigned long lastSerialOutput = 0;//when data was last displayed on Serial Port
const unsigned long serialOutputInterval = 2000;//serial output interval in ms

unsigned long lastLCDOutput = 0;//when data was last changed on LCD Display
const unsigned long LCDOutputInterval = 3000;//LCD display change interval in ms

unsigned long lastRPMCheck = 0;//when RPM was last time calculated

unsigned long lastSaveToSd = 0;//when data was saved to SD last time
const unsigned long saveToSdInterval = 60000;//Save to SD card interval in ms
bool sdInitialized = false;
String SDmessage = "";

volatile unsigned int pulseCounter = 0;
volatile unsigned long firstPulseMillis = 0;
unsigned int rpm = 0;

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
  delay(500);

  //RTC
  setSyncProvider(RTC.get);   // the function to get the time from the RTC

  if (timeStatus() != timeSet) {
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
  lcd.setCursor(0, 1);
  lcd.print(SDmessage);
  delay(500);
}

void loop() {
  //rtc date time
  utc = now();
  localTime = myTimeZone.toLocal(utc, &tcr);

  utcString = timeToString(utc);
  localTimeString = timeToString(localTime);

  //datalogger
  //datetime
  dataString = utcString + "," + localTimeString;

  //current
  currentRaw = analogReadOversample(currentPin);//66mV/A  0A = 2,5V 1A = 2,566V -1A = 2,434V 0V=0 5V=1023
  currentMapped = ((5000. * currentRaw / 1023.) - 2500) / 66.; //calculation in mV and then (XmV/(66mV/1A))
  dataString += "," + String(currentRaw) + "," + String(currentMapped);

  //voltage
  voltageRaw = analogReadOversample(voltagePin);
  voltageMapped = (5 * voltageRaw / 1023.) ;//V
  dataString += "," + String(voltageRaw) + "," + String(voltageMapped);

  //rpm
  pulsesCount = pulseCounter;
  unsigned long currentMillis = millis();
  unsigned long deltaT =  (unsigned long)(currentMillis - lastRPMCheck);
  dataString += "," + String(pulsesCount) + "," + String(deltaT);

  if (pulsesCount > 20 || deltaT > 5000 || lastRPMCheck == 0) {
    rpm = pulsesCount * 60000 / deltaT ;
    pulseCounter = 0;
    lastRPMCheck = currentMillis;
    Serial.println("start: " + String(firstPulseMillis) + " end: " + String(currentMillis) + " count: " + String(pulsesCount) + " rpm: " + String(rpm));
  }

  dataString += "," + String(rpm);

  //Printout data to serial
  if ((unsigned long)(currentMillis - lastSerialOutput) >= serialOutputInterval || lastSerialOutput == 0) {
    lastSerialOutput = currentMillis;
    Serial.println(dataString);
  }

  //lcd display data
  updateLcd(currentMillis);

  //setup date and time throught serial port data. Use YYYY-MM-DD HH:mm:SS format add T prefix (e.g. T2016-05-16 08:12:23). Remember to add zeros.
  setupDateTimeFromSerialPort();

  //saving to sd card
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
        dataFile.println(dataString);
        dataFile.close();
        SDmessage = "Data saved to SD";
      }
      else { // file save error
        Serial.println("Error on saving to " + fileName);
        SDmessage = "Data save error!";
      }
    }
  }
}

void pulseISR() {
  if (pulseCounter == 0) {
    firstPulseMillis = millis();
  }
  pulseCounter++;
}

void initSD() {
  sdInitialized = SD.begin(10);
  if (sdInitialized) {
    SDmessage = "SD Card ok";
    Serial.println("SD Card initialized.");
  }
  else {
    Serial.println("SD Card failed or not present");
    SDmessage = "SD Card error!";
  }
}

void updateLcd(unsigned long currentMillis) { //prints out data on LCD display
  if ((unsigned long)(currentMillis - lastLCDOutput) >= LCDOutputInterval || lastLCDOutput == 0) {
    lastLCDOutput = currentMillis;
    lcd.clear();
    //show date and time or rpms
    /*
    if (loopCounter % 10 > 5) {
      lcd.print(String(currentMapped, 1) + "A " + String(voltageMapped, 1) + "V");
      lcd.setCursor(0, 1);
      lcd.print(String(rpm) + "o/m" + " " + String(pulsesCount));
    }
    else {
      lcd.print(SDmessage);
      lcd.setCursor(0, 1);
      lcd.print(localTimeString);
    }
    */
    lcd.print(String(currentMapped, 1) + "A " + String(rpm) + " " + String(pulsesCount));
    lcd.setCursor(0, 1);
    lcd.print(localTimeString);
  }
}

//read serial for setting date and time(YYYY-MM-DD HH:mm:SS) add T prefix (e.g. T2016-05-16 08:12:23)
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

float analogReadOversample(int pin) {
  int sum = 0;
  for (int i = 0; i < 10; i++) {
    sum += analogRead(pin);
    delay(10);
  }
  return sum / 10.;
}

String timeToString(time_t t) {
  return formatToPlaces(year(t), 4) + "-" + formatToPlaces(month(t), 2) + "-" + formatToPlaces(day(t), 2) + " " + formatToPlaces(hour(t), 2) + ":" + formatToPlaces(minute(t), 2) + ":" + formatToPlaces(second(t), 2);
}

String formatToPlaces(int value, int places) {
  String s = String(value);
  while (s.length() != places) {
    s = "0" + s;
  }
  return s;
}

