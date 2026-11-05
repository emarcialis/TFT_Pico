#include <Arduino.h>
#include <EEPROM.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>

// Configuration des broches pour Raspberry Pi Pico
constexpr uint8_t TFT_CS = 17;
constexpr uint8_t TFT_DC = 27;
constexpr uint8_t TFT_RST = 21;
constexpr uint8_t TFT_BL = 28;
constexpr uint32_t SERIAL_BAUD_RATE = 9600;

// Dimensions ecran en rotation 1
constexpr int16_t SCREEN_WIDTH = 320;
constexpr int16_t SCREEN_HEIGHT = 240;

// Positions Y
constexpr int16_t TITLE_Y = 10;
constexpr int16_t TABLE_Y = 44;

// Dimensions du tableau
constexpr int16_t NUM_COLS = 6;
constexpr int16_t NUM_ROWS_DISPLAY = 7;
constexpr int16_t CELL_WIDTH = SCREEN_WIDTH / NUM_COLS; // 53
constexpr int16_t CELL_HEIGHT = 22;

// Configuration pagination
constexpr int16_t TOTAL_DEVICES = 42;
constexpr int16_t DEVICES_PER_PAGE = 7;
constexpr int16_t MAX_PAGES = (TOTAL_DEVICES + DEVICES_PER_PAGE - 1) / DEVICES_PER_PAGE;
constexpr int16_t HISTORY_SIZE = 60; // 60 secondes de mesures (1 par seconde)
constexpr int16_t MINUTE_HISTORY_SIZE = 60; // 60 minutes (1 heure)
constexpr int16_t HOUR_HISTORY_SIZE = 24; // 24 heures (1 jour)

// Structure pour la sauvegarde en EEPROM (champs persistants uniquement)
typedef struct {
  float puissance;
  float total;
  uint16_t hourHistoryIndex;
  float une_heureHistory[HOUR_HISTORY_SIZE];
} AppSaveData;

// Taille totale pour la sauvegarde
constexpr size_t SAVE_DATA_SIZE = TOTAL_DEVICES * sizeof(AppSaveData);

// Structure pour un appareil
typedef struct {
  char Nom[32];
  float puissance;
  float une_minute;
  float une_heure;
  float une_journee;
  float total;
  time_t last_update;
  float puissanceHistory[HISTORY_SIZE];
  float une_minuteHistory[MINUTE_HISTORY_SIZE];
  float une_heureHistory[HOUR_HISTORY_SIZE];
  uint16_t puissanceHistoryIndex;
  uint16_t minuteHistoryIndex;
  uint16_t hourHistoryIndex;
  uint16_t secondsSinceLastMinute;
  uint16_t minutesSinceLastHour;
} App;

// Tableau des appareils en RAM
App devices[TOTAL_DEVICES];
uint8_t currentPage = 1;

Adafruit_ILI9341 tft(TFT_CS, TFT_DC, TFT_RST);

unsigned long lastLedToggle = 0;
unsigned long lastDisplayUpdate = 0;
bool ledState = false;
uint8_t backlightLevel = 100;
char commandBuffer[16];
size_t commandLength = 0;

// Fonction de log horodaté sur la liaison série
void log(const char* message) {
  unsigned long uptime = millis() / 1000; // secondes depuis démarrage
  char timestamp[15];
  uint8_t hours = uptime / 3600;
  uint8_t minutes = (uptime % 3600) / 60;
  uint8_t seconds = uptime % 60;
  snprintf(timestamp, sizeof(timestamp), "%02u:%02u:%02u", hours, minutes, seconds);
  Serial.print("[");
  Serial.print(timestamp);
  Serial.print("] ");
  Serial.println(message);
}

// Calcule la largeur du texte en pixels (6px par char en taille 1)
int16_t getTextWidth(const char* text, uint8_t size) {
  return strlen(text) * 6 * size;
}

// Affiche du texte centre
void drawCenteredText(const char *text, int16_t y, uint8_t textSize, uint16_t color) {
  tft.setTextSize(textSize);
  tft.setTextColor(color);
  int16_t textWidth = getTextWidth(text, textSize);
  int16_t x = (SCREEN_WIDTH - textWidth) / 2;
  tft.setCursor(x, y);
  tft.print(text);
}

// Affiche une cellule du tableau (texte centré)
void drawCell(const char* text, int16_t x, int16_t y, uint16_t bgColor, uint16_t textColor, uint8_t textSize = 1) {
  tft.fillRect(x, y, CELL_WIDTH, CELL_HEIGHT, bgColor);
  
  tft.setTextSize(textSize);
  tft.setTextColor(textColor);
  
  int16_t textWidth = strlen(text) * 6 * textSize;
  int16_t centeredX = x + (CELL_WIDTH - textWidth) / 2;
  int16_t textY = y + (CELL_HEIGHT - (8 * textSize)) / 2 + 2;
  
  tft.setCursor(centeredX, textY);
  tft.print(text);
}

