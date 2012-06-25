//'Finished' as of 25/06/2012 at 19:57.
//Christopher and Russell Steicke



#include <LiquidCrystal.h>
#include <Wire.h>

#define RTCADDR 0x51

// Pins in use
#define BUTTON_ADC_PIN           A0  // A0 is the button ADC input
#define LCD_BACKLIGHT_PIN         3  // D3 controls LCD backlight
// ADC readings expected for the 5 buttons on the ADC input
#define UP_10BIT_ADC            491  // up
#define DOWN_10BIT_ADC          860  // down
#define SELECT_10BIT_ADC          0  // select
#define BUTTONHYSTERESIS         20  // hysteresis for valid button sensing window
//return values for readButtons()
#define BUTTON_NONE               0  //
#define BUTTON_RIGHT              1  //
#define BUTTON_UP                 2  //
#define BUTTON_DOWN               3  //
#define BUTTON_LEFT               4  //
#define BUTTON_SELECT             5  //

byte button;
boolean doExit = false;
#define lev1                      0
#define lev2                     10
#define lev3                     70
#define lev4                    175
#define lev5                    255 //the brightness levels, only 5 have been coded in
uint8_t bness=lev3;                   //the brightness variable
long decTime;



LiquidCrystal lcd( 8, 9, 4, 5, 6, 7 );


void setup()
{
  
  //button adc input
  pinMode( BUTTON_ADC_PIN, INPUT );         //ensure A0 is an input
  digitalWrite( BUTTON_ADC_PIN, LOW );      //ensure pullup is off on A0

  lcd.begin(16, 2);
  lcd.print("   Clock v10!");
  delay(2000);
  lcd.clear();
  lcd.print(".");
  Wire.begin(); // no address = master mode
  if (rtcIsValid()) {
    lcd.print("+");
    delay(200);
  } else {
    lcd.print("-");
    writeToRTC();
    delay(500);
  }
  decTime = readRTCinDecimal();
  // Activate the pullup on D2.
  digitalWrite(2, 1);
  pinMode(12, OUTPUT);
  attachInterrupt(0, timerInterrupt, FALLING);
  analogWrite( LCD_BACKLIGHT_PIN , bness );
}


static uint8_t rtc_init_data[] = {
  0x00, // begin write at register 0
  0x00, // reg[0] = 0
  0x00, // alarms and interrupts off
  0x00, // seconds
  0x52, // minutes
  0x13, // hours
  0x01, // days, don't care
  0x01, // weekdays, don't care
  0x01, // century_months, don't care
  0x01, // years, don't care
  0x00, // alarm stuff
  0x00, // alarm stuff
  0x00, // alarm stuff
  0x00, // alarm stuff
  0x82, // CLKOUT at 32Hz
  0x00, // timer off
  0x00, // no timer value
};
static size_t rtc_init_data_len = 17;


// Only call this on startup, and only if the time in the RTC is invalid.
void writeToRTC(void)
{
  Wire.beginTransmission(RTCADDR);
  Wire.write(rtc_init_data, rtc_init_data_len);
  Wire.endTransmission();
}



volatile uint8_t newSecond = 0;


void timerInterrupt(void)
{
  static uint8_t count125 = 0;
  static uint8_t count27 = 0;
  uint8_t count27_max;
  
  if (count125 >= 125) {
    count125 = 0;
  }
  if (count27 < 88 && (count27 & 0x1)) {
    count27_max = 27;
  }
  else {
    count27_max = 28;
  }
  count27++;
  if (count27 >= count27_max) {
    count27 = 0;
    count125++;
    newSecond++;
  }
}

void readRTC(uint8_t *buffer)
{
  int index=0;

  Wire.beginTransmission(RTCADDR);
  Wire.write(0x02);
  Wire.endTransmission();
  Wire.requestFrom(RTCADDR, 3);
  while (Wire.available()) {
    buffer[index++] = Wire.read();
  }
  checkRTCbuffer(buffer);
  //printRTCbuffer(buffer);
}

