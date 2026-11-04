#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <stdio.h>
#include <stdlib.h>

// Configuration des broches pour Raspberry Pi Pico
constexpr uint8_t TFT_CS = 17;     // Broche CS pour l'ecran TFT
constexpr uint8_t TFT_DC = 27;     // Broche DC pour l'ecran TFT
constexpr uint8_t TFT_RST = 21;    // Broche RST pour l'ecran TFT
constexpr uint8_t TFT_BL = 28;     // Broche PWM pour le retroeclairage
constexpr uint32_t SERIAL_BAUD_RATE = 9600;

Adafruit_ILI9341 tft(TFT_CS, TFT_DC, TFT_RST);

unsigned long lastLedToggle = 0;
unsigned long lastCounterUpdate = 0;
unsigned long counter = 0;
bool ledState = false;
uint8_t backlightLevel = 100;
char commandBuffer[16];
size_t commandLength = 0;

void showText(const char *text, int16_t x, int16_t y, uint8_t textSize,
              uint16_t color) {
  Serial.print("TFT: ");
  Serial.println(text);
  tft.setCursor(x, y);
  tft.setTextSize(textSize);
  tft.setTextColor(color);
  tft.println(text);
}

void drawCounter() {
  char text[24];
  snprintf(text, sizeof(text), "%lu", counter);
  tft.fillRect(24, 145, 272, 60, ILI9341_BLACK);
  showText(text, 24, 155, 4, ILI9341_CYAN);
}

void printHelp() {
  Serial.println("Commandes disponibles :");
  Serial.println("  h       : afficher cette aide");
  Serial.println("  c0..255 : regler la luminosite du retroeclairage PWM");
  Serial.println("            0 = eteint, 255 = luminosite maximale");
}

void handleCommand(const char *command) {
  Serial.print("Commande recue: ");
  Serial.println(command);

  if (command[0] == 'h' && command[1] == '\0') {
    printHelp();
    return;
  }

  if (command[0] == 'c' && command[1] != '\0') {
    char *end = nullptr;
    const long value = strtol(command + 1, &end, 10);
    if (*end == '\0' && end != command + 1 && value >= 0 && value <= 255) {
      backlightLevel = static_cast<uint8_t>(value);
      analogWrite(TFT_BL, backlightLevel);
      Serial.print("Retro eclairage PWM regle a ");
      Serial.println(backlightLevel);
      return;
    }
  }

  Serial.println("Commande invalide. Envoyez 'h' pour afficher l'aide.");
}

void processSerial() {
  while (Serial.available() > 0) {
    const char incoming = static_cast<char>(Serial.read());

    if (incoming == '\r' || incoming == '\n') {
      if (commandLength > 0) {
        commandBuffer[commandLength] = '\0';
        handleCommand(commandBuffer);
        commandLength = 0;
      }
    } else if (commandLength < sizeof(commandBuffer) - 1) {
      commandBuffer[commandLength++] = incoming;
    } else {
      commandLength = 0;
      Serial.println("Commande trop longue. Envoyez 'h' pour afficher l'aide.");
    }
  }
}

void setup() {
  Serial.begin(SERIAL_BAUD_RATE);
  
  // Initialisation de la LED integree
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);
  
  // Initialisation du retroeclairage PWM
  pinMode(TFT_BL, OUTPUT);
  analogWrite(TFT_BL, backlightLevel);

  // Initialisation de l'ecran TFT
  tft.begin();
  tft.setRotation(1);
  tft.fillScreen(ILI9341_BLACK);

  // Affichage initial
  showText("ILI9341", 24, 20, 3, ILI9341_BLUE);
  showText("Raspberry Pi Pico2W", 24, 60, 2, ILI9341_WHITE);

  showText("resolution : 320x240", 24, 80, 2, ILI9341_WHITE);
  showText("Compteur (secondes) :", 24, 120, 1, ILI9341_GREEN);
  drawCounter();

  lastLedToggle = millis();
  lastCounterUpdate = lastLedToggle;
  printHelp();
}

void loop() {
  processSerial();

  const unsigned long now = millis();

  // Clignotement de la LED integree
  if (now - lastLedToggle >= 125) {
    lastLedToggle = now;
    ledState = !ledState;
    digitalWrite(LED_BUILTIN, ledState ? HIGH : LOW);
  }

  // Mise a jour du compteur toutes les secondes
  if (now - lastCounterUpdate >= 1000) {
    lastCounterUpdate = now;
    ++counter;
    drawCounter();
  }
}
