#include <Arduino.h>
#include <Stepper.h>
#include <ctype.h>
#include <string.h>

// ---------------------------
// Konfiguration
// ---------------------------
const int IN1 = 8;
const int IN2 = 9;
const int IN3 = 10;
const int IN4 = 11;

const int HALL_PIN = 6;            // A3144: LOW = Magnet erkannt
const int POSITIONS = 45;          // Anzahl Zeichen
const long STEPS_PER_REV = 2048;   // 28BYJ-48 typisch
const int STEPPER_RPM = 15;        // Vorgabe original: 15

// Reihenfolge wichtig: (IN1, IN3, IN2, IN4) für 28BYJ-48 + ULN2003
Stepper stepper(STEPS_PER_REV, IN1, IN3, IN2, IN4);

// CCW-Richtung erzwingen
const int DIR = -1;

// Zeichensatz (45 Einträge, Index 0..44)
const char* LETTERS[] = {
  " ","A","B","C","D","E","F","G","H","I","J","K","L","M","N","O","P","Q","R","S",
  "T","U","V","W","X","Y","Z","Ä","Ö","Ü","0","1","2","3","4","5","6","7","8","9",
  ":",".","-","?","!"
};
const int INDEX_UE = 29; // 'Ü'

// Schritte pro Zeichen (nicht ganzzahlig → Rest sammeln)
const float STEPS_PER_CHAR_F = (float)STEPS_PER_REV / (float)POSITIONS;

// ---------------------------
// Zustände
// ---------------------------
volatile int currentIndex = 0;     // 0..44
static float stepRemainder = 0.0f; // Korrektur für Nicht-Ganzzahligkeit

// ---------------------------
// Hilfsfunktionen
// ---------------------------

bool hallTriggered(uint16_t stableMs = 10) {
  if (digitalRead(HALL_PIN) == LOW) {
    delay(stableMs);
    return (digitalRead(HALL_PIN) == LOW);
  }
  return false;
}

void moveDeltaCCW(int deltaChars) {
  if (deltaChars <= 0) return;
  stepper.setSpeed(STEPPER_RPM);

  float want = (float)deltaChars * STEPS_PER_CHAR_F + stepRemainder;
  long steps = lroundf(want);
  stepRemainder = want - (float)steps;

  stepper.step(DIR * steps);
  currentIndex = (currentIndex + deltaChars) % POSITIONS;
}

void goToIndexCCW(int targetIndex) {
  targetIndex = (targetIndex % POSITIONS + POSITIONS) % POSITIONS;
  int delta = (targetIndex - currentIndex + POSITIONS) % POSITIONS; // 0..POSITIONS-1
  moveDeltaCCW(delta);
}

int findIndexBySymbol(const char* sym) {
  for (int i = 0; i < POSITIONS; ++i) {
    if (strcmp(sym, LETTERS[i]) == 0) return i;
  }
  return -1;
}

// Hilfsfunktionen für Case-Handling
static inline char toUpperAscii(char c) {
  if (c >= 'a' && c <= 'z') return c - 'a' + 'A';
  return c;
}

// Vergleicht case-insensitive nur für ASCII (A-Z); Sonderzeichen bleiben wie sind
bool eqIgnoreCaseAscii(const char* a, const char* b) {
  while (*a && *b) {
    char ca = toUpperAscii(*a++);
    char cb = toUpperAscii(*b++);
    if (ca != cb) return false;
  }
  return (*a == '\0' && *b == '\0');
}

// Konvertiere ASCII-Token in Uppercase (nur A-Z), belasse UTF-8 Umlaute
void asciiUpperInPlace(char* s) {
  for (size_t i = 0; s[i]; ++i) {
    if ((unsigned char)s[i] < 0x80) { // ASCII
      s[i] = toUpperAscii(s[i]);
    }
  }
}

