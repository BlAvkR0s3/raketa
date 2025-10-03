#include <SPI.h>
#include <SD.h>
#include <EEPROM.h>
#include <Wire.h>
#include <Adafruit_BMP3XX.h>

// ---- BMP390 ----
Adafruit_BMP3XX bmp;
#define SEALEVELPRESSURE_HPA (1027) // ustaw na lokalne ciśnienie odniesienia

// ---- SD i logowanie ----
const int chipSelect = 10;
unsigned long previousMillis = 0;
const long interval = 2000; // 0,5 Hz

int licznikPomiarow = 0;
int licznikResetow = 0;
int pomiaryBezKarty = 0;

// EEPROM flag
const int EEPROM_FLAG_ADDR = 0;
const byte EEPROM_FLAG_NONE = 0;
const byte EEPROM_FLAG_CARD_MISSING = 1;

// ---- Funkcje pomocnicze ----
String readLastLine(const char* filename) {
  if (!SD.exists(filename)) return "";
  File f = SD.open(filename, FILE_READ);
  if (!f) return "";
  String line = "", lastLine = "";
  while (f.available()) {
    char c = f.read();
    if (c == '\r') continue;
    if (c == '\n') {
      if (line.length() > 0) lastLine = line;
      line = "";
    } else {
      line += c;
    }
  }
  if (line.length() > 0) lastLine = line;
  f.close();
  return lastLine;
}

int parseResetNumber(const String &l) {
  int idx = l.indexOf("Reset ");
  if (idx == -1) return -1;
  int start = idx + 6;
  int pos = start;
  while (pos < l.length() && isDigit(l.charAt(pos))) pos++;
  if (pos <= start) return -1;
  return l.substring(start, pos).toInt();
}

int parseMeasurementNumber(const String &l) {
  int idx = l.indexOf("Pomiar ");
  if (idx == -1) return 0;
  int start = idx + 7;
  int colon = l.indexOf(':', start);
  if (colon == -1) colon = l.length();
  int pos = start;
  while (pos < colon && isDigit(l.charAt(pos))) pos++;
  if (pos <= start) return 0;
  return l.substring(start, pos).toInt();
}

// ---- Setup ----
void setup() {
  Serial.begin(9600);
  while (!Serial) { ; }

  Serial.println("Inicjalizacja...");

  // BMP390
  if (!bmp.begin_I2C()) {
    Serial.println("Nie można znaleźć BMP390!");
    while (1);
  }
  bmp.setTemperatureOversampling(BMP3_OVERSAMPLING_8X);
  bmp.setPressureOversampling(BMP3_OVERSAMPLING_4X);
  bmp.setIIRFilterCoeff(BMP3_IIR_FILTER_COEFF_3);
  bmp.setOutputDataRate(BMP3_ODR_50_HZ);
  Serial.println("BMP390 gotowy!");

  // SD karta
  if (SD.begin(chipSelect)) {
    Serial.println("Karta SD OK.");

    // Odczytaj ostatni reset
    String lastResetLine = readLastLine("resety.txt");
    int lastResetNum = parseResetNumber(lastResetLine);
    if (lastResetNum >= 0) licznikResetow = lastResetNum;

    // Odczytaj ostatni pomiar
    String lastDataLine = readLastLine("dane.txt");
    licznikPomiarow = parseMeasurementNumber(lastDataLine);

    byte flag = EEPROM.read(EEPROM_FLAG_ADDR);
    if (flag == EEPROM_FLAG_CARD_MISSING) {
      EEPROM.write(EEPROM_FLAG_ADDR, EEPROM_FLAG_NONE);
    }

  } else {
    Serial.println("Brak karty SD!");
  }
}

// ---- Loop ----
void loop() {
  unsigned long currentMillis = millis();
  if (currentMillis - previousMillis < interval) return;
  previousMillis = currentMillis;

  licznikPomiarow++;

  // Odczyt BMP390
  if (!bmp.performReading()) {
    Serial.println("Błąd odczytu BMP390!");
    return;
  }

  float temperatura = bmp.temperature + (-2.0); // korekta
  float cisnienie = bmp.pressure / 100.0;
  float wysokosc = bmp.readAltitude(SEALEVELPRESSURE_HPA);

  // Zapis do SD
  if (SD.begin(chipSelect)) {
    pomiaryBezKarty = 0;

    File plik = SD.open("dane.txt", FILE_WRITE);
    if (plik) {
      plik.print("Reset ");
      plik.print(licznikResetow);
      plik.print(" - Pomiar ");
      plik.print(licznikPomiarow);
      plik.print(": ");
      plik.print(temperatura);
      plik.print(" C, ");
      plik.print(cisnienie);
      plik.print(" hPa, ");
      plik.print(wysokosc);
      plik.println(" m");
      plik.close();

      Serial.print("Zapisano -> Reset ");
      Serial.print(licznikResetow);
      Serial.print(" - Pomiar ");
      Serial.print(licznikPomiarow);
      Serial.print(": ");
      Serial.print(temperatura);
      Serial.print(" C, ");
      Serial.print(cisnienie);
      Serial.print(" hPa, ");
      Serial.print(wysokosc);
      Serial.println(" m");
    }

    if (licznikPomiarow % 30 == 0) {
      Serial.println("30 pomiarów -> reset");
      File rf = SD.open("resety.txt", FILE_WRITE);
      if (rf) {
        licznikResetow++;
        rf.print("Reset ");
        rf.println(licznikResetow);
        rf.close();
      }
      delay(100);
      EEPROM.write(EEPROM_FLAG_ADDR, EEPROM_FLAG_NONE);
      asm volatile ("jmp 0");
    }
  } else {
    pomiaryBezKarty++;
    Serial.print("Brak karty SD! Pomiar ");
    Serial.println(licznikPomiarow);
    if (pomiaryBezKarty >= 3) {
      Serial.println("3 pomiary bez karty -> reset!");
      EEPROM.write(EEPROM_FLAG_ADDR, EEPROM_FLAG_CARD_MISSING);
      delay(100);
      asm volatile ("jmp 0");
    }
  }
}
