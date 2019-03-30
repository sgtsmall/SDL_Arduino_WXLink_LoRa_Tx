// SDL_Arduino_WeatherLink_LoRa_Tx
// SwitchDoc Labs April 2018
//
#define TXDEBUG
//#undef TXDEBUG
#include <JeeLib.h>

#include "MemoryFree.h"

#define LED 13

#define SOFTWAREVERSION 7

// WIRELESSID is changed if you have more than one unit reporting in the same area.  It is coded in protocol as WIRELESSID*10+SOFTWAREVERSION
#define WIRELESSID 3
// Number of milliseconds between data out - set 1000 or or 30000 or 60000 if you are using DS3231
//#define SLEEPCYCLE 30000

// WIRELESSID is changed if you have more than one unit reporting in the same area.  It is coded in protocol as WIRELESSID*10+SOFTWAREVERSION
//#define WIRELESSID 2
// Number of milliseconds between data out - set 1000 or or 30000 or 60000 if you are using DS3231
#define SLEEPCYCLE 29000
//#define SLEEPCYCLE 14000

#include "Crc16.h"

//Crc 16 library (XModem)
Crc16 crc;

ISR(WDT_vect) {
  Sleepy::watchdogEvent();
}
#include <SoftwareSerial.h>
#include <RH_RF95.h>

#include <avr/sleep.h>
#include <avr/power.h>
#include "SDL_Arduino_INA3221.h"

SDL_Arduino_INA3221 SunAirPlus;

// the three channels of the INA3221 named for SunAirPlus Solar Power Controller channels (www.switchdoc.com)
#define LIPO_BATTERY_CHANNEL 1
#define SOLAR_CELL_CHANNEL 2
#define OUTPUT_CHANNEL 3

#define ENABLE_RADIO 5

#define WATCHDOG_1 8
#define WATCHDOG_2 9

// Number of milliseconds between data out - set 1000 or or 30000 or 60000 if you are using DS3231


// Note:  If you are using a External WatchDog Timer, then you should set the timer to exceed SLEEPCYCLE by at least 10%.  If you are using the SwitchDoc Labs Dual WatchDog, SLEEPCYCLE must be less than 240 seconds
//        or what you set the WatchDog timeout interval.

// Note:  If you are using the alarm DS3231 system, you only have the choice of every second or every minute, but you can change this to some degree:


/*
  Values for Alarm 1

  ALM1_EVERY_SECOND -- causes an alarm once per second.
  ALM1_MATCH_SECONDS -- causes an alarm when the seconds match (i.e. once per minute).
  ALM1_MATCH_MINUTES -- causes an alarm when the minutes and seconds match.
  ALM1_MATCH_HOURS -- causes an alarm when the hours and minutes and seconds match.
  ALM1_MATCH_DATE -- causes an alarm when the date of the month and hours and minutes and seconds match.
  ALM1_MATCH_DAY -- causes an alarm when the day of the week and hours and minutes and seconds match.
  Values for Alarm 2

  ALM2_EVERY_MINUTE -- causes an alarm once per minute.
  ALM2_MATCH_MINUTES -- causes an alarm when the minutes match (i.e. once per hour).
  ALM2_MATCH_HOURS -- causes an alarm when the hours and minutes match.
  ALM2_MATCH_DATE -- causes an alarm when the date of the month and hours and minutes match.
  ALM2_MATCH_DAY -- causes an alarm when the day of the week and hours and minutes match.

*/


SoftwareSerial SoftSerial(6, 7); // TX, RX

RH_RF95 rf95(SoftSerial);

unsigned long MessageCount = 0;

unsigned long badAM2315Reads = 0;






#include "avr/pgmspace.h"
#include <Time.h>
#include <TimeLib.h>
#include "DS3232RTC.h"

#include <Wire.h>

// Note:   We found a long term reliablity problem with the AM2315 (took weeks to manifest), so we went to our High Reliablity AM2315 and modified it to work on the Mini Pro LP.
#include "SDL_ESP8266_HR_AM2315.h"

