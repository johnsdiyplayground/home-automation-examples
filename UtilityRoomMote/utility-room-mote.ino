// Moteino utilityRoomMote
// By John 
// v1.0 November 24, 2015
//
// Uses a Moteino from http://www.lowpowerlab.com/ to communicate with a Pi Home Automation Gateway

/* PROJECT DESCRIPTION 
utilityRoomMote is Moteino based and controls a 12 VDC relay that is hooked to a motor that turns 
my natural gas water heater on and off. It also detects the clothes dryer running, and has 2 inputs 
for water leak detectors (outside the project case only requires 2 copper wires close together per 
sensor). The relay for the water heater can be remotely turned on/off using the Moteino Home Automation 
gateway. Status updates are sent to the gateway for the 2 water sensor inputs and dryer input.
*/ 

#include <RFM69.h>         //get it here: http://github.com/lowpowerlab/rfm69
#include <SPIFlash.h>      //get it here: http://github.com/lowpowerlab/spiflash
#include <WirelessHEX69.h> //get it here: https://github.com/LowPowerLab/WirelessProgramming
#include <SPI.h>           //comes with Arduino IDE (www.arduino.cc)

#define SERIAL_BAUD     115200
#define SERIAL_EN                //comment out if you don't want any serial output

//*****************************************************************************************************************************
// ADJUST THE SETTINGS BELOW DEPENDING ON YOUR HARDWARE/TRANSCEIVER SETTINGS/REQUIREMENTS
//*****************************************************************************************************************************
#define GATEWAYID   10    // this is the node ID of your gateway (which is probably tied to a Raspberry Pi)
#define NODEID      99   // must be unique to each sensor node
#define NETWORKID   151  // every node must match the same network ID to hear messages from each other and take actions directly
//#define FREQUENCY     RF69_433MHZ
//#define FREQUENCY     RF69_868MHZ
#define FREQUENCY       RF69_915MHZ //Match this with the version of your Moteino! (others: RF69_433MHZ, RF69_868MHZ)
#define ENCRYPTKEY      "YOUR-KEY-HERE" //has to be same 16 characters/bytes on all nodes, not more not less!
#define IS_RFM69HW      //uncomment only for RFM69HW! Leave out if you have RFM69W!
//*****************************************************************************************************************************

#ifdef SERIAL_EN
  #define DEBUG(input)   {Serial.print(input); delay(1);}
  #define DEBUGln(input) {Serial.println(input); delay(1);}
#else
  #define DEBUG(input);
  #define DEBUGln(input);
#endif

#define LEDLEAK           5   // LED output pin for leak detection, this will go high if either water sensor 1 or 2 detects water
#define LEDDRYER		      6	 	// LED output pin for dryer, this LED goes high if the clothes dryer is running
#define buzzerPin         3		// used for audio notifications of water leaks or end of the dryer run cycle
#define waterHeaterRelay  7
#define leakSensor1		  A2
#define leakSensor2		  A4
#define dryerSensor		  A0

const int numReadings = 5;   // how many dryer readings we will take before deciding if it's on or off
int reading = 0;            // current dryer reading for on/off calculation
int deltaReading = 0;       // calculated change compared to dryer being without power (off) 
int readingTotal = 0;		    // total reading differential

int leakSensor1value;
int leakSensor2value;

boolean leakStatus1 = false;        // the ongoing state to compare to the current status to see if it changed
boolean leakStatus2 = false; 
boolean currentLeakStatus1 = false; // value measured every time in the loop
boolean currentLeakStatus2 = false;

boolean currentDryerStatus = false; 
boolean dryerStatus = false;

boolean wHtrRelay = true;

char payload[] = "123 ABCDEFGHIJKLMNOPQRSTUVWXYZ";
char item[5] = "";

byte STATUS;

RFM69 radio;
SPIFlash flash(8, 0xEF30); //WINDBOND 4MBIT flash chip on CS pin D8 (default for Moteino)

void setup() {
  Serial.begin(115200);
  pinMode(leakSensor1, INPUT);
  pinMode(leakSensor2, INPUT);
  pinMode(dryerSensor, INPUT);
  pinMode(buzzerPin, OUTPUT);
  pinMode(waterHeaterRelay, OUTPUT);
  pinMode(LEDLEAK, OUTPUT);
  pinMode(LEDDRYER, OUTPUT);
  
  beep(50, 1);
  beep(50, 1);
  beep(50, 1);
  
  radio.initialize(FREQUENCY,NODEID,NETWORKID);
  #ifdef IS_RFM69HW
    radio.setHighPower(); //uncomment only for RFM69HW!
  #endif
  radio.encrypt(ENCRYPTKEY);

  char buff[20];
  sprintf(buff, "UtilRoomMote : %d Mhz...", FREQUENCY==RF69_433MHZ ? 433 : FREQUENCY==RF69_868MHZ ? 868 : 915);
  DEBUGln(buff);
  delay(5000); 
  
  waterHeaterRelayPower(1);  //we startup from power restore with the water heater relay set to on
}

