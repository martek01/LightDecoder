/*
 ============================================================================
 Name        : Light decoder
 Author      : Markus Grigull
 Version     : 1.0
 Copyright   : 2014
 Description : DCC decoder for controlling a RGB and a white LED stripe
 ============================================================================
 */

// include
#include <NmraDcc.h>

// defines
#define CV_CUSTOM_BASE 30
#define COLOR_SIZE 4  // see RGBColor struct + fade duration
//#define COSINUS_FADE
#define DEBUG

// Bytes per Color: 1=Red, 2=Green, 3=Blue, 4=FadeDuration
#define CV_FIRST_COLOR CV_CUSTOM_BASE
#define CV_SECOND_COLOR CV_CUSTOM_BASE + COLOR_SIZE
#define CV_THRID_COLOR CV_CUSTOM_BASE + 2 * COLOR_SIZE
#define CV_FOURTH_COLOR CV_CUSTOM_BASE + 3 * COLOR_SIZE
#define CV_FIFTH_COLOR CV_CUSTOM_BASE + 4 * COLOR_SIZE
#define CV_SIXTH_COLOR CV_CUSTOM_BASE + 5 * COLOR_SIZE

// pins
const uint8_t DccAckPin = 4;
const uint8_t LedRedPin = 3;
const uint8_t LedGreenPin = 5;
const uint8_t LedBluePin = 6;
const uint8_t LedWhitePin = 9;

// structs
struct CVPair {
  uint16_t  CV;
  uint8_t   Value;
};

struct RGBColor {
  uint8_t red;
  uint8_t green;
  uint8_t blue;
};

struct RGBColorStep {
  double red;
  double green;
  double blue;
};

// global objects and variables
NmraDcc dcc;

struct RGBColor currentColor;
struct RGBColor targetColor;
struct RGBColorStep colorStep;
struct RGBColorStep colorRemainder;

unsigned long fadeDuration;
unsigned long lastUpdate;
 
uint8_t factoryDefaultCVIndex = 0;
 
CVPair factoryDefaultCVs[] = {
  {CV_ACCESSORY_DECODER_ADDRESS_LSB, 1},
  {CV_ACCESSORY_DECODER_ADDRESS_MSB, 0},
};

/*
 * notifyCVResetFactoryDefault
 */
void notifyCVResetFactoryDefault() {
  // Make FactoryDefaultCVIndex non-zero and equal to num CV's to be reset 
  // to flag to the loop() function that a reset to Factory Defaults needs to be done
  factoryDefaultCVIndex = sizeof(factoryDefaultCVs)/sizeof(CVPair);
};

/*
 * notifyCVAck
 */
void notifyCVAck() {
  digitalWrite(DccAckPin, HIGH);
  delay(6);  
  digitalWrite(DccAckPin, LOW);
  
  // print dcc message
  #ifdef DEBUG
    Serial.println("notifyCVAck") ;
  #endif
}

/*
 * notifyCVChange
 */
void notifyCVChange(uint16_t CV, uint8_t value) {
  // print dcc message
  #ifdef DEBUG
    Serial.print("notifyCVChange: ");
    Serial.print(CV, DEC);
    Serial.print(", ");
    Serial.println(value);
  #endif
}

/*
 * matchColors
 */
boolean matchColors(struct RGBColor one, struct RGBColor two) {
  return (one.red == two.red && one.green == two.green && one.blue == two.blue);
}

/*
boolean matchColors(uint8_t red, uint8_t green, uint8_t blue, struct RGBColor two) {
  return (red == two.red && green == two.green && blue == two.blue);
}
*/

/*
 * uint8FromDouble
 * the value is rounded down if it is greater than 0 or rounded up if lower than 0
 */
uint8_t uint8FromDouble(double value) {
  if (value >= 0) {
    return floor(value);  
  } else {
    return ceil(value);
  }
}

/*
 * analogFromColorValue
 * if the COSINUS_FADE define is set, the values will be scaled by cosinus and not linear
 */
uint8_t analogFromColorValue(uint8_t value) {
  #ifdef COSINUS_FADE
    return 128.0 - 127.0 * cos((double)PI * (double)value / 256.0);
  #else
    return value;
  #endif
}

/*
 * setColor
 */