typedef enum  {

  NO_INTERRUPT,
  IGNORE_INTERRUPT,
  SLEEP_INTERRUPT,
  RAIN_INTERRUPT,
  ANEMOMETER_INTERRUPT,
  ALARM_INTERRUPT,
  REBOOT
} wakestate;


// Device Present State Variables

bool SunAirPlus_Present;
bool DS3231_Present;

byte byteBuffer[200]; // contains string to be sent to RX unit

// State Variables
byte Protocol;
long TimeStamp;
int WindDirection;
float AveWindSpeed;
long TotalRainClicks;
float MaxWindGust;
float OutsideTemperature;
float OutsideHumidity;
float BatteryVoltage;
float BatteryCurrent;
float LoadVoltage;
float LoadCurrent;
float SolarPanelVoltage;
float SolarPanelCurrent;
float AuxA;
float AuxB;
int protocolBufferCount;

wakestate wakeState;  // who woke us up?

bool Alarm_State_1;
bool Alarm_State_2;
bool ignore_anemometer_interrupt;
long nextSleepLength;

long lastRainTime;
long windClicks;
long  lastWindTime;
long shortestWindTime;

// Interfaced Devices

#include "WeatherRack.h"

// Grove AM2315 - 5V Arduino Drivers

SDL_ESP8266_HR_AM2315 am2315;

float dataAM2315[2];  //Array to hold data returned by sensor.  [0,1] => [Humidity, Temperature]
boolean OK;  // 1=successful read




//
// Protocol - 1 Byte - 1 - byte
// TimeStamp - 4 Bytes - milliseconds since bootup - long
// WindDirection -2 Byte - instantenous wind direction - int - degrees
// Ave Wind Speed - 4 bytes - average in the sample interval - float - kph
// Rain Total - 4 bytes - clicks since bootup - long - clicks
// Wind Gust - 4 bytes - high during sample interval - float - kph
// Outside Temperature - 4 bytes - instantenous temperature - float - degrees c
// Outside Humidity - 4 bytes - instaneous humidity - float - % RH
// Aux A - 4 Bytes - user defined - float
// Aux B - 4 bytes - user define - float
// Buffer Total Bytes
//
//

int convert4ByteLongVariables(int bufferCount, long myVariable)
{

  int i;

  union {
    long a;
    unsigned char bytes[4];
  } thing;
  thing.a = myVariable;

  for (i = 0; i < 4; i++)
  {
    byteBuffer[bufferCount] = thing.bytes[i];
    bufferCount++;
  }
  return bufferCount;

}

int convert4ByteFloatVariables(int bufferCount, float myVariable)
{
  int i;

  union {
    float a;
    unsigned char bytes[4];
  } thing;
  thing.a = myVariable;

  for (i = 0; i < 4; i++)
  {
    byteBuffer[bufferCount] = thing.bytes[i];


    bufferCount++;
  }

  return bufferCount;
}


int convert2ByteVariables(int bufferCount, int myVariable)
{


  union {
    int a;
    unsigned char bytes[2];
  } thing;

  thing.a = myVariable;


  byteBuffer[bufferCount] = thing.bytes[0];
  bufferCount++;
  byteBuffer[bufferCount] = thing.bytes[1];
  bufferCount++;
#if defined(TXDEBUG)
  Serial.println(F("-------"));
  Serial.println(thing.bytes[0]);
  Serial.println(thing.bytes[1]);
  Serial.println(F("------"));
#endif
  return bufferCount;

}

int convert1ByteVariables(int bufferCount, int myVariable)
{


  byteBuffer[bufferCount] = (byte) myVariable;
  bufferCount++;
  return bufferCount;

}

int checkSum(int bufferCount)
{
  unsigned short checksumValue;
  // calculate checksum
  checksumValue = crc.XModemCrc(byteBuffer, 0, 59);
#if defined(TXDEBUG)
  Serial.print(F("crc = 0x"));
  Serial.println(checksumValue, HEX);
#endif

  byteBuffer[bufferCount] = checksumValue >> 8;
  bufferCount++;
  byteBuffer[bufferCount] = checksumValue & 0xFF;
  bufferCount++;

  return bufferCount;
}

