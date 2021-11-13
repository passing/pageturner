#include "HID-Project.h"
#include <EEPROM.h>

unsigned long currentMillis = 0;
unsigned long lastMillis = 0;
unsigned long diffMillis = 0;

const int UNPLUGGED = 2;
/*
 * UNPLUGGED is used when the digital state is LOW, but the analog read is above a certain threshold.
 * that is the case when the Jack is unplugged, so the tip pin is connected to ground via a resistor.
 */

// console enabled
bool console = false;

// input pins
const int inputJackTip = 4;
const int inputJackTipAnalog = A6;
const int inputJackTipAnalogThreshold = 100;
int inputJackTipAnalogRead = 0;
const int inputJackRing = 3;
const int inputPushbutton = 2;

// pin readings
const int inputReadDebounceDelay = 25;
int inputStatePushbutton = HIGH;
int inputStatePushbuttonLast = HIGH;
int inputStatePushbuttonCurrent = HIGH;
int inputStatePushbuttonCurrentDuration = 0;
int inputStateJackRing = HIGH;
int inputStateJackRingLast = HIGH;
int inputStateJackRingCurrent = HIGH;
int inputStateJackRingCurrentDuration = 0;
int inputStateJackTip = HIGH;
int inputStateJackTipLast = HIGH;
int inputStateJackTipCurrent = HIGH;
int inputStateJackTipCurrentDuration = 0;

// eeprom addresses
const byte eepromAddressMode = 0;     // current mode
const byte eepromAddressPolarity = 1; // jack polarity
const byte eepromAddressDelay = 2;    // key delay
const byte eepromAddressSwap = 3;     // swap state (1 bit per mode)

// mode switching
const byte modes = 4; // number of modes
byte mode = 0;        // current mode

// jack polarity
byte inputJackPolarity = 0; // 0 = action on low, 1 = action on high

// key delay
const int keyDelays[] = {500, 150, 50}; // action repeat interval (milliseconds)
byte keyDelay = 1;                      // active delay
const int keyDelayUnplugged = 200;      // key delay for pushbutton when jack is unplugged
unsigned long keyNextMillis = 0;        // absolute time when next action is allowed
bool keyRepeat = 1;                     // if action is repeated

// jack swap
byte inputJackSwap = 0; //  high = swap input (1 bit per mode)

// push button action
byte pushButtonAction = 0;
/*
 * 0 = released
 * 1 = pushed
 * 2 = pushed + tip
 * 3 = pushed + ring
 * 4 = pushed + tip + ring
 */

// led blinking
const int ledPin = LED_BUILTIN;
unsigned long ledMillis = 0;
unsigned long ledSlotMillis = 0;
const int ledPeriod = 2000;
const int ledSlotInterval = 130;
int ledSlot = 0;
int ledState = LOW;

void printTime(unsigned long time) {
  float s = float(time % 60000) / 1000;
  time /= 60000;
  int m = time % 60;
  int h = time / 60;

  Serial.print(h);
  Serial.print(":");
  Serial.print(m);
  Serial.print(":");
  Serial.print(s, 3);
  Serial.print(" | ");
}

bool action(byte mode, byte action) {
  bool repeat = true;

  if (console) {
    printTime(currentMillis);
    Serial.print("action: ");
    Serial.print(mode);
    Serial.print("/");
    Serial.println(action);
  }

  switch (mode) {
  case 0:
    // mode 1: up / down
    switch (action) {
    case 0:
      Keyboard.write(KEY_UP_ARROW);
      break;
    case 1:
      Keyboard.write(KEY_DOWN_ARROW);
      break;
    }
    break;
  case 1:
    // mode 2: page up / page down
    switch (action) {
    case 0:
      Keyboard.write(KEY_PAGE_UP);
      break;
    case 1:
      Keyboard.write(KEY_PAGE_DOWN);
      break;
    }
    break;
  case 2:
    // mode 3: mouse wheel up / mouse wheel down
    switch (action) {
    case 0:
      Mouse.move(0, 0, 1);
      break;
    case 1:
      Mouse.move(0, 0, -1);
      break;
    }
    break;
  case 3:
    // mode 4: volume up / volume down / mute
    switch (action) {
    case 0:
      Consumer.write(MEDIA_VOLUME_UP);
      break;
    case 1:
      Consumer.write(MEDIA_VOLUME_DOWN);
      break;
    case 2:
      Consumer.write(MEDIA_VOLUME_MUTE);
      repeat = false;
      break;
    }
    break;
  }
  return repeat;
}

byte eeprom_read(byte address, byte valueMax, byte valueDefault) {
  byte valueRead = EEPROM.read(address);
  if (valueRead <= valueMax) {
    return valueRead;
  } else {
    return valueDefault;
  }
}

