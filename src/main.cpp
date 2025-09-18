#include <Arduino.h>
#include <AccelStepper.h>

// 28BYJ-48 über ULN2003 (4 Drähte), Pinreihenfolge: IN1, IN3, IN2, IN4
// Achtung: Reihenfolge ist (IN1, IN3, IN2, IN4) => (8, 10, 9, 11)
AccelStepper stepper(AccelStepper::FULL4WIRE, 8, 10, 9, 11);

// --- Split-Flap Parameter ---
const long   STEPS_PER_REV   = 2048;     // typischer 28BYJ-48-Wert
const int    POSITIONS       = 37;       // A-Z, 0-9, Blank
const float  STEPS_PER_CHARF = (float)STEPS_PER_REV / (float)POSITIONS;
const long   STEPS_PER_CHAR  = 55;       // Startwert (≈55.35), später feinjustieren
// Motion-Parameter
const float  MAX_SPEED       = 500;      // Steps/s (später ggf. 600–800 testen)
const float  ACCEL           = 300;      // Steps/s^2

// Optional: Hall-Sensor für Homing
const int    HALL_PIN        = 6;        // A3144 -> OUT an D6, Vcc 5V, GND
const bool   USE_HOMING      = true;     // auf false setzen, wenn kein Sensor da

int currentIndex = 0; // aktuelle Zeichenposition [0..36]

// --- Zeichensatz-Mapping Beispiel ---
// 0..25 = 'A'..'Z', 26..35 = '0'..'9', 36 = Blank
int charToIndex(char c) {
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= 'a' && c <= 'z') return c - 'a';
  if (c >= '0' && c <= '9') return 26 + (c - '0');
  return 36; // alles andere => Blank
}

void goToIndex(int targetIndex) {
  // kürzester Vorwärtsweg (einfach gehalten)
  int delta = (targetIndex - currentIndex + POSITIONS) % POSITIONS;
  long targetSteps = stepper.currentPosition() + delta * STEPS_PER_CHAR;
  stepper.moveTo(targetSteps);

}

void homeIfEnabled() {
  if (!USE_HOMING) return;

  Serial.println("HOMING");

  pinMode(HALL_PIN, INPUT_PULLUP);
  // Langsam drehen bis Sensor triggert (LOW, je nach Verdrahtung)
  stepper.setMaxSpeed(200);
  stepper.setAcceleration(200);
  stepper.setSpeed(150); // Konstantfahrt für Homing
  
  Serial.println(digitalRead(HALL_PIN));
  
  while (digitalRead(HALL_PIN) == HIGH) { // HIGH = nicht getriggert (typisch)
    stepper.runSpeed(); // konstante Geschwindigkeit ohne Rampen
    Serial.println(digitalRead(HALL_PIN));
  }
  Serial.println(digitalRead(HALL_PIN));
  // Referenz gefunden
  stepper.setCurrentPosition(0);
  currentIndex = 0;

  // Motion-Parameter wieder hochsetzen
  stepper.setMaxSpeed(MAX_SPEED);
  stepper.setAcceleration(ACCEL);
}

void setup() {
  stepper.setMaxSpeed(MAX_SPEED);
  stepper.setAcceleration(ACCEL);

  // Demo: per Serial Zeichen schicken, z. B. 'A', 'B', '3', ' ' (Leerzeichen)
  Serial.begin(115200);

  if (USE_HOMING) homeIfEnabled();

  Serial.println(F("Sende einen Buchstaben A-Z, Ziffer 0-9, oder Leerzeichen fuer Blank."));
}

void loop() {
  // Neue Zielvorgabe per Serial?
  if (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      // ignorieren
    } else {
      int target = charToIndex(c);
      stepper.enableOutputs();
      goToIndex(target);
      currentIndex = target;
      Serial.print(F("Setze auf: ")); Serial.print(c);
      Serial.print(F(" (Index ")); Serial.print(target); Serial.println(F(")"));
    }
  }

  // Bewegung ausführen (Rampen)
  stepper.run();

  if (stepper.distanceToGo() == 0) {
    stepper.disableOutputs();
  }
}