int buildProtocolString()
{

  int bufferCount;


  bufferCount = 0;

  byteBuffer[bufferCount] = 0xAB;
  bufferCount++;
  byteBuffer[bufferCount] = 0x66;
  bufferCount++;
  bufferCount = convert1ByteVariables(bufferCount, Protocol);
  bufferCount = convert4ByteLongVariables(bufferCount, TimeStamp / 100);
  bufferCount = convert2ByteVariables(bufferCount, WindDirection);
  bufferCount = convert4ByteFloatVariables(bufferCount, AveWindSpeed);
  bufferCount = convert4ByteLongVariables(bufferCount, windClicks);
  bufferCount = convert4ByteLongVariables(bufferCount, TotalRainClicks);
  bufferCount = convert4ByteFloatVariables(bufferCount, MaxWindGust);
  bufferCount = convert4ByteFloatVariables(bufferCount, OutsideTemperature);
  bufferCount = convert4ByteFloatVariables(bufferCount, OutsideHumidity);

  bufferCount = convert4ByteFloatVariables(bufferCount, BatteryVoltage);
  bufferCount = convert4ByteFloatVariables(bufferCount, BatteryCurrent);
  bufferCount = convert4ByteFloatVariables(bufferCount, LoadCurrent);
  bufferCount = convert4ByteFloatVariables(bufferCount, SolarPanelVoltage);
  bufferCount = convert4ByteFloatVariables(bufferCount, SolarPanelCurrent);

  bufferCount = convert4ByteFloatVariables(bufferCount, AuxA);
  bufferCount = convert4ByteLongVariables(bufferCount, MessageCount);
  protocolBufferCount = bufferCount + 2;
  //     bufferCount = convert1ByteVariables(bufferCount, protocolBufferCount);
  bufferCount = checkSum(bufferCount);




  return bufferCount;


}



void printStringBuffer()
{
  int bufferLength;

  bufferLength = protocolBufferCount;
  int i;
  for (i = 0; i < bufferLength; i++)
  {
    Serial.print(F("i="));
    Serial.print(i);
    Serial.print(F(" | "));
    Serial.println(byteBuffer[i], HEX);
  }

}


// DS3231 Library functions



