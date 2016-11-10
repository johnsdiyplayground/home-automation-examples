// IMPORTANT: adjust the settings in the configuration section below !!!
// **********************************************************************************
// Copyright Felix Rusu of LowPowerLab.com, 2016
// RFM69 library and sample code by Felix Rusu - lowpowerlab.com/contact
//
// Code modified by John Wills, November 2016 
// This code should only be used with Weather Shield R2 from lowpowerlab.com
// **********************************************************************************
// License
// **********************************************************************************
// This program is free software; you can redistribute it 
// and/or modify it under the terms of the GNU General    
// Public License as published by the Free Software       
// Foundation; either version 3 of the License, or        
// (at your option) any later version.                    
//                                                        
// This program is distributed in the hope that it will   
// be useful, but WITHOUT ANY WARRANTY; without even the  
// implied warranty of MERCHANTABILITY or FITNESS FOR A   
// PARTICULAR PURPOSE. See the GNU General Public        
// License for more details.                              
//                                                        
// You should have received a copy of the GNU General    
// Public License along with this program.
// If not, see <http://www.gnu.org/licenses/>.
//                                                        
// Licence can be viewed at                               
// http://www.gnu.org/licenses/gpl-3.0.txt
//
// Please maintain this license information along with authorship
// and copyright notices in any redistribution of this code
// **********************************************************************************
#include <RFM69.h>    //get it here: https://www.github.com/lowpowerlab/rfm69
#include <RFM69_ATC.h>//get it here: https://www.github.com/lowpowerlab/rfm69
#include <SPI.h>      //comes with Arduino IDE (www.arduino.cc)
#include <LowPower.h> //get library from: https://github.com/lowpowerlab/lowpower
                      //writeup here: http://www.rocketscream.com/blog/2011/07/04/lightweight-low-power-arduino-library/
#include <SPIFlash.h> //get it here: https://www.github.com/lowpowerlab/spiflash
#include <SparkFunBME280.h> //get it here: https://github.com/sparkfun/SparkFun_BME280_Breakout_Board/tree/master/Libraries/Arduino/src
#include <Wire.h>     //comes with Arduino

//*********************************************************************************************
//************ IMPORTANT SETTINGS - YOU MUST CHANGE/CONFIGURE TO FIT YOUR HARDWARE *************
//*********************************************************************************************
#define NODEID        19    //unique for each node on same network
#define NETWORKID     101  //the same on all nodes that talk to each other
#define GATEWAYID     1
//Match frequency to the hardware version of the radio on your Moteino (uncomment one of the three):
//#define FREQUENCY     RF69_433MHZ
//#define FREQUENCY     RF69_868MHZ
#define FREQUENCY     RF69_915MHZ
#define IS_RFM69HW    //uncomment only for RFM69HW! Remove/comment if you have RFM69W!
#define ENCRYPTKEY    "1111111111111111" //exactly the same 16 characters/bytes on all nodes!
#define ENABLE_ATC    //comment out this line to disable AUTO TRANSMISSION CONTROL
#define ATC_RSSI      -75
//*********************************************************************************************
#define ACK_TIME      30  // max # of ms to wait for an ack
#define BATT_MONITOR  A7  // Sense VBAT_COND signal (when powered externally should read ~3.25v/3.3v (1000-1023), when external power is cutoff it should start reading around 2.85v/3.3v * 1023 ~= 883 (ratio given by 10k+4.7K divider from VBAT_COND = 1.47 multiplier)
#define BATT_FORMULA(reading) reading * 0.00322 * 1.49 // >>> fine tune this parameter to match your voltage when fully charged
                                                       // details on how this works: https://lowpowerlab.com/forum/index.php/topic,1206.0.html

#define SERIAL_EN             //comment this out when deploying to an installed Mote to save a few KB of sketch size
#define SERIAL_BAUD    115200
#ifdef SERIAL_EN
  #define DEBUG(input)   {Serial.print(input); delay(1);}
  #define DEBUGln(input) {Serial.println(input); delay(1);}
  #define DEBUGFlush() { Serial.flush(); }
#else
  #define DEBUG(input);
  #define DEBUGln(input);
  #define DEBUGFlush();
#endif

#ifdef ENABLE_ATC
  RFM69_ATC radio;
#else
  RFM69 radio;
#endif

#define FLASH_SS      8 // and FLASH SS on D8 on regular Moteinos (D23 on MoteinoMEGA)
SPIFlash flash(FLASH_SS, 0xEF30); //EF30 for 4mbit  Windbond chip (W25X40CL)

BME280 bme280;  

float batteryVolts = 5;
char BATstr[10]; //longest battery voltage reading message = 9chars

