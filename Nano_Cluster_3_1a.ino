/*Clock with Animated Subaru 360 sedans and FHI logo
RTC clock module, Arduino Pro-Mini, and SPI OLED 128x64 required. (I2C would work well too but the background animation may be sluggish?)
AFR signal via LC1 wideband controller
Connect VIA the following pin assigments.
Batt Voltage: A0 --> requires voltage divider circuit:  (+)---[100k]-A0-[10k]---(-)
RPM: A3 --> requires opto isolation from ignition circuit
Temp:A2 --> Still looking for a suitable sensor right now there are no calcs being done on the pin
AFR: A1 --> Uses wideband output from LC1 controller
Mode Select Button: D12 (take pin low to change mode)
*/
#include <SPI.h>  // library used for OLED communications interface
#include <Wire.h> // basic wire library 
#include <Adafruit_GFX.h>   // OLED graphics support library
#include <Adafruit_SSD1306.h>  // OLED  funtion library
#include "bitmaps.h" //graphics data is in this header file
#include <Time.h>    // library used for clock
#include <DS1307RTC.h> //  library to interface with DS1307 RTC module
#include <PinChangeInt.h> //allows int on alternate pins (D2 is an INT pin but is currently used for OLED VCC)

#if (SSD1306_LCDHEIGHT != 64)
#error("Height incorrect, please fix Adafruit_SSD1306.h!");
#endif


#define OLED_MOSI  4   //marked SDA on cheapo OLED
#define OLED_CLK   3   //marked SCL on cheapo OLED
#define OLED_DC    6   //marked DC on cheapo OLED
#define OLED_CS    8   //NA on cheapo OLED
#define OLED_RESET 5   //marked RST on cheapo OLED
#define OLEDVCC  2     //marked VCC on cheapo OLED
Adafruit_SSD1306 display(OLED_MOSI, OLED_CLK, OLED_DC, OLED_RESET, OLED_CS);

int timexpos = 0;  // used later on for clock digit alignment

// Readings smoothing
const int numReadings = 10;     // number of reading samples to take before averaging the read
int readings[numReadings];      // the readings from the analog input
int index = 0;                  // the index of the current reading
int total = 0;                  // the running total
int average = 0;     




//RPM event inturrupt counting stuff 
#define RPMPIN A3  //Inturrupt PIN used to count RPM events
volatile byte rpmcount;
unsigned int rpm;
unsigned long timeold;
//-------------------

//Mode button config
const int buttonPin = 12;    // mode button on D12
int buttonPushCounter = 0;   // counter for the number of button presses
int buttonState = 0;         // current state of the button
int lastButtonState = 0;     // previous state of the button
int mode = 0;                // current mode
//----

//AFR input pin config
int AFRInput = 1; // Assigned to A1
int afrVal = 0;
//-----------

// Voltage input config
int voltInput = 0;    // Assigned to A0 
float vout = 0.0;     // zero everything to start off
float vin = 0.0;      // zero everything to start off
float R1 = 100000.0;  // resistance of R1 (100K) -see text!
float R2 = 10000.0;   // resistance of R2 (10K) - see text!
int vValue = 0;       // zero everything to start off
int vadjv = 0;        // (adjustment to correct sample voltage deviation (displayed voltage vs actual measured voltage)
int vadj =  0;       // 1 = + | 0 = - (does the value need to be increased or reduced)

//----------
// Temp input config
int tempInput = 2;    //A2 is reserved for temperature readings Right now there aer no calcs being done on the input just raw port data
int tempReading;      // the analog reading from the sensor



//

//background flying things config
#define N_FLYERS   4 // Number of flying things
struct Flyer {       // Array of flying things
  int16_t x, y;      // Top-left position * 16 (for subpixel pos updates)
  int8_t  depth;     // Stacking order is also speed, 12-24 subpixels/frame
  uint8_t frame;     // Animation frame; cars cycle 0-3, Logo=255
} flyer[N_FLYERS];