// Mappe Eingabe-Token auf Index im LETTERS[]
// Unterstützt:
//  - direktes UTF-8 „Ä/Ö/Ü“ (auch klein „ä/ö/ü“ → zu Großbuchstaben)
//  - ASCII-Buchstaben case-insensitive
//  - Fallbacks "AE"→Ä, "OE"→Ö, "UE"→Ü (case-insensitive)
//  - Satzzeichen : . - ? !
//  -SPACE per: " " (ein Leerzeichen) ODER "SPACE" / "BLANK" / "_" ODER leeres Token nach 'c' (c<Enter>)
int symbolToIndex(char* token) {
  if (!token) return -1;

  // Sonderfall: direkt Leerzeichen / SPACE-Befehle
  if (strcmp(token, " ") == 0) return 0;
  if (eqIgnoreCaseAscii(token, "SPACE") || eqIgnoreCaseAscii(token, "BLANK") || strcmp(token, "_") == 0) return 0;
  if (token[0] == '\0') return 0; // c<Enter> -> space

  // Wenn genau UTF-8 Umlaute klein: mappe auf Groß
  if (strcmp(token, "ä") == 0) return findIndexBySymbol("Ä");
  if (strcmp(token, "ö") == 0) return findIndexBySymbol("Ö");
  if (strcmp(token, "ü") == 0) return findIndexBySymbol("Ü");

  // Direktes Match (inkl. "Ä","Ö","Ü" groß, sowie Satzzeichen)
  int idx = findIndexBySymbol(token);
  if (idx >= 0) return idx;

  // Fallbacks AE/OE/UE (case-insensitive ASCII)
  if (eqIgnoreCaseAscii(token, "AE")) return findIndexBySymbol("Ä");
  if (eqIgnoreCaseAscii(token, "OE")) return findIndexBySymbol("Ö");
  if (eqIgnoreCaseAscii(token, "UE")) return findIndexBySymbol("Ü");

  // Einzelzeichen ASCII → uppercase und suchen
  if (strlen(token) == 1 && (unsigned char)token[0] < 0x80) {
    char up[2] = { toUpperAscii(token[0]), 0 };
    return findIndexBySymbol(up);
  }

  // Letzter Versuch: gesamtes Token ASCII-uppercase und vergleichen (für z. B. "a")
  {
    char buf[8];
    strncpy(buf, token, sizeof(buf) - 1);
    buf[sizeof(buf)-1] = 0;
    asciiUpperInPlace(buf);
    idx = findIndexBySymbol(buf);
    if (idx >= 0) return idx;
  }

  return -1;
}

// Homing: langsam CCW, bis Hall auslöst (diese Position ist „Ü“)
void homeAtUE() {
  Serial.println(F("[Homing] CCW bis Hall (Ü) triggert..."));
  stepper.setSpeed(6); // langsam & sicher

  unsigned long safety = STEPS_PER_REV * 4; // großzügige Grenze
  while (!hallTriggered(5) && safety > 0) {
    stepper.step(DIR * 1);
    safety--;
  }

  if (safety == 0) {
    Serial.println(F("[Homing] WARNUNG: Hall nicht gefunden – setze aktuelle Position als Ü."));
  } else {
    Serial.println(F("[Homing] Hall erkannt."));
  }

  // Optional Feinkorrektur:
  // for (int i=0; i<3; ++i) stepper.step(DIR * 1);

  currentIndex = INDEX_UE;
  stepRemainder = 0.0f;

  Serial.print(F("[Homing] currentIndex="));
  Serial.println(currentIndex);
}

// ---------------------------
// Serielle Steuerung
// Befehle:
//   iNN     -> Index 0..44
//   cTEXT   -> Zeichen (UTF-8: Ä/Ö/Ü; ae/oe/ue; ASCII case-insensitive; SPACE/BLANK/_; c<Leerzeichen>)
//   h       -> homing
//   ?       -> Hilfe
// ---------------------------
void printHelp() {
  Serial.println(F("\nCommands:"));
  Serial.println(F("  iNN   -> go to index NN (0..44), CCW only"));
  Serial.println(F("  cX    -> go to symbol X (UTF-8: Ä/Ö/Ü; Fallbacks: AE/OE/UE; case-insensitive)"));
  Serial.println(F("          Space:  c<space>  oder  cSPACE / cBLANK / c_  oder  c<Enter>"));
  Serial.println(F("  h     -> home to Hall (Ü)"));
  Serial.println(F("  b     -> test backwards through all symbols"));
  Serial.println(F("  f     -> test forwards through all symbols"));
  Serial.println(F("  r     -> test random 10 positions"));
  Serial.println(F("  ?     -> help"));
}