void loop() {

 // check for messages from the home automation gateway or other Moteino nodes  
    if (radio.receiveDone()){
      DEBUG("Msg received from sender ID ");DEBUG('[');DEBUG(radio.SENDERID);DEBUG("] ");
      for (byte i = 0; i < radio.DATALEN; i++)
          DEBUG((char)radio.DATA[i]);
    DEBUGln();
  
   // wireless programming token check
    // DO NOT REMOVE, or this sensor node will not be wirelessly programmable any more!
    CheckForWirelessHEX(radio, flash, true);

    //first send any ACK to request
    //DEBUG("   [RX_RSSI:");DEBUG(radio.RSSI);DEBUG("]");

    if (radio.ACKRequested())
    {
      radio.sendACK();
      DEBUGln("ACK sent.");
    }
  
      if (radio.DATALEN==3){
          //check for an web page request to turn off the water heater relay
          if (radio.DATA[0]=='0' && radio.DATA[1]=='W' && radio.DATA[2]=='R'){
            waterHeaterRelayPower(0);
            wHtrRelay = false;
            transmitStatus(9904, wHtrRelay);
          }
          else if (radio.DATA[0]=='1' && radio.DATA[1]=='W' && radio.DATA[2]=='R'){
       	    waterHeaterRelayPower(1);
       	    wHtrRelay = true;
       	    transmitStatus(9904, wHtrRelay);
          }
      }
    }

  leakSensor1value = analogRead(leakSensor1);
  leakSensor2value = analogRead(leakSensor2);
  
  // Setup the current values of the leak sensors so they can be compared to the stored state
  if (leakSensor1value > 850) currentLeakStatus1 = false;
  	else currentLeakStatus1 = true;
  if (leakSensor2value > 850) currentLeakStatus2 = false;
  	else currentLeakStatus2 = true;

  if (leakSensor1value < 850 && (currentLeakStatus1 != leakStatus1)){
  	 digitalWrite(LEDLEAK, HIGH);
   	 leakStatus1 = true;
     transmitStatus(9901, leakStatus1);  // we created '9901' to mean its water leak sensor 1
   }
   if (leakSensor1value > 850 && (currentLeakStatus1 != leakStatus1)){
     digitalWrite(LEDLEAK, LOW);
  	 leakStatus1 = false;
     transmitStatus(9901, leakStatus1);
   }
   
  if (leakSensor2value < 850 && (currentLeakStatus2 != leakStatus2)){
  	 digitalWrite(LEDLEAK, HIGH);
   	 leakStatus2 = true;
     transmitStatus(9902, leakStatus2);  // we created '9902' to mean its water leak sensor 2
   }
   if (leakSensor2value > 850 && (currentLeakStatus2 != leakStatus2)){
     digitalWrite(LEDLEAK, LOW);
  	 leakStatus2 = false;
     transmitStatus(9902, leakStatus2);
   }
  
  if (leakStatus1 == true || leakStatus2 == true){  // flash LED and beeper in case of a water leak detected
    digitalWrite(LEDLEAK,LOW);
    delay(200);
    digitalWrite(LEDLEAK,HIGH);
    delay(150);
    beep(500,1);
  }

// now we need to see if the dryer is running or not using a current clamp mounted inside the dryer
// The sensor can only move +/- 1 volt peak to peak so when the dryer is running the values do not change much.
// And because it is AC voltage, we need to take multiple readings to see if we have a running dryer.
// When the dryer is not running, the analog input will hover right around 510 or 511.

// first gather up a few readings and add the deltas from 511 together
for (int n=0; n < numReadings; n++){
	reading = analogRead(dryerSensor);
	// DEBUG("Sensor reading: ");DEBUG(reading);DEBUGln();
	deltaReading = (511 - reading);
	deltaReading = abs(deltaReading);
	readingTotal = (readingTotal + deltaReading);
}
	DEBUG("readingTotal = ");DEBUG(readingTotal);DEBUGln();
	
// now see if the dryer is on or off based on a test and take appropriate action
if (readingTotal > 8){
	currentDryerStatus = true;
	if (currentDryerStatus != dryerStatus){
		dryerStatus = true;
		digitalWrite(LEDDRYER, HIGH);
		transmitStatus(9903, dryerStatus);  // we assigned '9903' to mean dryer sensor 
	}
 }
 else if (readingTotal <= 8) {
	currentDryerStatus = false;
	if (currentDryerStatus != dryerStatus){
		dryerStatus = false;
		digitalWrite(LEDDRYER, LOW);  			// Turn the LED off
		beep(3500, 3);							// Beep long, 3 times 
		delay(500);
		beep(3500, 3);
		transmitStatus(9903, dryerStatus);
	}
 } 
readingTotal = 0;  // reset the total for the next time through the loop

}

void transmitStatus(int item, int status){  
    delay(50);
    sprintf(payload, "%d:%d", item, status);
    byte buffLen=strlen(payload);
    radio.sendWithRetry(GATEWAYID, payload, buffLen);
}

void beep(unsigned char delayPeriod, unsigned char beepNumber){
  for (int b=0; b < beepNumber; b++){
    for (int i=0; i < delayPeriod; i++){
      digitalWrite(buzzerPin, HIGH);      // Almost any value can be used except 0 and 255
                                          // experiment to get the best tone
      delayMicroseconds(150 + delayPeriod);          // wait for a delayms ms - 192 was orig value
      digitalWrite(buzzerPin, LOW);       // 0 turns it off
      delayMicroseconds(150 + delayPeriod);          // wait for a delayms ms   
    }
  delay(100);
  }
}  

void waterHeaterRelayPower(int onoff){
  if (onoff == 0) {
    digitalWrite(waterHeaterRelay, LOW);
    beep (700, 2);  // make a noise
  }
  else {
    digitalWrite(waterHeaterRelay, HIGH);
    beep (700, 2);  // make a noise
  }
}