uint8_t rtcIsValid(void)
{
  uint8_t VL_seconds;

  Wire.beginTransmission(RTCADDR);
  Wire.write(0x02);
  Wire.endTransmission();
  Wire.requestFrom(RTCADDR, 1);
  VL_seconds = Wire.read();
  if (VL_seconds & 0x80) {
    return 0;
  } else {
    return 1;
  }
}

char nibbleToHex(uint8_t u)
{
  u &= 0x0f;
  if (u <= 9) {
    return u + '0';
  } else {
    return (u-10) + 'A';
  }
}

long readRTCinDecimal()
{
  char s[17];
  uint8_t buffer[3] = { 0xff,0xff,0xff };
  readRTC(buffer);
  s[16] = '\0';
  lcd.setCursor(0, 0);
  lcd.print(s);
  delay(1500);
  return rtcToDecimal(buffer);
}

long rtcToDecimal(uint8_t *buffer)
{
  long decimal;

  // All the constants in here must be longs (with the L suffix) or we end
  // up doing truncated arithmetic.
  decimal =
    ((buffer[0] & 0x0f) + (10L * ((buffer[0] & 0x70) >> 4))) +
    (((buffer[1] & 0x0f) + (10L * ((buffer[1] & 0x70) >> 4))) * 60L) +
    (((buffer[2] & 0x0f) + (10L * ((buffer[2] & 0x30) >> 4))) * 3600L);
  decimal = (decimal * 125L) / 108L;
  return decimal;
}


// Convert a decimal time to bytes suitable for writing into the RTC.  We
// expect a pointer to the three bytes used for seconds, minutes, and
// hours.
void decimalToRTCBytes(long dtime, byte *bytes)
{
  byte s0, s1, m0, m1, h0, h1;
  long rtime = (dtime*108L)/125L;
  long rSec, rMin, rHour;
  
  rSec = rtime % 60L;
  rtime /= 60L;
  rMin = rtime % 60L;
  rtime /= 60L;
  rHour = rtime;
    
  s0 = rSec % 10;
  s1 = rSec / 10;
  m0 = rMin % 10;
  m1 = rMin / 10;
  h0 = rHour % 10;
  h1 = rHour / 10;
  
  bytes[0] = (s1 << 4) | s0;
  bytes[1] = (m1 << 4) | m0;
  bytes[2] = (h1 << 4) | h0;
}


void checkRTCbuffer(uint8_t *buffer)
{
  if (
      ((buffer[0] & 0x0f) > 9) ||
      (((buffer[0] & 0x70) >> 4 ) > 5) ||
      ((buffer[1] & 0x0f) > 9) ||
      (((buffer[1] & 0x70)>>4) > 5) ||
      ((buffer[2] & 0x0f) > 9) ||
      (((buffer[2] & 0x30) >> 4) > 2)
      )
  {
    printRTCbuffer(buffer);
  }
}


void printRTCbuffer(uint8_t *buffer)
{
  char s[9];

  lcd.clear();
  s[0] = nibbleToChar(buffer[0] >> 4);
  s[1] = nibbleToChar(buffer[0]);
  s[2] = ' ';
  s[3] = nibbleToChar(buffer[1] >> 4);
  s[4] = nibbleToChar(buffer[1]);
  s[5] = ' ';
  s[6] = nibbleToChar(buffer[2] >> 4);
  s[7] = nibbleToChar(buffer[2]);
  s[8] = '\0';
  lcd.print(s);
  while (true)
    ;
}


char nibbleToChar(uint8_t nibble)
{
  nibble &= 0x0f;
  if (nibble <= 9)
    return '0' + nibble;
  else
    return 'a' + (nibble-10);
}


// Get the current number of new seconds atomically.
// Should always return 0 or 1, but could possibly
// return a greater value if we have done something in
// the main loop that took more than one second.
uint8_t getNewSecond(void)
{
  uint8_t newsec;

  cli();
  newsec = newSecond;
  newSecond = 0;
  sei();
  return newsec;
}