const char *monthName[12] = {
  "Jan", "Feb", "Mar", "Apr", "May", "Jun",
  "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};

void printDigits(int digits) {
  // utility function for digital clock display: prints an leading 0

  if (digits < 10)
    Serial.print(F("0"));
  Serial.print(digits);
}

void digitalClockDisplay() {

  tmElements_t tm;
  RTC.read(tm);

  // digital clock display of the time
  printDigits(tm.Hour);
  Serial.print(F(":"));
  printDigits(tm.Minute);
  Serial.print(F(":"));
  printDigits(tm.Second);
  Serial.print(F(" "));
  Serial.print(tm.Day);
  Serial.print(F("/"));
  Serial.print(tm.Month);
  Serial.print(F("/"));
  Serial.print(tm.Year + 1970);
  Serial.println();
}

void return2Digits(char returnString[], char *buffer2, int digits)
{
  if (digits < 10)
    sprintf(returnString, "0%i", digits);
  else
    sprintf(returnString, "%i", digits);

  strcpy(returnString, buffer2);


}

void set32KHz(bool setValue)
{

  uint8_t s = RTC.readRTC(RTC_STATUS);

  if (setValue == true)
  {
    s = s | (1 << EN32KHZ);
    RTC.writeRTC(RTC_STATUS, s);

  }
  else
  {
    uint8_t flag;
    flag =  ~(1 << EN32KHZ);
    s = s & flag;
    RTC.writeRTC(RTC_STATUS, s);

  }



}

// AT24C32 EEPROM

#include "AT24C32.h"


void ResetWatchdog()
{

  if (badAM2315Reads < 5)
  {
    digitalWrite(WATCHDOG_1, LOW);
    delay(200);
    digitalWrite(WATCHDOG_1, HIGH);

#if defined(TXDEBUG)
    Serial.println(F("Watchdog1 Reset - Patted the Dog"));
#endif
  }
  else
  {
#if defined(TXDEBUG)
    Serial.println(F("AM2315 bad read > 5, stop patting dog"));
#endif
  }
}



void setup()
{
  Serial.begin(115200);    // TXDEBUGging only

  if (!rf95.init())
  {
    Serial.println(F("init failed"));
    while (1);
  }

  // Defaults after init are 434.0MHz, 13dBm, Bw = 125 kHz, Cr = 4/5, Sf = 128chips/symbol, CRC on

  // The default transmitter power is 13dBm, using PA_BOOST.
  // If you are using RFM95/96/97/98 modules which uses the PA_BOOST transmitter pin, then
  // you can set transmitter powers from 5 to 23 dBm:
  //rf95.setTxPower(13, false);

  rf95.setFrequency(434.0);
  rf95.setTxPower(13);
  
  rf95.setModemConfig(RH_RF95::Bw31_25Cr48Sf512);
  //rf95.setModemConfig(RH_RF95::Bw125Cr48Sf4096);



  //rf95.printRegisters();

  //SoftSerial.begin(9600);

  Serial.print(F("Wireless ID:"));
  Serial.println(WIRELESSID);

  Serial.print(F("Software Version:"));
  Serial.println(SOFTWAREVERSION);

  pinMode(LED, OUTPUT);

  digitalWrite(LED, HIGH);
  delay(1000);
  digitalWrite(LED, LOW);
  delay(1000);
  digitalWrite(LED, HIGH);
  delay(1000);
  digitalWrite(LED, LOW);
  delay(1000);
  digitalWrite(LED, HIGH);
  delay(1000);
  digitalWrite(LED, LOW);
  // setup initial values of variables

  wakeState = REBOOT;
  Alarm_State_1 = false;
  Alarm_State_2 = false;
  nextSleepLength = SLEEPCYCLE;


  Protocol = WIRELESSID * 10 + SOFTWAREVERSION;
  TimeStamp = 0;
  WindDirection = 0;
  AveWindSpeed = 0.0;
  TotalRainClicks = 0;
  MaxWindGust = 0.0;
  OutsideTemperature = 0.0;
  OutsideHumidity = 0.0;
  BatteryVoltage = 0.0;
  BatteryCurrent = 0.0;
  LoadCurrent = 0.0;
  SolarPanelVoltage = 0.0;
  SolarPanelCurrent = 0.0;
  AuxA = 0.0;
  AuxB = 0.0;

  // protocol 1



  // WeatherRack
  lastRainTime = 0;
  lastWindTime = 0;
  shortestWindTime = 10000000;

  pinMode(WATCHDOG_1, OUTPUT);
  digitalWrite(WATCHDOG_1, HIGH);
  // Just to turn off LED on _1_
  //pinMode(WATCHDOG_2, OUTPUT);
  //digitalWrite(WATCHDOG_2, HIGH);

  Wire.begin();

  // set up interrupts

  pinMode(2, INPUT);     // pinAnem is input to which a switch is connected
  digitalWrite(2, HIGH);   // Configure internal pull-up resistor
  pinMode(3, INPUT);     // pinRain is input to which a switch is connected
  digitalWrite(3, HIGH);   // Configure internal pull-up resistor

  // Now set up Transmitter off
  digitalWrite(ENABLE_RADIO, HIGH);
  pinMode(ENABLE_RADIO, OUTPUT);

  ignore_anemometer_interrupt = false;
  attachInterrupt(0, serviceInterruptAnem, RISING);
  attachInterrupt(1, serviceInterruptRain, FALLING);

  // test for DS3231 Present
  DS3231_Present = false;

  setSyncProvider(RTC.get);   // the function to get the time from the RTC
  if (timeStatus() != timeSet)
  {
    Serial.println(F("DS3231 Not Present"));
    DS3231_Present = false;
  }
  else
  {
    Serial.println(F("DS3231 Present"));
    digitalClockDisplay();
    DS3231_Present = true;
    //   Disable SQW

    RTC.squareWave(SQWAVE_NONE);
    set32KHz(false);

    // Now set up the alarm time
    // we only have a few  choices. for repeatable alarms (once per second, once per minute, once per hour, once per day, once per date and once per day of the week).
    // we are just choosing 1 per second or once per minute), you can get clever and use both alarms to do 30 seconds, 30 minutes, etc.
    if (SLEEPCYCLE < 1001)
    {


      // hits every second
      // set the alarm to go off.

      RTC.setAlarm(ALM1_EVERY_SECOND, 0, 0, 0, 0);
      RTC.alarm(ALARM_1);
      RTC.alarm(ALARM_2);
      RTC.alarmInterrupt(ALARM_1, true);


    }
    else if (SLEEPCYCLE < 30001)
    {
      // choose once per 30 seconds

      RTC.setAlarm(ALM1_MATCH_SECONDS, 30, 0, 0, 0);
      RTC.alarm(ALARM_1);
      RTC.setAlarm(ALM2_EVERY_MINUTE , 0, 0, 0, 0);
      RTC.alarm(ALARM_2);
      RTC.alarmInterrupt(ALARM_1, true);
      RTC.alarmInterrupt(ALARM_2, true);

    }
    else // if (SLEEPCYCLE < 60001)
    {
      // choose once per minute

      RTC.setAlarm(ALM1_MATCH_SECONDS, 0, 0, 0, 0);
      RTC.alarm(ALARM_1);
      RTC.alarm(ALARM_2);
      RTC.alarmInterrupt(ALARM_1, true);


    }

    uint8_t readValue;

  }

  uint8_t s = RTC.readRTC(RTC_STATUS);
  Serial.print(F("RTC_STATUS="));
  Serial.println(s, BIN);
  s = RTC.readRTC(RTC_CONTROL);
  Serial.print(F("RTC_CONTROL="));
  Serial.println(s, BIN);


  // test for SunAirPlus_Present
  SunAirPlus_Present = false;

  LoadVoltage = SunAirPlus.getBusVoltage_V(OUTPUT_CHANNEL);

  Serial.print("SAP Load Voltage =");
  Serial.println(LoadVoltage);

  LoadVoltage = SunAirPlus.getBusVoltage_V(OUTPUT_CHANNEL);
  LoadCurrent = SunAirPlus.getCurrent_mA(OUTPUT_CHANNEL);


  BatteryVoltage = SunAirPlus.getBusVoltage_V(LIPO_BATTERY_CHANNEL);
  BatteryCurrent = SunAirPlus.getCurrent_mA(LIPO_BATTERY_CHANNEL);

  SolarPanelVoltage = SunAirPlus.getBusVoltage_V(SOLAR_CELL_CHANNEL);
  SolarPanelCurrent = -SunAirPlus.getCurrent_mA(SOLAR_CELL_CHANNEL);

  Serial.println("");
  Serial.print(F("LIPO_Battery Load Voltage:  ")); Serial.print(BatteryVoltage); Serial.println(F(" V"));
  Serial.print(F("LIPO_Battery Current:       ")); Serial.print(BatteryCurrent); Serial.println(F(" mA"));
  Serial.println("");

  Serial.print(F("Solar Panel Voltage:   ")); Serial.print(SolarPanelVoltage); Serial.println(F(" V"));
  Serial.print(F("Solar Panel Current:   ")); Serial.print(SolarPanelCurrent); Serial.println(F(" mA"));
  Serial.println("");

  Serial.print(F("Load Voltage:   ")); Serial.print(LoadVoltage); Serial.println(F(" V"));
  Serial.print(F("Load Current:   ")); Serial.print(LoadCurrent); Serial.println(F(" mA"));
  Serial.println("");

  if (LoadVoltage < 0.1)
  {
    SunAirPlus_Present = false;
    Serial.println(F("SunAirPlus Not Present"));
  }
  else
  {
    SunAirPlus_Present = true;
    Serial.println(F("SunAirPlus Present"));
  }




}




void loop()
{

  if (DS3231_Present)
  {
    RTC.get();
#if defined(TXDEBUG)
    digitalClockDisplay();
#endif
  }
  // Only send if source is SLEEP_INTERRUPT
#if defined(TXDEBUG)
  Serial.print(F("wakeState="));
  Serial.println(wakeState);
#endif


  if ((wakeState == SLEEP_INTERRUPT) || (Alarm_State_1 == true)  || (Alarm_State_2 == true))
  {

    wakeState = NO_INTERRUPT;
    Alarm_State_1 = false;
    Alarm_State_2 = false;

    Serial.print(F("MessageCount="));
    Serial.println(MessageCount);

    OK = am2315.readData(dataAM2315);

    if (OK) {
      Serial.print(F("Outside Temperature (C): ")); Serial.println(dataAM2315[1]);
      Serial.print(F("Outside Humidity (%RH): ")); Serial.println(dataAM2315[0]);
      OutsideTemperature = dataAM2315[1];
      OutsideHumidity = dataAM2315[0];


    }
    else
    {
      Serial.println(F("AM2315 not found"));
      badAM2315Reads++;   // increment the bad one

    }

    // now read wind vane

    float WindVaneVoltage;
    WindVaneVoltage = analogRead(A1) * (5.0 / 1023.0);
    Serial.print(F("Wind Vane Voltage ="));
    Serial.println(WindVaneVoltage);
    Serial.print(F("Wind Vane Degrees ="));
    WindDirection = voltageToDegrees(WindVaneVoltage, 0.0);
    Serial.println(WindDirection);
    Serial.print(F("TotalRainClicks="));
    Serial.println(TotalRainClicks);
    Serial.print(F("windClicks="));
    Serial.println(windClicks);

    // calculate wind
    AveWindSpeed = (((float)windClicks / 2.0) / (((float)SLEEPCYCLE) / 1000.0)) * 2.4; // 2.4 KPH/click

    Serial.print(F("Average Wind Speed="));
    Serial.print(AveWindSpeed);
    Serial.println(F(" KPH"));

    Serial.print("shortestWindTime (usec)");
    Serial.println(shortestWindTime);

    MaxWindGust = 0.0;

    shortestWindTime = 10000000;

    TimeStamp = millis();

    // if SunAirPlus present, read charge data

    if (SunAirPlus_Present)
    {

      LoadVoltage = SunAirPlus.getBusVoltage_V(OUTPUT_CHANNEL);
      LoadCurrent = SunAirPlus.getCurrent_mA(OUTPUT_CHANNEL);


      BatteryVoltage = SunAirPlus.getBusVoltage_V(LIPO_BATTERY_CHANNEL);
      BatteryCurrent = SunAirPlus.getCurrent_mA(LIPO_BATTERY_CHANNEL);

      SolarPanelVoltage = SunAirPlus.getBusVoltage_V(SOLAR_CELL_CHANNEL);
      SolarPanelCurrent = -SunAirPlus.getCurrent_mA(SOLAR_CELL_CHANNEL);

      Serial.println("");
      Serial.print(F("LIPO_Battery Load Voltage:  ")); Serial.print(BatteryVoltage); Serial.println(F(" V"));
      Serial.print(F("LIPO_Battery Current:       ")); Serial.print(BatteryCurrent); Serial.println(F(" mA"));
      Serial.println("");

      Serial.print(F("Solar Panel Voltage:   ")); Serial.print(SolarPanelVoltage); Serial.println(F(" V"));
      Serial.print(F("Solar Panel Current:   ")); Serial.print(SolarPanelCurrent); Serial.println(F(" mA"));
      Serial.println("");

      Serial.print(F("Load Voltage:   ")); Serial.print(LoadVoltage); Serial.println(F(" V"));
      Serial.print(F("Load Current:   ")); Serial.print(LoadCurrent); Serial.println(F(" mA"));
      Serial.println("");

    }

    // write out the current protocol to message and send.
    int bufferLength;
    bufferLength = buildProtocolString();

    Serial.println(F("----------Sending packet----------"));

    /*
      /// turn radio on
      digitalWrite(ENABLE_RADIO, LOW);
      delay(100);
      SoftSerial.write((uint8_t *)byteBuffer, bufferLength);
    */
    // Send a message

    rf95.send(byteBuffer, bufferLength);
    Serial.println(F("----------After Sending packet----------"));
    if (!rf95.waitPacketSent(6000))
    {
      Serial.println(F("Timeout on transmission"));
      // re-initialize board
      if (!rf95.init())
      {
        Serial.println(F("init failed"));
        while (1);
      }

      // Defaults after init are 434.0MHz, 13dBm, Bw = 125 kHz, Cr = 4/5, Sf = 128chips/symbol, CRC on

      // The default transmitter power is 13dBm, using PA_BOOST.
      // If you are using RFM95/96/97/98 modules which uses the PA_BOOST transmitter pin, then
      // you can set transmitter powers from 5 to 23 dBm:
      //rf95.setTxPower(13, false);

      rf95.setFrequency(434.0);

      rf95.setModemConfig(RH_RF95::Bw31_25Cr48Sf512);
      // rf95.setModemConfig(RH_RF95::Bw125Cr48Sf4096);

      rf95.setTxPower(13);

      //rf95.printRegisters();
      Serial.println(F("----------Board Reinitialized----------"));


    }
    else
    {
      Serial.println(F("----------Packet Sent.  Sleeping Now----------"));
      rf95.sleep();
    }
    Serial.println(F("----------After Wait Sending packet----------"));
    delay(100);
    digitalWrite(LED, HIGH);
    delay(100);
    digitalWrite(LED, LOW);

    Serial.print(F("freeMemory()="));
    Serial.println(freeMemory());
    Serial.print(F("bufferlength="));
    Serial.println(bufferLength);

    for (int i = 0; i < bufferLength; i++) {
      Serial.print(" ");
      if (byteBuffer[i] < 16)
      {
        Serial.print(F("0"));
      }
      Serial.print(byteBuffer[i], HEX);           //  write buffer to hardware serial port
    }
    Serial.println();

    /*
      delay(100);
       digitalWrite(ENABLE_RADIO, HIGH);
       // turn radio off
    */

    MessageCount++;

    windClicks = 0;


  }





  if (DS3231_Present == false)
  {
    if (wakeState != REBOOT)
      wakeState = SLEEP_INTERRUPT;
    long timeBefore;
    long timeAfter;
    timeBefore = millis();
#if defined(TXDEBUG)
    Serial.print(F("timeBeforeSleep="));
    Serial.println(timeBefore);
#endif
    delay(100);
    // This is what we use for sleep if DS3231 is not present
    if (DS3231_Present == false)
    {
      //Sleepy::loseSomeTime(nextSleepLength);
      for (long i = 0; i < nextSleepLength / 16; ++i)
        Sleepy::loseSomeTime(16);

      wakeState = SLEEP_INTERRUPT;

#if defined(TXDEBUG)
      Serial.print(F("Awake now: "));
#endif
      timeAfter = millis();
#if defined(TXDEBUG)
      Serial.print(F("timeAfterSleep="));
      Serial.println(timeAfter);

      Serial.print(F("SleepTime = "));
      Serial.println(timeAfter - timeBefore);

      Serial.print(F("Millis Time: "));
#endif
      long time;
      time = millis();
#if defined(TXDEBUG)
      //prints time since program started
      Serial.println(time / 1000.0);
      Serial.print(F("2wakeState="));
      Serial.println(wakeState);
#endif
    }
  }

  if (DS3231_Present == true)
  {
    // use DS3231 Alarm to Wake up
#if defined(TXDEBUG)
    Serial.println(F("Using DS3231 to Wake Up"));
#endif
    delay(50);
    Sleepy::powerDown ();

    if (RTC.alarm(ALARM_1))
    {
      Serial.println(F("ALARM_1 Found"));
      wakeState = ALARM_INTERRUPT;
      Alarm_State_1 = true;
      // remove one rain click
      if (TotalRainClicks > 0)
        TotalRainClicks = TotalRainClicks - 1;

    }

    if (RTC.alarm(ALARM_2))
    {
      Serial.println(F("ALARM_2 Found"));
      wakeState = ALARM_INTERRUPT;
      Alarm_State_2 = true;
      // remove one rain click
      if (TotalRainClicks > 0)
        TotalRainClicks = TotalRainClicks - 1;

    }

  }

  // if it is an anemometer interrupt, do not process anemometer interrupts for 17 msec  debounce

  if (wakeState == ANEMOMETER_INTERRUPT)
  {
    ignore_anemometer_interrupt = true;
    delay(17);
    ignore_anemometer_interrupt = false;
  }

  // Pat the WatchDog
  ResetWatchdog();


}