void setColor(uint8_t red, uint8_t green, uint8_t blue) {
  // only write color values that differ from the current color
  if (currentColor.red != red) {
    analogWrite(LedRedPin, analogFromColorValue(red));
    
    #ifdef DEBUG
      Serial.print("R: ");
      Serial.print(red);
      Serial.print(" ");
    #endif
    
    currentColor.red = red;
  }
  
  if (currentColor.green != green) {
    analogWrite(LedGreenPin, analogFromColorValue(green));
    
    #ifdef DEBUG
      Serial.print("G: ");
      Serial.print(green);
      Serial.print(" ");
    #endif
    
    currentColor.green = green;
  }
  
  if (currentColor.blue != blue) {
    analogWrite(LedBluePin, analogFromColorValue(blue));
    
    #ifdef DEBUG
      Serial.print("B: ");
      Serial.print(blue);
      Serial.print(" ");
    #endif
    
    currentColor.blue = blue;
  }
}

void setColor(struct RGBColor color) {
  setColor(color.red, color.green, color.blue);
}

/*
 * setTargetColor
 * same as setColor, but updates the target color. This is neccessery if a color
 * should be set directly instead of being faded
 */
void setTargetColor(uint8_t red, uint8_t green, uint8_t blue) {
  // set color
  setColor(red, green, blue);
  
  // update target color
  targetColor.red = red;
  targetColor.green = green;
  targetColor.blue = blue;
}

/*
 * addColor
 * adds the values to the current color value
 */
void addColor(uint8_t red, uint8_t green, uint8_t blue) {
  // beware, that the color values do not underflow
  if (currentColor.red + red < 0) {
    red -= currentColor.red + red;
  } else if (currentColor.red + red > 255) {
    red = 255 - currentColor.red;
  }
  
  if (currentColor.green + green < 0) {
    green -= currentColor.green + green;
  } else if (currentColor.green + green > 255) {
    green = 255 - currentColor.green;
  }
  
  if (currentColor.blue + blue < 0) {
    blue -= currentColor.blue + blue;
  } else if (currentColor.blue + blue > 255) {
    blue = 255 - currentColor.blue;
  }
  
  // set new color
  setColor(currentColor.red + red, currentColor.green + green, currentColor.blue + blue);
}

/*
 * getColor
 * colors range is from 0 to 5 and the colors are stored in the
 * CV-Values of the decoder (EEPROM)
 */
struct RGBColor &getColor(uint8_t colorNumber) {
  RGBColor color;
  
  color.red = dcc.getCV(CV_CUSTOM_BASE + colorNumber * COLOR_SIZE);
  color.green = dcc.getCV(CV_CUSTOM_BASE + colorNumber * COLOR_SIZE + 1);
  color.blue = dcc.getCV(CV_CUSTOM_BASE + colorNumber * COLOR_SIZE + 2);
  
  return color;
}

/*
 * getFadeDurationForColor
 * range from 0 to 5
 */
uint8_t getFadeDurationForColor(uint8_t colorNumber) {
  return dcc.getCV(CV_CUSTOM_BASE + colorNumber * COLOR_SIZE + 3);
}
 
/*
 * fadeToColor
 */
void fadeToColor(struct RGBColor color, uint8_t fadeDurationInSeconds) {
  // do not change anything if the new color is the same as the target color
  if (matchColors(color, targetColor)) {
    return;
  }
  
  // get fade duration
  fadeDuration = (double)fadeDurationInSeconds * 1000.0;
  
  // calculate color steps
  colorStep.red = ((double)color.red - (double)currentColor.red) / (double)fadeDuration;
  colorStep.green = ((double)color.green - (double)currentColor.green) / (double)fadeDuration;
  colorStep.blue = ((double)color.blue - (double)currentColor.blue) / (double)fadeDuration;
  
  // update target color
  targetColor.red = color.red;
  targetColor.green = color.green;
  targetColor.blue = color.blue;
  
  #ifdef DEBUG
    Serial.print("Target Color: R: ");
    Serial.print(targetColor.red);
    Serial.print(", G: ");
    Serial.print(targetColor.green);
    Serial.print(", B: ");
    Serial.print(targetColor.blue);
    Serial.print(" Step: R: ");
    Serial.print(colorStep.red, DEC);
    Serial.print(", G: ");
    Serial.print(colorStep.green, DEC);
    Serial.print(", B: ");
    Serial.println(colorStep.blue, DEC);
  #endif
  
  // set new update time
  lastUpdate = millis();
  
  // clear remainder from last fade
  colorRemainder.red = 0;
  colorRemainder.green = 0;
  colorRemainder.blue = 0;
}

/*
 * updateColors
 * this function must be called repeatedly in the main loop to keep the
 * colors up-to-date and fade smoothly
 */