void setup() {

// initialize reading smoothing. used by Temp and AFTR
for (int thisReading = 0; thisReading < numReadings; thisReading++)
    readings[thisReading] = 0; 
//--------    
  
  
//RPM 
pinMode(RPMPIN, INPUT_PULLUP);  // Configure the pin as an input, and turn on the pullup resistor.
  attachPinChangeInterrupt(RPMPIN, rpm_fun, FALLING);
  
   rpmcount = 0;
   rpm = 0;
   timeold = 0;
//------------


  randomSeed(analogRead(6));           // Seed random from unused analog input
  pinMode (OLEDVCC, OUTPUT);           // Set one pin high to provide VCC to OLED
  digitalWrite(OLEDVCC, HIGH);         // This allows direct piggyback connection to NANO
  digitalWrite(buttonPin,HIGH);        // Set (buttonPin) HIGH
  pinMode(buttonPin, INPUT_PULLUP);    // initialize the button pin as a input:
  pinMode(voltInput, INPUT);           // configure Batt voltage level input
  pinMode(AFRInput, INPUT);            // configure AFR signal input
  pinMode(tempInput, INPUT);           // configure Temp signal input
  digitalWrite(tempInput,HIGH);
  Serial.begin(9600);                  // Start serial protocol

  display.begin(SSD1306_SWITCHCAPVCC); // Init screen
  display.clearDisplay();
  
  //Some flying things math. This also keeps the positions available when flipping through screens
    for(uint8_t i=0; i<N_FLYERS; i++) {  // Randomize initial flyer states
    flyer[i].x     = (-32 + random(160)) * 16;
    flyer[i].y     = (-32 + random( 96)) * 16;
    flyer[i].frame = random(3) ? random(4) : 255; // 66% cars, else logo
    flyer[i].depth = 10 + random(16);             // Speed / stacking order
 }
  qsort(flyer, N_FLYERS, sizeof(struct Flyer), compare); // Sort depths
}



void loop() {
  
display.display();          // Update screen to show current positions
display.clearDisplay();     // Start drawing next frame
modeSelect();            // Which mode do we want to display
overlaySelect();        // Call the functions to the screen

}

 void showFlyers() { 
  uint8_t i, f;
  int16_t x, y;
 
  boolean resort = false;     // By default, don't re-sort depths
    

  for(i=0; i<N_FLYERS; i++) { // For each flyer...

    // First draw each item...
  f = (flyer[i].frame == 255) ? 4 : (flyer[i].frame++ & 2); // Frame #
     
    x = flyer[i].x / 16;
    y = flyer[i].y / 16;
    display.drawBitmap(x, y, (const uint8_t *)pgm_read_word(&mask[f]), 32, 32, BLACK);
    display.drawBitmap(x, y, (const uint8_t *)pgm_read_word(&img[f]), 32, 32, WHITE);

    // Then update position, checking if item moved off screen...
    flyer[i].x -= flyer[i].depth * 2; // Update position based on depth,
    flyer[i].y += flyer[i].depth;     // for a sort of pseudo-parallax effect.
    if((flyer[i].y >= (64*16)) || (flyer[i].x <= (-32*16))) { // Off screen?
      if(random(7) < 5) {         // Pick random edge; 0-4 = top
        flyer[i].x = random(160) * 16;
        flyer[i].y = -32         * 16;
      } else {                    // 5-6 = right
        flyer[i].x = 128         * 16;
        flyer[i].y = random(64)  * 16;
      }
      flyer[i].frame = random(3) ? random(4) : 255; // random flyer selection 
      flyer[i].depth = 10 + random(16);
      resort = true;
    }
  }
  // If any items were 'rebooted' to new position, re-sort all depths
  if(resort) qsort(flyer, N_FLYERS, sizeof(struct Flyer), compare);


}

// Flyer depth comparison function for qsort()
static int compare(const void *a, const void *b) {
  return ((struct Flyer *)a)->depth - ((struct Flyer *)b)->depth;
}


