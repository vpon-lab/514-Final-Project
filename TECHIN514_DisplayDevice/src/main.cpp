#include <Arduino.h>
#include <SwitecX25.h>

// This code was developed with Claude AI

#define PIN_BUTTON D8
#define LED D10

SwitecX25 motor(600, D1, D0, D3, D2);

const int MAX_STEPS = 600;
const float MAX_DISTANCE = 4.0;  // meters

int testPosition = MAX_STEPS;  // start at max (physical zero)
bool lastButtonState = HIGH;

int distanceToSteps(float meters)
{
  return MAX_STEPS - (int)((meters / MAX_DISTANCE) * MAX_STEPS);
}

void setup()
{
  Serial.begin(9600);
  pinMode(PIN_BUTTON, INPUT_PULLUP);

  motor.zero();
  delay(3000);
  Serial.println("Homed.");

  // move to physical zero (CW stop)
  motor.setPosition(MAX_STEPS);
  while(motor.currentStep != motor.targetStep) motor.update();
  Serial.println("At physical zero.");
}

void loop()
{
  motor.update();

  bool buttonState = digitalRead(PIN_BUTTON);

  if (lastButtonState == HIGH && buttonState == LOW) {
    testPosition -= 50;  // decrement toward 0 = CCW
    if (testPosition < 0) testPosition = MAX_STEPS;  // wrap back to physical zero
    Serial.print("Moving to step: ");
    Serial.println(testPosition);
    motor.setPosition(testPosition);
  }

  lastButtonState = buttonState;
  delay(20);
}