void setup() {
  // configure pins
  pinMode(ledPin, OUTPUT);
  pinMode(inputJackTip, INPUT_PULLUP);
  pinMode(inputJackRing, INPUT_PULLUP);
  pinMode(inputPushbutton, INPUT_PULLUP);

  // enable serial console when pushbutton is active
  if (digitalRead(inputPushbutton) == LOW) {
    Serial.begin(9600);
    while (!Serial) {
      ; // wait for serial port to connect. Needed for native USB
    }
    printTime(millis());
    Serial.println("console started");
    console = true;
  }

  // initialize USB
  Mouse.begin();
  Keyboard.begin();
  Consumer.begin();

  if (console) {
    printTime(millis());
    Serial.println("USB initialized");
  }

  // read stored settings
  mode = eeprom_read(eepromAddressMode, modes, mode);
  inputJackPolarity = eeprom_read(eepromAddressPolarity, 1, inputJackPolarity);
  keyDelay = eeprom_read(eepromAddressDelay, 2, keyDelay);
  inputJackSwap = eeprom_read(eepromAddressSwap, bit(modes + 1) - 1, inputJackSwap);

  if (console) {
    printTime(millis());
    Serial.print("settings restored: mode=");
    Serial.print(mode);
    Serial.print(", polarity=");
    Serial.print(inputJackPolarity);
    Serial.print(", delay=");
    Serial.print(keyDelays[keyDelay]);
    Serial.print(", swap=");
    Serial.println(inputJackSwap, BIN);
  }
}