// Dessine le tableau complet pour la page courante
void drawTable() {
  const char* headers[NUM_COLS] = {"Objets", "Puis.", "1m", "1h", "1j", "Total"};
  
  // Calculer l'index de depart
  int startIndex = (currentPage - 1) * DEVICES_PER_PAGE;
  
  // Dessiner les entetes
  for (uint8_t col = 0; col < NUM_COLS; col++) {
    drawCell(headers[col], col * CELL_WIDTH, TABLE_Y, ILI9341_BLUE, ILI9341_WHITE, 1);
  }
  
  // Dessiner les lignes de donnees pour la page courante
  char buffer[20];
  for (uint8_t row = 0; row < NUM_ROWS_DISPLAY && (startIndex + row) < TOTAL_DEVICES; row++) {
    int deviceIndex = startIndex + row;
    int16_t y = TABLE_Y + (row + 1) * CELL_HEIGHT;
    
    // Objets
    drawCell(devices[deviceIndex].Nom, 0, y, ILI9341_WHITE, ILI9341_BLACK, 1);
    
    // Puissance (en kW)
    snprintf(buffer, sizeof(buffer), "%f", devices[deviceIndex].puissance / 1000.0f);
    drawCell(buffer, CELL_WIDTH, y, ILI9341_WHITE, ILI9341_BLACK, 1);
    
    // Energie 1 minute
    snprintf(buffer, sizeof(buffer), "%.3f", devices[deviceIndex].une_minute);
    drawCell(buffer, 2 * CELL_WIDTH, y, ILI9341_WHITE, ILI9341_BLACK, 1);
    
    // Energie 1 heure
    snprintf(buffer, sizeof(buffer), "%.2f", devices[deviceIndex].une_heure);
    drawCell(buffer, 3 * CELL_WIDTH, y, ILI9341_WHITE, ILI9341_BLACK, 1);
    
    // Energie 1 jour
    snprintf(buffer, sizeof(buffer), "%.2f", devices[deviceIndex].une_journee);
    drawCell(buffer, 4 * CELL_WIDTH, y, ILI9341_WHITE, ILI9341_BLACK, 1);
    
    // Energie Totale
    snprintf(buffer, sizeof(buffer), "%.2f", devices[deviceIndex].total);
    drawCell(buffer, 5 * CELL_WIDTH, y, ILI9341_WHITE, ILI9341_BLACK, 1);
  }
  
  // Dessiner les separateurs noirs
  for (uint8_t col = 1; col < NUM_COLS; col++) {
    int16_t x = col * CELL_WIDTH;
    tft.drawFastVLine(x, TABLE_Y, CELL_HEIGHT * (NUM_ROWS_DISPLAY + 1), ILI9341_BLACK);
  }
  
  for (uint8_t row = 0; row <= NUM_ROWS_DISPLAY; row++) {
    int16_t y = TABLE_Y + row * CELL_HEIGHT;
    tft.drawFastHLine(0, y, SCREEN_WIDTH, ILI9341_BLACK);
  }
  
  // Afficher l'indicateur de page
  snprintf(buffer, sizeof(buffer), "Page %d/%d", currentPage, MAX_PAGES);
  tft.fillRect(0, SCREEN_HEIGHT - 20, SCREEN_WIDTH, 20, ILI9341_BLACK);
  drawCenteredText(buffer, SCREEN_HEIGHT - 10, 1, ILI9341_WHITE);
}