void updateColors() {
  // only update if the fade does not ended
  if (fadeDuration > 0) {
    // get time since last update
    unsigned long time = millis();
    
    if (time - lastUpdate >= 1) {
      // if the current color is the target color, stop fading
      if (matchColors(currentColor, targetColor)) {
        // stop fading
        fadeDuration = 0;
        return;
      }
      
      // get millis since last update
      double steps = time - lastUpdate;
      
      // calculate color steps
      double redSteps = colorStep.red * steps + colorRemainder.red;
      double greenSteps = colorStep.green * steps + colorRemainder.green;
      double blueSteps = colorStep.blue * steps + colorRemainder.blue;
      
      // do not change the color, if no step is greater than 1
      if (abs(redSteps) < 1 && abs(greenSteps) < 1 && abs(blueSteps) < 1) {
        return;
      }
      
      // get color step remainder for next update cycle
      colorRemainder.red = fmod(redSteps, 1);
      colorRemainder.green = fmod(greenSteps, 1);
      colorRemainder.blue = fmod(blueSteps, 1);
      
      // set new color
      addColor(uint8FromDouble(redSteps), uint8FromDouble(greenSteps), uint8FromDouble(blueSteps));
      
      // decrease fade duration
      if (fadeDuration > steps) {
        fadeDuration -= steps;
      } else {
        fadeDuration = 0;
      }
      
      // save new update time
      lastUpdate = time;
    }
  }
}

/*
 * setWhiteLed
 */
void setWhiteLed(uint8_t value) {
  analogWrite(LedWhitePin, value);
  
  #ifdef DEBUG
    Serial.print("White: ");
    Serial.println(value);
  #endif
}

/*
 * getButtonMapping
 */
uint8_t getButtonMapping(uint8_t value) {
  switch(value) {
    case 0:
      return 0;
    case 1:
      return 3;
    case 2:
      return 1;
    case 3:
      return 4;
    case 4:
      return 2;
    case 5:
      return 5;
    default:
      return 255;
  }
}

/*
 * handleColorButton
 */
void handleColorButton(uint8_t outputAddr) {
  // get real button number
  outputAddr = getButtonMapping(outputAddr);
  
  // get color and fade duration
  struct RGBColor newColor = getColor(outputAddr);
  uint8_t fadeDuration = getFadeDurationForColor(outputAddr);
  
  // fade to color
  fadeToColor(newColor, fadeDuration);
  
  #ifdef DEBUG
    Serial.print("Color button pressed: ");
    Serial.println(outputAddr);
  #endif
}

/*
 * notifyDccAccState
 */
void notifyDccAccState(uint16_t Addr, uint16_t BoardAddr, uint8_t OutputAddr, uint8_t State) {
  // handle output address
  switch(OutputAddr) {
    case 0:
    case 1:
    case 2:
    case 3:
    case 4: 
    case 5:
      handleColorButton(OutputAddr);
      break;
      
    case 6:
      // turn on white led
      setWhiteLed(255);
      break;
      
    case 7:
      // turn off white led
      setWhiteLed(0);
      break;
    
    default:
      break;
  }
  
  // print dcc message
  Serial.print("notifyDccAccState: ") ;
  Serial.print(Addr,DEC) ;
  Serial.print(',');
  Serial.print(BoardAddr,DEC) ;
  Serial.print(',');
  Serial.print(OutputAddr,DEC) ;
  Serial.print(',');
  Serial.println(State, HEX) ;
}

/*
 * setup
 */
void setup() {
  // open serial communication with baud rate 115200
  Serial.begin(115200);
  
  // set pins' modes
  pinMode(DccAckPin, OUTPUT);
  pinMode(LedRedPin, OUTPUT);
  pinMode(LedGreenPin, OUTPUT);
  pinMode(LedBluePin, OUTPUT);
  pinMode(LedWhitePin, OUTPUT);

  // print decoder name and version
  Serial.println("Light decoder V1.0");
  
  // setup dcc decoder
  dcc.pin(0, 2, 1);
  
  // call the main DCC Init function to enable the DCC Receiver
  dcc.init(MAN_ID_DIY, 10, FLAGS_OUTPUT_ADDRESS_MODE | FLAGS_DCC_ACCESSORY_DECODER | FLAGS_MY_ADDRESS_ONLY, 0);
  Serial.println("Initialization done");
}

/*
 * loop
 */
void loop() {
  // process DCC packages
  dcc.process();
  
  // update colors if needed
  updateColors();
  
  // check if factory defaults need to be set
  if(factoryDefaultCVIndex && dcc.isSetCVReady())
  {
    factoryDefaultCVIndex--; // Decrement first as initially it is the size of the array 
    dcc.setCV(factoryDefaultCVs[factoryDefaultCVIndex].CV, factoryDefaultCVs[factoryDefaultCVIndex].Value);
  }
}