//which function will be displayed
 void overlaySelect() { 
 
 /* if  (mode == 0) {
     digitalClockDisplay();
    }
   */ 
  if  (mode == 0) {
      credits();
    }
    else if (mode == 1)
   DS1307RTC_Display();        
    else if (mode == 2)
      RPM();        
    else if  (mode == 3) 
      AFR_Display();
    else if  (mode == 4) 
      temp();
    else if  (mode == 5) 
      volt();
     else if (mode == 6)
      ClockFace(); 
   }
 
 
  
 void modeSelect (){
   buttonState = digitalRead(buttonPin);    // read the pushbutton input pin:
  if (buttonState != lastButtonState)   // compare the buttonState to its previous state
  {
    if (buttonState == LOW)  // if the state has changed, increment the counter
    {
      // if the current state is LOW then the button
      // wend from off to on:
      buttonPushCounter++;
      
  }
  lastButtonState = buttonState;
  
  if (buttonPushCounter == 0) {
      mode = 0;
  } 
   if  (buttonPushCounter == 1){
   mode = 1;
   }
  if  (buttonPushCounter == 2){
   mode = 2;
  }
   if  (buttonPushCounter == 3){
   mode = 3;
   }
   if  (buttonPushCounter == 4){
   mode = 4;
   }
   if  (buttonPushCounter == 5){
   mode = 5;
   }
  if (buttonPushCounter >= 6){
 mode = 6;
  }
    if (buttonPushCounter >= 7){
  buttonPushCounter = 0;
 
  }
 }
 } 
 
void DS1307RTC_Display(){ //Digital clock display from DS1307RTC module
  
 showFlyers();  // We like floating cars and logos behind the clock
 tmElements_t tm;
  int modifiedHour = 0;
 RTC.read(tm);
 
  if ((tm.Hour)<10 || (tm.Hour) >= 13)  //used to shift the hour numbers under 10 over to try to keep them centered 
 {timexpos=30;
 }else{
   timexpos = 20; }
  
  display.setTextSize(3);
  display.setTextColor(WHITE,BLACK);
display.setCursor(timexpos,30);

 

  if(tm.Hour == 0){display.print("12");} //Convert zero hour for 12-hour display
    else if(tm.Hour < 13 && tm.Hour >= 10){display.print(tm.Hour);} //Just display hour if double digit hour
        else if(tm.Hour < 10){display.print(tm.Hour);} //If single digit hour
        else if(tm.Hour >= 13 && tm.Hour >= 22){display.print(tm.Hour-12);} //If double digit and PM, convert 24 to 12 hour
        else{display.print(tm.Hour-12);} //If single digit and PM, convert to 12 hour
        
        display.print(":"); //Display hour-minute separator
        if(tm.Minute<10){display.print("0");} //Add leading zero if single-digit minute
        display.print(tm.Minute); //Display retrieved minutes
/*   
        display.setTextSize(1);
          if(tm.Hour < 12){display.print(" AM");} //Display AM indicator, as needed
        else
         {display.print(" PM");} //Display PM indicator, as needed
*/



// Digital clock display of the day
  display.setTextSize(1);
  display.setTextColor(WHITE,BLACK);
  display.setCursor(35,55);
  display.print(tm.Month);
  display.print("/");
  display.print(tm.Day);
  display.print("/");
  display.print(tmYearToCalendar(tm.Year));
  
 }


void printDigits(int digits) {
  display.print(":");
  if(digits < 10)
    display.print('0');
  display.print(digits);
}