// Met a jour UNIQUEMENT le contenu des cellules de donnees (sans toucher aux separateurs et titres)
void updateTableData() {
  int startIndex = (currentPage - 1) * DEVICES_PER_PAGE;
  
  char buffer[20];
  for (uint8_t row = 0; row < NUM_ROWS_DISPLAY && (startIndex + row) < TOTAL_DEVICES; row++) {
    int deviceIndex = startIndex + row;
    int16_t y = TABLE_Y + (row + 1) * CELL_HEIGHT;
    
    // Puissance (colonne 1) en kW
    dtostrf(devices[deviceIndex].puissance , 5, 0, buffer);
    tft.fillRect(CELL_WIDTH + 1, y + 1, CELL_WIDTH - 2, CELL_HEIGHT - 2, ILI9341_WHITE);
    tft.setTextSize(1);
    tft.setTextColor(ILI9341_BLACK);
    int16_t textWidth = strlen(buffer) * 6;
    int16_t x = CELL_WIDTH + (CELL_WIDTH - textWidth) / 2;
    tft.setCursor(x, y + (CELL_HEIGHT - 8) / 2 + 2);
    tft.print(buffer);

    // Energie 1 minute (colonne 2)
    dtostrf(devices[deviceIndex].une_minute, 5, 3, buffer);
    tft.fillRect(2 * CELL_WIDTH + 1, y + 1, CELL_WIDTH - 2, CELL_HEIGHT - 2, ILI9341_WHITE);
    textWidth = strlen(buffer) * 6;
    x = 2 * CELL_WIDTH + (CELL_WIDTH - textWidth) / 2;
    tft.setCursor(x, y + (CELL_HEIGHT - 8) / 2 + 2);
    tft.print(buffer);
    
    // Energie 1 heure (colonne 3)
    dtostrf(devices[deviceIndex].une_heure, 5, 2, buffer);
    tft.fillRect(3 * CELL_WIDTH + 1, y + 1, CELL_WIDTH - 2, CELL_HEIGHT - 2, ILI9341_WHITE);
    textWidth = strlen(buffer) * 6;
    x = 3 * CELL_WIDTH + (CELL_WIDTH - textWidth) / 2;
    tft.setCursor(x, y + (CELL_HEIGHT - 8) / 2 + 2);
    tft.print(buffer);
    
    // Energie 1 jour (colonne 4)
    dtostrf(devices[deviceIndex].une_journee, 5, 2, buffer);
    tft.fillRect(4 * CELL_WIDTH + 1, y + 1, CELL_WIDTH - 2, CELL_HEIGHT - 2, ILI9341_WHITE);
    textWidth = strlen(buffer) * 6;
    x = 4 * CELL_WIDTH + (CELL_WIDTH - textWidth) / 2;
    tft.setCursor(x, y + (CELL_HEIGHT - 8) / 2 + 2);
    tft.print(buffer);
    
    // Energie Totale (colonne 5)
    dtostrf(devices[deviceIndex].total, 5, 2, buffer);
    tft.fillRect(5 * CELL_WIDTH + 1, y + 1, CELL_WIDTH - 2, CELL_HEIGHT - 2, ILI9341_WHITE);
    textWidth = strlen(buffer) * 6;
    x = 5 * CELL_WIDTH + (CELL_WIDTH - textWidth) / 2;
    tft.setCursor(x, y + (CELL_HEIGHT - 8) / 2 + 2);
    tft.print(buffer);
  }
}