void loop() {
  // get current time and difference to last loop run
  lastMillis = currentMillis;
  currentMillis = millis();
  diffMillis = currentMillis - lastMillis;

  // read all inputs
  inputStatePushbuttonLast = inputStatePushbuttonCurrent;
  inputStatePushbuttonCurrent = digitalRead(inputPushbutton);
  inputStateJackRingLast = inputStateJackRingCurrent;
  inputStateJackRingCurrent = digitalRead(inputJackRing);
  inputStateJackTipLast = inputStateJackTipCurrent;
  inputStateJackTipCurrent = digitalRead(inputJackTip);

  // detect unplugged jack
  if (inputStateJackTipCurrent == LOW) {
    inputJackTipAnalogRead = analogRead(inputJackTipAnalog);
    if (inputJackTipAnalogRead >= inputJackTipAnalogThreshold) {
      inputStateJackTipCurrent = UNPLUGGED;
    }
  }

  // debounce pushbutton state
  if (inputStatePushbuttonCurrent != inputStatePushbutton && inputStatePushbuttonCurrent == inputStatePushbuttonLast) {
    inputStatePushbuttonCurrentDuration += diffMillis;
    if (inputStatePushbuttonCurrentDuration >= inputReadDebounceDelay) {
      inputStatePushbutton = inputStatePushbuttonCurrent;
      if (console) {
        printTime(currentMillis);
        Serial.print("pushbutton ");
        Serial.println(inputStatePushbutton);
      }
    }
  } else {
    inputStatePushbuttonCurrentDuration = 0;
  }

  // debounce jack ring state
  if (inputStateJackRingCurrent != inputStateJackRing && inputStateJackRingCurrent == inputStateJackRingLast) {
    inputStateJackRingCurrentDuration += diffMillis;
    if (inputStateJackRingCurrentDuration >= inputReadDebounceDelay) {
      inputStateJackRing = inputStateJackRingCurrent;
      if (console) {
        printTime(currentMillis);
        Serial.print("jack ring ");
        Serial.println(inputStateJackRing);
      }
    }
  } else {
    inputStateJackRingCurrentDuration = 0;
  }

  // debounce jack tip state
  if (inputStateJackTipCurrent != inputStateJackTip && inputStateJackTipCurrent == inputStateJackTipLast) {
    inputStateJackTipCurrentDuration += diffMillis;
    if (inputStateJackTipCurrentDuration >= inputReadDebounceDelay) {
      inputStateJackTip = inputStateJackTipCurrent;
      if (console) {
        printTime(currentMillis);
        Serial.print("jack tip ");
        Serial.println(inputStateJackTip);
      }
    }
  } else {
    inputStateJackTipCurrentDuration = 0;
  }

  // LED Update (blink according to active mode)
  if (inputStatePushbutton == LOW) {
    // keep LED off while pushbutton is pressed
    if (ledState == HIGH) {
      digitalWrite(ledPin, LOW);
      ledState = LOW;
    }
    ledMillis = currentMillis;
    ledSlotMillis = ledMillis;
    ledSlot = 0;
  } else if (inputStateJackTip == UNPLUGGED) {
    // keep LED on while unplugged
    if (ledState == LOW) {
      digitalWrite(ledPin, HIGH);
      ledState = HIGH;
    }
    ledMillis = currentMillis;
    ledSlotMillis = ledMillis;
    ledSlot = 0;
  } else {
    // each period is devided into slots
    // the LED is on in even slots only so that its blink count indicates the current mode
    while (currentMillis >= ledMillis + ledPeriod) {
      // time reached end of period
      ledMillis += ledPeriod;
      ledSlotMillis = ledMillis;
      ledSlot = 0;
    }
    while (currentMillis >= ledSlotMillis + ledSlotInterval) {
      // time reached end of slot
      ledSlotMillis += ledSlotInterval;
      ledSlot++;
    }
    if (ledSlot % 2 == 0 && ledSlot <= mode * 2) {
      if (ledState == LOW) {
        digitalWrite(ledPin, HIGH);
        ledState = HIGH;
      }
    } else if (ledState == HIGH) {
      digitalWrite(ledPin, LOW);
      ledState = LOW;
    }
  }

  // Process Steady Inputs
  if (inputStateJackTip == UNPLUGGED) {
    // while jack is unplugged, trigger default action when pushbutton is active

    if (inputStatePushbutton == LOW) {
      if (keyRepeat && currentMillis > keyNextMillis) {
        keyRepeat = action(0, 1);
        keyNextMillis += keyDelayUnplugged;
      }
    } else {
      // inactive, reset variable so next action does not get delayed
      keyNextMillis = currentMillis;
      keyRepeat = true;
    }

  } else if (inputStatePushbutton == LOW) {
    // pushbutton pressed, collect additional inputs

    bitSet(pushButtonAction, 0);
    if (inputStateJackRing == inputJackPolarity) {
      bitSet(pushButtonAction, 1);
    }
    if (inputStateJackTip == inputJackPolarity) {
      bitSet(pushButtonAction, 2);
    }

  } else if (pushButtonAction > 0) {
    // pushbutton released, evaluate collected inputs

    if (console) {
      printTime(currentMillis);
      Serial.print("config action (");
      Serial.print(pushButtonAction >> 1);
      Serial.print("): ");
    }
    switch (pushButtonAction >> 1) {
    case 0: // pushbutton only was active
      mode = (mode + 1) % modes;
      if (console) {
        Serial.print("switch mode (");
        Serial.print(mode);
        Serial.println(")");
      }
      EEPROM.write(eepromAddressMode, mode);
      break;
    case 1:                       // pushbutton + tip were active
      inputJackSwap ^= bit(mode); // flip bit of active mode
      if (console) {
        Serial.print("toggle swap (");
        Serial.print(inputJackSwap, BIN);
        Serial.println(")");
      }
      EEPROM.write(eepromAddressSwap, inputJackSwap);
      break;
    case 2: // pushbutton + ring were active
      keyDelay = (keyDelay + 1) % (sizeof(keyDelays) / sizeof(*keyDelays));
      if (console) {
        Serial.print("switch key delay (");
        Serial.print(keyDelays[keyDelay]);
        Serial.println(")");
      }
      EEPROM.write(eepromAddressDelay, keyDelay);
      break;
    case 3: // pushbutton + tip + ring were active
      inputJackPolarity ^= 1;
      if (console) {
        Serial.print("toggle polarity (");
        Serial.print(inputJackPolarity);
        Serial.println(")");
      }
      EEPROM.write(eepromAddressPolarity, inputJackPolarity);
      break;
    }
    pushButtonAction = 0;

  } else if (inputStateJackRing == inputJackPolarity && inputStateJackTip == inputJackPolarity) {
    // both active

    if (keyRepeat && currentMillis >= keyNextMillis) {
      keyRepeat = action(mode, 2);
      keyNextMillis += keyDelays[keyDelay];
    }

  } else if (inputStateJackRing == inputJackPolarity) {
    // ring active

    if (keyRepeat && currentMillis >= keyNextMillis) {
      keyRepeat = action(mode, 0 ^ bitRead(inputJackSwap, mode));
      keyNextMillis += keyDelays[keyDelay];
    }

  } else if (inputStateJackTip == inputJackPolarity) {
    // tip active

    if (keyRepeat && currentMillis >= keyNextMillis) {
      keyRepeat = action(mode, 1 ^ bitRead(inputJackSwap, mode));
      keyNextMillis += keyDelays[keyDelay];
    }

  } else {
    // inactive, reset variable so next action does not get delayed

    keyNextMillis = currentMillis;
    keyRepeat = true;
  }
}