void ClockFace() {

 /* f = (flyer[i].frame == 255) ? 4 : (flyer[i].frame++ & 3); // Frame #
    x = flyer[i].x / 16;
    y = flyer[i].y / 16;
    display.drawBitmap(x, y, (const uint8_t *)pgm_read_word(&mask[f]), 32, 32, BLACK);
    display.drawBitmap(x, y, (const uint8_t *)pgm_read_word(&img[f]), 32, 32, WHITE);
*/

  tmElements_t tm;
  RTC.read(tm);
  // Now draw the clock face
    
     display.drawCircle(display.width()/2, display.height()/2 , 32, WHITE); //Draw and position clock outer circle
    
    //display.drawCircle(display.width()/2, display.height()/2 + 8, 20, WHITE); //Draw and position clock outer circle
    //display.fillCircle(display.width()/2+25, display.height()/2 + 8, 20, WHITE); //Fill circle only if displaying inverted colors
    //display.drawRect(41,17,47,47,WHITE); //Draw box around clock
 display.drawRect(32,0,65,64,WHITE); //Draw box around clock

    //Position and draw hour tick marks
    for( int z=0; z < 360;z= z + 30 ){
    //Begin at 0° and stop at 360°
      float angle = z ;
      angle=(angle/57.29577951) ; //Convert degrees to radians
    
      int x2=(64+(sin(angle)*30));
      int y2=(32-(cos(angle)*30));
      int x3=(64+(sin(angle)*(30-5)));
      int y3=(32-(cos(angle)*(30-5)));
      
      display.drawLine(x2,y2,x3,y3,WHITE);
    }
  
    //Position and display second hand
    float angle = (tm.Second) * 6 ; //Retrieve stored seconds and apply
    angle=(angle/57.29577951) ; //Convert degrees to radians  
    int x3=(64+(sin(angle)*(30)));
    int y3=(32-(cos(angle)*(30)));
    display.drawLine(64,32,x3,y3,WHITE);

   //Position and display minute hand
    angle = tm.Minute * 6; //Retrieve stored minutes and apply
    angle=(angle/57.29577951) ; //Convert degrees to radians  
    x3=(64+(sin(angle)*(30-3)));
    y3=(32-(cos(angle)*(30-3)));
    display.drawLine(64,32,x3,y3,WHITE);

    //Position and display hour hand
    angle = tm.Hour * 30 + int((tm.Minute / 12) * 6); //Retrieve stored hour and minutes and apply
    angle=(angle/57.29577951) ; //Convert degrees to radians  
    x3=(64+(sin(angle)*(30-11)));
    y3=(32-(cos(angle)*(30-11)));
    display.drawLine(64,32,x3,y3,WHITE);
  }


 
 void RPM(){
   
   delay(100);    //Update RPM every 100 milliseconds
   detachPinChangeInterrupt(RPMPIN);   //Don't process interrupts during calculations
  int barval;
   //Note that this would be normally be 60*1000/(millis() - timeold)*rpmcount if the interrupt
   //happened once per revolution instead of twice like in a 360 dizzy . 
   //Other multiples could be used for a different number of events.
  rpm = 30*1000/(millis() - timeold)*rpmcount;
  
   timeold = millis();
   rpmcount = 0;
   barval = rpm /70;

   display.setTextSize(1);
    display.setCursor(100,32);
    display.print("RPM ");
       display.setCursor(15,10);
       display.setTextSize(3);
    if (rpm >6000){
         display.setTextColor(BLACK,WHITE);  // Over-Rev ALERT by inverting colors on tach display
       }else{   
         display.setTextColor(WHITE,BLACK);
    }
       display.print(rpm);
     
  
  display.drawLine(10, 60, 10, 62, WHITE);
  display.drawLine(95, 60, 95, 62, WHITE);
  display.drawLine(117, 60, 117, 62, WHITE);
  display.drawRect(10, 50, 108, 10, WHITE); //Border of the bar chart
  display.fillRect(12, 52, barval, 6, WHITE); //Draws the bar depending on the sensor value


   //Restart the interrupt processing
   attachPinChangeInterrupt(RPMPIN, rpm_fun, FALLING);
 }
 
 
 void AFR_Display(){
  int barval; 

  total= total - readings[index];   // subtract the last reading:     
  readings[index] = analogRead(AFRInput);   // read from the sensor:  
  total= total + readings[index];      // add the reading to the total: 
  index = index + 1;          // advance to the next position in the array:  

  if (index >= numReadings)        // if we're at the end of the array...
    index = 0;                    // ...wrap around to the beginning:
  average = total / numReadings; // calculate the average:

  float afrVal = analogRead(average);  //Read input from LC1 Wideband voltage with smoothing   
//  float afrVal = analogRead(AFRInput); //Read input from LC1 Wideband voltage (No smoothing)
  float afroutput = (afrVal-0)*(22-8)/(1023-0) +8;   // convert Air Fuel voltage from 0-5v to 8-22 AFR  couldn't use this: map(afrValue,0,1023,8,22)  because it does not support decimal values so it's done a little different
 //float afroutput = 14.7;// test var

       afrVal = analogRead(AFRInput);
       display.setCursor(15,10);
       display.setTextSize(3);
       display.setTextColor(WHITE,BLACK);
       display.print(afroutput);
     
       
       
   
    display.setTextSize(1);
    display.setCursor(100,32);
    display.print("AFR");
       
  barval = (afroutput) * 3.54;
   display.drawLine(10, 60, 10, 62, WHITE);
   
    display.drawLine(64, 60, 64, 62, WHITE); //limit line

    display.drawLine(117, 60, 117, 62, WHITE);
    display.drawRect(10, 50, 108, 10, WHITE); //Border of the bar chart
    display.fillRect(12, 52, barval, 6, WHITE); //Draws the bar depending on the sensor value
delay(100);
     } 
 
 
