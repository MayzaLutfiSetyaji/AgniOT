// Include Servo.h from Servo library by Michael Margolis
#include <Servo.h>

// Define pin numbers
const int flameAnalogPin = A0;  // ESP-12E only has 1 analog pin (A0)
const int ledRedPin = 5;  // GPIO 5 (D1)
const int ledYellowPin = 0; // GPIO 0 (D3)
const int ledGreenPin = 14; // GPIO 14 (D5)
const int servoPin = 12; // GPIO 12 (D6)

// Intensity Threshold (%)
const int yellowThresholdPercent = 30;
const int redThresholdPercent = 100;

// Servo object
Servo fireServo;

// Track previous state to prevent repeating Servo and Serial calls
enum FireState{SAFE, WARNING_YELLOW, DANGER_RED};
FireState lastState = SAFE;

void setup() {
  // Initialize serial communication
  Serial.begin(115200);
  
  // Set the pin modes
  pinMode(flameAnalogPin, INPUT);  // Flame sensor input
  pinMode(ledRedPin, OUTPUT);  // Red LED output
  pinMode(ledYellowPin, OUTPUT); // Yellow LED output
  pinMode(ledGreenPin, OUTPUT); // Green LED output

  // Attach servo to GPIO pin and set initial position
  fireServo.attach(servoPin);
  fireServo.write(0); //Safe position: 0 degrees

  // Default initial state: Green LED ON
  digitalWrite(ledRedPin, LOW);
  digitalWrite(ledYellowPin, LOW);
  digitalWrite(ledGreenPin, HIGH);
}

void loop() {
  // Read analog value from ESP-12E 
  const int rawValue = analogRead(flameAnalogPin);

  //Convert ADC to percentage (0 to 1023 -> 0% to 100%)
  int intensityPercent = map(rawValue, 0, 1023, 0, 100);
  intensityPercent = constrain(intensityPercent, 0, 100);

  // State Evaluation
  if (intensityPercent >= redThresholdPercent) {
    // Turn on the red LED (DANGER)
    digitalWrite(ledRedPin, HIGH);
    digitalWrite(ledYellowPin, LOW);
    digitalWrite(ledGreenPin, LOW);

    if (lastState != DANGER_RED) {
      lastState = DANGER_RED;
      fireServo.write(90); // Rotate servo to 90 degrees
      Serial.println("Flame detected! Red LED ON");
    }
  } 
  else if (intensityPercent >= yellowThresholdPercent) {
    // Turn on the yellow LED (warning state)
    digitalWrite(ledRedPin, LOW);
    digitalWrite(ledYellowPin, HIGH);
    digitalWrite(ledGreenPin, LOW);

    if (lastState != WARNING_YELLOW) {
      lastState = WARNING_YELLOW;
      fireServo.write(0); // Rotate servo to 0 degrees
      Serial.println("Medium fire warning!! Yellow LED ON");
      
    }
  }
  else {
    // Turn on the green LED (safe state)
    digitalWrite(ledRedPin, LOW);
    digitalWrite(ledYellowPin, LOW);
    digitalWrite(ledGreenPin, HIGH);

    if (lastState != SAFE) {
      lastState = SAFE;
      fireServo.write(0); // Reset servo to 0 degrees
      Serial.println("It is safe for now. Green LED ON");
    }
  }
  
  //Debugging serial output
  Serial.println("Raw ADC (A0): ");
  Serial.println(rawValue);
  Serial.println(" | Intensity: ");
  Serial.println(intensityPercent);
  Serial.println("%");

  delay(100);  // Delay to make the reading stable
}