void testBackwards() {
  Serial.println(F("[Test] Backwards through all symbols..."));
  for (int i = POSITIONS - 1; i >= 0; --i) {
    Serial.print(F("[Test] Goto index ")); Serial.print(i); Serial.print(F(" (")); Serial.print(LETTERS[i]); Serial.println(F(")"));
    goToIndexCCW(i);
    delay(500);
  }
  Serial.println(F("[Test] Done."));
}

void testForwards() {
  Serial.println(F("[Test] Forwards through all symbols..."));
  for (int i = 0; i < POSITIONS; ++i) {
    Serial.print(F("[Test] Goto index ")); Serial.print(i); Serial.print(F(" (")); Serial.print(LETTERS[i]); Serial.println(F(")"));
    goToIndexCCW(i);
    delay(500);
  }
  Serial.println(F("[Test] Done."));
}

void testRandom10() {
  Serial.println(F("[Test] Random 10 positions..."));
  for (int i = 0; i < 10; ++i) {
    int idx = random(0, POSITIONS);
    Serial.print(F("[Test] Goto index ")); Serial.print(idx); Serial.print(F(" (")); Serial.print(LETTERS[idx]); Serial.println(F(")"));
    goToIndexCCW(idx);
    delay(500);
  }
  Serial.println(F("[Test] Done."));
}

void handleSerial() {
  static char buf[64];
  static uint8_t len = 0;

  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\r' || c == '\n') {
      buf[len] = '\0';
      if (len > 0) {
        Serial.print(F("[RX] ")); Serial.println(buf);  // Eingabe anzeigen

        if (buf[0] == 'i' || buf[0] == 'I') {
          int idx = atoi(buf + 1);
          if (idx >= 0 && idx < POSITIONS) {
            Serial.print(F("[Goto] index ")); Serial.println(idx);
            goToIndexCCW(idx);
          } else {
            Serial.println(F("[Error] Index 0..44"));
          }
        } else if (buf[0] == 'c' || buf[0] == 'C') {
          // Alles nach 'c' ist Token – NICHT führende Spaces entfernen, damit 'c ' (Leerzeichen) funktioniert
          char* tok = buf + 1;                  // kann leer sein -> Space
          // Entferne nur ein einziges führendes Tab/CR, aber nicht das Space
          if (*tok == '\t' || *tok == '\r') tok++;

          int idx = symbolToIndex(tok);
          if (idx >= 0) {
            Serial.print(F("[Goto] symbol → index ")); Serial.println(idx);
            goToIndexCCW(idx);
          } else {
            Serial.println(F("[Error] Symbol nicht im Set"));
          }
        } else if (buf[0] == 'h' || buf[0] == 'H') {
          homeAtUE();
        } else if (buf[0] == 'b' || buf[0] == 'B') {
          testBackwards();
        } else if (buf[0] == 'f' || buf[0] == 'F') {
          testForwards();
        } else if (buf[0] == 'r' || buf[0] == 'R') {
          testRandom10();
        } else if (buf[0] == '?') {
          printHelp();
        } else {
          Serial.println(F("[Unknown] '?' fuer Hilfe"));
        }
      }
      len = 0;
    } else {
      if (len < sizeof(buf) - 1) buf[len++] = c;
    }
  }
}

// ---------------------------
// Arduino setup/loop
// ---------------------------
void setup() {
  pinMode(HALL_PIN, INPUT_PULLUP);
  Serial.begin(115200);
  delay(50);
  Serial.println(F("\nSplit-Flap (Stepper.h) | CCW | 45 Symbole | Home=Ü via Hall | Speed=15 RPM"));
  printHelp();

  stepper.setSpeed(STEPPER_RPM);
  homeAtUE();
}

void loop() {
  handleSerial();
  // Platz für eigene Demo-Bewegungen
}