void temp(){
 int barval;
   // subtract the last reading:
  total= total - readings[index];  
  //-----------
  
  tempReading = analogRead(tempInput);

// read from the sensor:  
  readings[index] = (tempReading); 
  // add the reading to the total:
  total= total + readings[index];
  // advance to the next position in the array:  
  index = index + 1; 
// if we're at the end of the array...
  if (index >= numReadings)              
    // ...wrap around to the beginning:
    index = 0; 
   // calculate the average:
  average = total / numReadings;  
//--------


float voltage = average * 5.0;  // converting that reading to voltage with smoothing
//float voltage = tempReading * 5.0;  // converting that reading to voltage
voltage /= 1024.0;

float temperatureC = (voltage - 0.5) * 100 ; // converting from 10 mv per degree with 500 mV offset
                                         // to degrees ((volatge - 500mV) times 100)
float temperatureF = (temperatureC * 9.0 / 5.0) + 32.0;  // now convert to Fahrenheit
   

       display.setCursor(15,10);
       display.setTextSize(3);
       display.setTextColor(WHITE,BLACK);
       display.print(temperatureF);
       display.setTextSize(1);
       display.setCursor(100,32);
       display.print("TEMP");
       
  barval = (temperatureC)- 9;
  
   display.drawLine(10, 60, 10, 62, WHITE);
   
    display.drawLine(64, 60, 64, 62, WHITE); //limit line

    display.drawLine(117, 60, 117, 62, WHITE);
    display.drawRect(10, 50, 108, 10, WHITE); //Border of the bar chart
    display.fillRect(12, 52, barval, 6, WHITE); //Draws the bar depending on the sensor value



delay(1); 

}



void volt(){
 
  
  /*this section is for battery voltage monitoring
  two resistors are reguired to as a voltage divider to protect the processor
  (+)---{100k]----Pin A0-----[10k]-----(-)
  With the values used in the voltage divider it is possible to feed voltage from 
  0V to 55V into the Arduino board. The junction on the voltage divider network connected 
  to the the Arduino analog pin is equivalent to the input voltage divided by 11, so 55V ÷ 11 = 5V. 
  In other words, when measuring 55V, the Arduino analog pin will be at its maximum voltage of 5V. 
  So, in practice, it is better to label this “0-30V” to add a safety margin!
  */
  int barval;
   vValue = analogRead(voltInput); // read the value at A0
   vout = (vValue * 5.0) / 1024.0;  // see text above
      vin = vout / (R2/(R1+R2)); 
   if (vin<0.09) 
   vin=0.0;//statement to quash undesired reading !
   
       display.setCursor(15,10);
       display.setTextSize(3);
       
       if (vin <12.25){
              display.setTextColor(BLACK,WHITE);
       }else{
       display.setTextColor(WHITE,BLACK);
       }
     //  display.print(vin,vadj,vadjv);
     if (vadj == 0){
     display.print((vin) - (vadjv));
     }
        if (vadj == 1){     
         display.print((vin) + (vadjv));
        }
                 
        display.setTextSize(1);
    display.setCursor(100,32);
    display.print("VOLT");
       
  barval = (vin)*4.2;
  
   display.drawLine(10, 60, 10, 62, WHITE);
   
    display.drawLine(64, 60, 64, 62, WHITE); //limit line

    display.drawLine(117, 60, 117, 62, WHITE);
    display.drawRect(10, 50, 108, 10, WHITE); //Border of the bar chart
    display.fillRect(12, 52, barval, 6, WHITE); //Draws the bar depending on the sensor value
       
       
       
       delay(100);
    
 }

void credits(){
  //showFlyers();
    display.drawBitmap(0, 0, (const uint8_t *)pgm_read_word(&img[5]), 128, 22, WHITE);

  display.drawBitmap(47, 25, (const uint8_t *)pgm_read_word(&img[6]), 66, 20, WHITE);
  display.drawBitmap(15, 19, (const uint8_t *)pgm_read_word(&img[4]), 32, 32, WHITE);
 
 
 display.setTextSize(1);
 display.setTextColor(WHITE);
 display.setCursor(27,45);
 display.print("nano-cluster");
 display.setCursor(30,55);
 display.print("Sigmaz 2015");
 }

void rpm_fun() {
  rpmcount++;
}