void loop()
{
  uint8_t newsec;
  uint8_t button;
  static uint8_t started = 0;
  
  if (! started) {
    started = 1;
  }

  digitalWrite(12, 1);

  newsec = getNewSecond();
  if (newsec) {
    decTime += newsec;
    updateTimeFull(decTime, true);
  }
  
  button = readButtons();

  switch (button) {
    case BUTTON_SELECT:
      editTime();
      // Discard the newsec value, as it has probably incremented a fair bit
      // while we've been editing the time.
      newsec = getNewSecond();
      break;
    case BUTTON_UP:
    case BUTTON_DOWN:
      changeBrightness(button);
      break;
  }

  digitalWrite(12, 0);
}

void changeBrightness(byte buttonB) {
  if (buttonB == BUTTON_UP) {
    switch (bness) {
      case lev1:
        bness = lev2;
        break;
      case lev2:
        bness = lev3;
        break;
      case lev3:
        bness = lev4;
        break;
      case lev4:
        bness = lev5;
        break;
      case lev5:
        break;
    }
  }
  if (buttonB == BUTTON_DOWN) {
    switch (bness) {
      case lev1:
        break;
      case lev2:
        bness = lev1;
        break;
      case lev3:
        bness = lev2;
        break;
      case lev4:
        bness = lev3;
        break;
      case lev5:
        bness = lev4;
        break;
    }
  }
  analogWrite( LCD_BACKLIGHT_PIN , bness );
}

void updateTimeFull(long decTime, boolean moving)
{
  byte d54, d32, d10;
  d10 = decTime % 100;
  decTime /= 100;
  d32 = decTime % 100;
  decTime /= 100;
  d54 = decTime % 100;
  updateTimeParts(d54, d32, d10, moving);
}

void updateTimeParts(byte d54, byte d32, byte d10, boolean moving)
{
  byte spaces=0;
  byte spot;
  char s[17];
  char top[17];

  if (0 == d54) {
    d54 = 10;
  }

  s[16] = '\0';
  for (byte i=0; i<16; i++) {
    s[i] = ' ';
  }
  if (moving) {
    spaces = d32 % 5;
  }
  spot = spaces;

  sprintf(s+spot, "%02d", d54);
  s[spot+2] = ' '; // Replace the null byte.
  s[spot+3] = ':';
  spot += 5;
  if (d32 > 100) {
    sprintf(s+spot, "%s", "CC");
  } else {
    sprintf(s+spot, "%02d", d32);
  }
  s[spot+2] = ' '; // Replace the null byte.
  s[spot+3] = ':';
  spot += 5;
  if (d10 > 100) {
    sprintf(s+spot, "%s", "CC");
  } else {
    sprintf(s+spot, "%02d", d10);
  }
  s[spot+2] = ' '; // Replace the null byte.
  s[16] = '\0';    // Restore the null byte.

  lcd.setCursor(0, 1);
  lcd.print(s);


  const char *greet;
  char greetline[17];
  uint8_t extraspaces;

  if (( d54 >= 10 ) || ( d54 < 2 ))
  {
    greet = "Go to bed";
    extraspaces = 7;
  }
  else if (( d54 >= 2 ) && ( d54 < 5 ))
  {
    greet = "Good morning";
    extraspaces = 4;
  }
  else if (( d54 >= 5 ) && ( d54 < 7 ))
  {
    greet = "Good day";
    extraspaces = 8;
  }
  else if (( d54 >= 7 ) && ( d54 < 8))
  {
    greet = "Good evening";
    extraspaces = 4;
  }
  else if (( d54 >= 8 ) && ( d54 < 10 ))
  {
    greet = "Good night";
    extraspaces = 6;
  }
  for (uint8_t i=0; i<16; i++) {
    greetline[i] = ' ';
  }
  greetline[16] = '\0';
  spot = d32 % (extraspaces+1);
  uint8_t greetspot = 0;
  while (greet[greetspot]) {
    greetline[spot] = greet[greetspot];
    greetspot++;
    spot++;
  }

  lcd.setCursor(0, 0);
  lcd.print(greetline);
}