char sendBuf[32];
byte sendLen;

float temperature=0;
char Fstr[10];
float humidity=0;
char Hstr[10];
float pressure=0;
char Pstr[10];

uint16_t batteryReportCycles=0;

void setup() {
  Serial.begin(SERIAL_BAUD);
  radio.initialize(FREQUENCY,NODEID,NETWORKID);
  #ifdef IS_RFM69HW
    radio.setHighPower(); //uncomment only for RFM69HW!
  #endif
  radio.encrypt(ENCRYPTKEY);

  //Auto Transmission Control - dials down transmit power to save battery (-100 is the noise floor, -90 is still pretty good)
  //For indoor nodes that are pretty static and at pretty stable temperatures (like a MotionMote) -90dBm is quite safe
  //For more variable nodes that can expect to move or experience larger temp drifts a lower margin like -70 to -80 would probably be better
  //Always test your ATC mote in the edge cases in your own environment to ensure ATC will perform as you expect
  #ifdef ENABLE_ATC
    radio.enableAutoPower(ATC_RSSI);
  #endif
  
  char buff[50];
  sprintf(buff, "\nTransmitting at %d Mhz...", FREQUENCY==RF69_433MHZ ? 433 : FREQUENCY==RF69_868MHZ ? 868 : 915);
  DEBUGln(buff);
  radio.sendWithRetry(GATEWAYID, "START", 5);
  
  #ifdef ENABLE_ATC
    DEBUGln("RFM69_ATC Enabled (Auto Transmission Control)\n");
  #endif

  if (flash.initialize()) flash.sleep(); //if Moteino has FLASH-MEM, make sure it sleeps

  bme280.settings.commInterface = I2C_MODE;
  bme280.settings.I2CAddress = 0x77;
  bme280.settings.runMode = 3; //Normal mode
  bme280.settings.tStandby = 0;
  bme280.settings.filter = 0;
  bme280.settings.tempOverSample = 1;
  bme280.settings.pressOverSample = 1;
  bme280.settings.humidOverSample = 1;

}  // END OF THE SETUP SECTION OF CODE
// *****************************************************

void loop() {
    //read BME sensor
    bme280.begin();
    dtostrf(bme280.readTempF(), 3,1, Fstr);
    dtostrf(bme280.readFloatHumidity(), 2,0, Hstr);
    float p = bme280.readFloatPressure();
    p = p / 3311.8352;  // this is conversion from Pascals to inches of mercury, adjusted for my location above sea level 
    dtostrf(p, 2,2, Pstr);    
    bme280.writeRegister(BME280_CTRL_MEAS_REG, 0x00); //sleep the BME280

    // we only will report battery voltage every "x" cycles, set the number of cycles in the line below 
    if (batteryReportCycles > 3)  // change this number to a higher number to save even more battery life  
    {
      DEBUGln("Inside the checkBattery routine to report battery voltage");
      int readings = 0; 
      for (byte i=0; i<10; i++) //take 10 samples, and average
        readings+=analogRead(BATT_MONITOR);
      batteryVolts = BATT_FORMULA(readings / 10.0);
      dtostrf(batteryVolts, 3,2, BATstr); //update the BATStr which gets sent every batteryReportCycles
      sprintf(sendBuf, "V:%s F:%s H:%s P:%s", BATstr, Fstr, Hstr, Pstr);  // transmit battery voltage and all weather data
      sendLen = strlen(sendBuf);
      radio.sendWithRetry(GATEWAYID, sendBuf, sendLen);
      batteryReportCycles=0;
    }
    else
    {
      sprintf(sendBuf, "F:%s H:%s P:%s", Fstr, Hstr, Pstr);  // we skip transmitting battery voltage to save battery life 
      sendLen = strlen(sendBuf);
      radio.sendWithRetry(GATEWAYID, sendBuf, sendLen);  
    }
 
    DEBUGFlush();
    radio.sleep();

    // the value of this loop will set a sleep delay an power the unit down
    // the lengh of the delay is 8 seconds * the number of cycles
    // so for example a value of 100 means 100 * 8 = 800 seconds sleeping, 
    // or about 13.33 minutes between read and transmitting weather data
    // NOTE:  The maximum value for the loop size is 255
    for (int i = 0; i < 4; i++)  
       LowPower.powerDown(SLEEP_8S, ADC_OFF, BOD_OFF);
    DEBUGln("SLEEP_8S for loop completed");

    batteryReportCycles++;
    DEBUG("Battery Report cycles is ");DEBUGln(batteryReportCycles);

} // ****END OF THE LOOP SECTION OF CODE ****
