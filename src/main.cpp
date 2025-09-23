#include <Arduino.h>
#include <AccelStepper.h>

// 28BYJ-48 via ULN2003 (4 wires), pin order: IN1, IN3, IN2, IN4
// Note: Order is (IN1, IN3, IN2, IN4) => (8, 10, 9, 11)
AccelStepper stepper(AccelStepper::FULL4WIRE, 8, 10, 9, 11);

// Split-Flap parameters
const long   STEPS_PER_REV   = 2048;     // typical 28BYJ-48 value
const int    POSITIONS       = 37;       // A-Z, 0-9, blank
const float  STEPS_PER_CHARF = (float)STEPS_PER_REV / (float)POSITIONS;
const long   STEPS_PER_CHAR  = 55;       // Initial value (≈55.35), fine-tune later

// Motion parameters
const float  MAX_SPEED       = 500;      // Steps/s (later try 600–800)
const float  ACCEL           = 300;      // Steps/s^2

// Hall sensor for homing
const int    HALL_PIN        = 6;        // A3144 -> OUT to D6, Vcc 5V, GND
const bool   USE_HOMING      = true;     // set to false if no sensor present

int currentIndex = 0; // current character position [0..36]

/**
 * @brief Maps a character to its corresponding split-flap index.
 *
 * Converts a character ('A'-'Z', 'a'-'z', '0'-'9', or others) to an index for the split-flap display.
 * Letters map to 0..25, digits to 26..35, and all other characters to 36 (blank).
 *
 * @param c The character to map.
 * @return int The corresponding index (0..36).
 */
int charToIndex(char c) {
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= 'a' && c <= 'z') return c - 'a';
  if (c >= '0' && c <= '9') return 26 + (c - '0');
  return 36; // everything else => blank
}

/**
 * @brief Moves the stepper to the specified character index.
 *
 * Calculates the shortest forward path to the target index and sets the stepper's target position accordingly.
 *
 * @param targetIndex The desired character index to move to (0..36).
 */
void goToIndex(int targetIndex) {
  // shortest forward path (simple version)
  int delta = (targetIndex - currentIndex + POSITIONS) % POSITIONS;
  long targetSteps = stepper.currentPosition() + delta * STEPS_PER_CHAR;
  stepper.moveTo(targetSteps);

}

/**
 * @brief Performs homing routine if enabled.
 *
 * If USE_HOMING is true, rotates the stepper motor slowly until the Hall sensor is triggered.
 * Sets the current position to zero when the sensor is activated and restores motion parameters.
 * Prints status information to the serial monitor during the process.
 */
void homeIfEnabled() {
  if (!USE_HOMING) return;

  Serial.println("HOMING");

  pinMode(HALL_PIN, INPUT_PULLUP);
  // Rotate slowly until sensor triggers (LOW, depending on wiring)
  stepper.setMaxSpeed(200);
  stepper.setAcceleration(200);
  stepper.setSpeed(150); // constant speed for homing
  
  Serial.println(digitalRead(HALL_PIN));
  
  while (digitalRead(HALL_PIN) == HIGH) { // HIGH = not triggered (typical)
    stepper.runSpeed(); // constant speed without ramps
    Serial.println(digitalRead(HALL_PIN));
  }
  Serial.println(digitalRead(HALL_PIN));
  // Reference found
  stepper.setCurrentPosition(0);
  currentIndex = 0;

  // Restore motion parameters
  stepper.setMaxSpeed(MAX_SPEED);
  stepper.setAcceleration(ACCEL);
}

/**
 * @brief Arduino setup function. Initializes stepper parameters, serial communication, and performs homing if enabled.
 *
 * Sets the maximum speed and acceleration for the stepper motor, starts serial communication,
 * performs homing if the USE_HOMING flag is set, and prints instructions to the serial monitor.
 */
void setup() {
  stepper.setMaxSpeed(MAX_SPEED);
  stepper.setAcceleration(ACCEL);

  // Demo: send a character via Serial, e.g. 'A', 'B', '3', ' ' (space)
  Serial.begin(115200);

  if (USE_HOMING) homeIfEnabled();

  Serial.println(F("Send a letter A-Z, digit 0-9, or space for blank."));
}

/**
 * @brief Arduino main loop function. Handles serial input and stepper control.
 *
 * Reads characters from the serial interface, maps them to split-flap positions,
 * and commands the stepper motor to move accordingly. Disables stepper outputs
 * when movement is complete.
 */
void loop() {
  // New target via Serial?
  if (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      // ignore
    } else {
      int target = charToIndex(c);
      stepper.enableOutputs();
      goToIndex(target);
      currentIndex = target;
      Serial.print(F("Set to: ")); Serial.print(c);
      Serial.print(F(" (Index ")); Serial.print(target); Serial.println(F(")"));
    }
  }

  // Execute movement (ramps)
  stepper.run();

  if (stepper.distanceToGo() == 0) {
    stepper.disableOutputs();
  }
}