void editTime()
{
  int8_t d54, d32, d10;
  d10 = decTime % 100;
  decTime /= 100;
  d32 = decTime % 100;
  decTime /= 100;
  d54 = decTime % 100;

  button = BUTTON_NONE;
  doExit = false;
  updateTimeParts(d54, d32, d10, false);

  //Edit the hours first (d54, where "d54 : d32 : d10")
  while( doExit == false ) {
    button = readButtons();

    switch( button ) {
      case BUTTON_UP:
        if (10 <= d54) {
          d54 = 1;
        } else {
          d54 = d54 + 1;
        }
        updateTimeParts(d54, d32, d10, false);
        break;

      case BUTTON_DOWN:
        if (1 >= d54) {
          d54 = 10;
        } else {
          d54 = d54 - 1;
        }
        updateTimeParts(d54, d32, d10, false);
        break;

      case BUTTON_SELECT:
        doExit = true;
        break;
    }
    lcd.blink();
    lcd.setCursor(1, 1);
  }


  lcd.noBlink();
  doExit = false;
  //Edit the minutes second (d32, where "d54 : d32 : d10")
  while( doExit == false ) {
    button = readButtons();

    switch( button ) {
      case BUTTON_UP:
        if (99 <= d32) {
          d32 = 0;
        } else {
          d32 = d32 + 1;
        }
        updateTimeParts(d54, d32, d10, false);
        break;

      case BUTTON_DOWN:
        if (0 >= d32) {
          d32 = 99;
        } else {
          d32 = d32 - 1;
        }
        updateTimeParts(d54, d32, d10, false);
        break;

      case BUTTON_SELECT:
        doExit = true;
        break;
    }

    lcd.blink();
    lcd.setCursor(6, 1);
  }

  lcd.noBlink();
  doExit = false;
  //Edit the seconds last (d10, where "d54 : d32 : d10")
  while( doExit == false ) {
    button = readButtons();

    switch( button ) {
      case BUTTON_UP:
        if (99 <= d10) {
          d10 = 0;
        } else {
          d10 = d10 + 1;
        }
        updateTimeParts(d54, d32, d10, false);
        break;

      case BUTTON_DOWN:
        if (0 >= d10) {
          d10 = 99;
        } else {
          d10 = d10 - 1;
        }
        updateTimeParts(d54, d32, d10, false);
        break;

      case BUTTON_SELECT:
        doExit = true;
        break;
    }

    lcd.blink();
    lcd.setCursor(11, 1);
  }

  decTime = ((long)d54) * 10000L;
  decTime = decTime + ((long)d32) * 100L;
  decTime = decTime + (long)d10;

  decimalToRTCBytes(decTime, rtc_init_data + 3);
  writeToRTC();

  lcd.noBlink();
}


byte readButtons()
{
  static byte buttonWas = BUTTON_NONE;   // used for detection of button events
  byte button = BUTTON_NONE; //return no button pressed if the outcomes are different
  byte b1, b2, b3;
  
  b1 = readRawButtons();
  delay(5);
  b2 = readRawButtons();
  delay(5);
  b3 = readRawButtons();
  
  if ( (b1 == b2) && (b2 == b3) ) {
    button = b1;
  }
  
  if( button == BUTTON_NONE ) {
    buttonWas = button;
  } else if ( buttonWas != BUTTON_NONE ) {
    button = BUTTON_NONE;
  } else {
    buttonWas = button;
  }
  
  return ( button );
}


byte readRawButtons(void)
{

  unsigned int buttonVoltage;
  byte button = BUTTON_NONE;   // return no button pressed if the below checks don't write to btn

  //read the button ADC pin voltage
  buttonVoltage = analogRead( BUTTON_ADC_PIN );
  //sense if the voltage falls within valid voltage windows
  if(   buttonVoltage >= ( UP_10BIT_ADC - BUTTONHYSTERESIS )
    && buttonVoltage <= ( UP_10BIT_ADC + BUTTONHYSTERESIS ) )
  {
    button = BUTTON_UP;
  }
  else if(   buttonVoltage >= ( DOWN_10BIT_ADC - BUTTONHYSTERESIS )
    && buttonVoltage <= ( DOWN_10BIT_ADC + BUTTONHYSTERESIS ) )
  {
    button = BUTTON_DOWN;
  }
  else if( buttonVoltage <= ( SELECT_10BIT_ADC + BUTTONHYSTERESIS ) )
  {
    button = BUTTON_SELECT;
  }
  
  return( button );
}