void printHelp() {
  Serial.println("Commandes disponibles :");
  Serial.println("  h       : afficher cette aide");
  Serial.println("  t       : afficher toutes les valeurs des appareils");
  Serial.println("  c0..255 : regler la luminosite du retroeclairage PWM");
  Serial.println("            0 = eteint, 255 = luminosite maximale");
  Serial.println("  p1..p6  : changer de page (1-6)");
  Serial.println("  d X Y   : mettre a jour puissance de l'appareil X (1-42) avec valeur Y");
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

  if (command[0] == 'p' && command[1] != '\0') {
    uint8_t page = command[1] - '0';
    if (page >= 1 && page <= MAX_PAGES) {
      currentPage = page;
      drawTable();
      Serial.print("Page ");
      Serial.println(currentPage);
      return;
    }
  }

  if (command[0] == 't' && command[1] == '\0') {
    for (int i = 0; i < TOTAL_DEVICES; i++) {
      Serial.print("Device ");
      Serial.print(i + 1);
      Serial.print(": Nom=");
      Serial.print(devices[i].Nom);
      Serial.print(", Puis=");
      Serial.print(devices[i].puissance / 1000.0f);
      Serial.print("kW, 1m=");
      Serial.print(devices[i].une_minute);
      Serial.print("kWh, 1h=");
      Serial.print(devices[i].une_heure);
      Serial.print("kWh, 1j=");
      Serial.print(devices[i].une_journee);
      Serial.print("kWh, Total=");
      Serial.print(devices[i].total);
      Serial.println("kWh");
    }
    return;
  }

  if (command[0] == 'd' && command[1] == ' ') {
    const char* cmd_ptr = command + 2;
    char* end;
    char* value_end;
    
    long cmdIndex = strtol(cmd_ptr, &end, 10);
    if (cmd_ptr == end || cmdIndex < 1 || cmdIndex > TOTAL_DEVICES) {
      Serial.println("Index invalide. Utilisez 1-42.");
      return;
    }
    
    while (*end == ' ') end++;
    
    float value = strtof(end, &value_end);
    if (end == value_end) {
      Serial.println("Valeur invalide.");
      return;
    }
    
    size_t index = cmdIndex - 1;
    devices[index].puissance = value;
    devices[index].last_update = time(nullptr);
    
    int startIndex = (currentPage - 1) * DEVICES_PER_PAGE;
    if (index >= startIndex && index < startIndex + NUM_ROWS_DISPLAY) {
      updateTableData();
    }
    
    Serial.print("Appareil ");
    Serial.print(cmdIndex);
    Serial.print(" puissance = ");
    Serial.println(value);
    return;
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

// Sauvegarde un appareil en EEPROM
void saveDeviceToEEPROM(int index) {
  size_t offset = index * sizeof(AppSaveData);
  AppSaveData saveData;
  
  saveData.puissance = devices[index].puissance;
  saveData.total = devices[index].total;
  saveData.hourHistoryIndex = devices[index].hourHistoryIndex;
  
  for (int j = 0; j < HOUR_HISTORY_SIZE; j++) {
    saveData.une_heureHistory[j] = devices[index].une_heureHistory[j];
  }
  
  EEPROM.put(offset, saveData);
}

// Sauvegarde tous les appareils en EEPROM
void saveAllToEEPROM() {
  for (int i = 0; i < TOTAL_DEVICES; i++) {
    saveDeviceToEEPROM(i);
  }
}

// Charge un appareil depuis EEPROM avec validation des données
void loadDeviceFromEEPROM(int index) {
  size_t offset = index * sizeof(AppSaveData);
  AppSaveData saveData;
  
  EEPROM.get(offset, saveData);
  
  // Valider et charger puissance (doit être > 0 et < 20000)
  if (saveData.puissance == saveData.puissance && saveData.puissance > 100.0f && saveData.puissance < 20000.0f) {
    devices[index].puissance = saveData.puissance;
  } else {
    devices[index].puissance = 1000.0 + (index * 50.0);
  }
  
  // Valider et charger total (doit être >= 0 et raisonnable)
  if (saveData.total == saveData.total && saveData.total >= 0.0f && saveData.total < 100000.0f) {
    devices[index].total = saveData.total;
  } else {
    devices[index].total = 0.0;
  }
  
  devices[index].hourHistoryIndex = saveData.hourHistoryIndex % HOUR_HISTORY_SIZE;
  
  // Charger et valider les historiques d'énergie heure
  for (int j = 0; j < HOUR_HISTORY_SIZE; j++) {
    float val = saveData.une_heureHistory[j];
    if (val == val && val >= 0.0f && val < 1000.0f) {
      devices[index].une_heureHistory[j] = val;
    } else {
      devices[index].une_heureHistory[j] = 0.0;
    }
  }
}

// Charge tous les appareils depuis EEPROM
void loadAllFromEEPROM() {
  for (int i = 0; i < TOTAL_DEVICES; i++) {
    loadDeviceFromEEPROM(i);
  }
}

// Initialisation des données de test
void initDeviceData() {
  // Vérifier si l'EEPROM contient des données valides
  // L'EEPROM non initialisée contient 0xFF, ce qui donne NaN pour un float
  // Vérification portable : NaN != NaN, et on vérifie aussi la plage valide
  float testPuissance;
  EEPROM.get(0, testPuissance);
  // Un float NaN a la propriété que NaN != NaN
  bool isValidPuissance = (testPuissance == testPuissance) && 
                          (testPuissance > 100.0f) && 
                          (testPuissance < 20000.0f);
  
  if (isValidPuissance) {
    // Charger les données sauvegardées depuis EEPROM
    loadAllFromEEPROM();
    log("Données chargées depuis EEPROM.");
  } else {
    // Première initialisation - initialiser les valeurs persistantes
    for (int i = 0; i < TOTAL_DEVICES; i++) {
      devices[i].puissance = 1000.0 + (i * 50.0);
      devices[i].total = 0.0;
      devices[i].hourHistoryIndex = 0;
      for (int j = 0; j < HOUR_HISTORY_SIZE; j++) {
        devices[i].une_heureHistory[j] = 0.0;
      }
    }
  }
  
  // Initialisation commune pour tous les appareils (après chargement ou première init)
  for (int i = 0; i < TOTAL_DEVICES; i++) {
    // Nom
    snprintf(devices[i].Nom, sizeof(devices[i].Nom), "Objet %02d", i + 1);
    
    // Initialiser puissanceHistory (non sauvegardé en EEPROM)
    for (int j = 0; j < HISTORY_SIZE; j++) {
      devices[i].puissanceHistory[j] = 0.0;
    }
    devices[i].puissanceHistoryIndex = 0;
    
    // Initialiser minuteHistory (non sauvegardé en EEPROM)
    devices[i].minuteHistoryIndex = 0;
    for (int j = 0; j < MINUTE_HISTORY_SIZE; j++) {
      devices[i].une_minuteHistory[j] = 0.0;
    }
    
    // Réinitialiser les valeurs temporaires au démarrage
    devices[i].une_minute = 0.0;
    devices[i].une_heure = 0.0;
    devices[i].une_journee = 0.0;
    devices[i].secondsSinceLastMinute = 0;
    devices[i].minutesSinceLastHour = 0;
    devices[i].last_update = time(nullptr);
  }
}

void setup() {
  Serial.begin(SERIAL_BAUD_RATE);

  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);

  pinMode(TFT_BL, OUTPUT);
  analogWrite(TFT_BL, backlightLevel);

  // Initialiser l'EEPROM avec la taille nécessaire
  EEPROM.begin(SAVE_DATA_SIZE);

  tft.begin();
  tft.setRotation(1);
  tft.fillScreen(ILI9341_BLACK);

  initDeviceData();

  drawCenteredText("Suivi Conso", TITLE_Y + 5, 2, ILI9341_RED);
  drawTable();

  lastLedToggle = millis();
  lastDisplayUpdate = lastLedToggle;
  printHelp();
}

void loop() {
  processSerial();

  const unsigned long now = millis();

  if (now - lastLedToggle >= 125) {
    lastLedToggle = now;
    ledState = !ledState;
    digitalWrite(LED_BUILTIN, ledState ? HIGH : LOW);
  }

  if (now - lastDisplayUpdate >= 1000) {
    lastDisplayUpdate = now;
    
    // Mettre a jour les histories et calculer les énergies
    for (int i = 0; i < TOTAL_DEVICES; i++) {
      devices[i].secondsSinceLastMinute++;
      
      // Stocker la puissance actuelle dans l'historique circulaire (1 valeur par seconde)
      devices[i].puissanceHistory[devices[i].puissanceHistoryIndex] = devices[i].puissance;
      devices[i].puissanceHistoryIndex = (devices[i].puissanceHistoryIndex + 1) % HISTORY_SIZE;
      
      
      // Calculer une_minute: energie sur les 60 dernieres secondes
      // Somme des puissances (W) * 1 seconde / 3600000 = kWh
      float sum = 0.0f;
      for (int j = 0; j < HISTORY_SIZE; j++) {
        sum += devices[i].puissanceHistory[j];
      }
      devices[i].une_minute = sum / 3600000.0f;
           
      // Calculer total: intégration continue
      devices[i].total += devices[i].puissance / 3600000.0f;
      
      // Toutes les 60 secondes (1 minute)
      if (devices[i].secondsSinceLastMinute >= 60) {
        devices[i].secondsSinceLastMinute = 0;
        devices[i].minutesSinceLastHour++;
        
        // Stocker la valeur de une_minute dans le barillet minute
        devices[i].une_minuteHistory[devices[i].minuteHistoryIndex] = devices[i].une_minute;
        devices[i].minuteHistoryIndex = (devices[i].minuteHistoryIndex + 1) % MINUTE_HISTORY_SIZE;
         
        for (int j = 0; j < HISTORY_SIZE; j++) {
          devices[i].puissanceHistory[j] = 0.0;
        }
        // Sauvegarder en EEPROM toutes les minutes
        saveDeviceToEEPROM(i);
        
        // Calculer une_heure: somme des 60 dernières valeurs de une_minuteHistory
        float hourSum = 0.0f;
        for (int j = 0; j < MINUTE_HISTORY_SIZE; j++) {
          hourSum += devices[i].une_minuteHistory[j];
        }
        devices[i].une_heure = hourSum;
        
        // Toutes les 60 minutes (1 heure)
        if (devices[i].minutesSinceLastHour >= 60) {
          devices[i].minutesSinceLastHour = 0;
          
          // Stocker la valeur de une_heure dans le barillet heure
          devices[i].une_heureHistory[devices[i].hourHistoryIndex] = devices[i].une_heure;
          devices[i].hourHistoryIndex = (devices[i].hourHistoryIndex + 1) % HOUR_HISTORY_SIZE;
          
          // Calculer une_journee: somme des 24 dernières valeurs de une_heureHistory
          float daySum = 0.0f;
          for (int j = 0; j < HOUR_HISTORY_SIZE; j++) {
            daySum += devices[i].une_heureHistory[j];
          }
          devices[i].une_journee = daySum;
        }
      }
    }
    
    updateTableData();
  }
}
