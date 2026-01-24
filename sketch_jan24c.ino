#include <EEPROM.h>
#include "Key.h"
#include "SimpleHOTP.h"

#define SECRET_SIZE 20
#define MAX_RECORDS (1024 / SECRET_SIZE) // ~51 запись
#define FIXED_TIME 1769272850ULL // фиксированное время для генерации UID

String pad6(uint32_t num) {
  String s = String(num);
  while (s.length() < 8) s = "0" + s;
  return s;
}

uint8_t pseudoRandomByte() {
  uint8_t val = 0;
  for (int i = 0; i < 8; i++) {
    val |= (analogRead(A0) & 1) << i;
    delayMicroseconds(50);
  }
  return val;
}

void generateSecret(uint8_t* out) {
  for (int i = 0; i < SECRET_SIZE; i++) {
    out[i] = pseudoRandomByte();
  }
}

const char base32Alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";
void base32Encode(const uint8_t* data, size_t len, char* output) {
  int bits = 0, value = 0, index = 0;
  for (size_t i = 0; i < len; i++) {
    value = (value << 8) | data[i];
    bits += 8;
    while (bits >= 5) {
      bits -= 5;
      output[index++] = base32Alphabet[(value >> bits) & 0x1F];
    }
  }
  if (bits > 0) {
    output[index++] = base32Alphabet[(value << (5 - bits)) & 0x1F];
  }
  while (index % 8 != 0) output[index++] = '=';
  output[index] = '\0';
}

// Чтение/запись блока в EEPROM
void eepromReadBytes(int addr, uint8_t* buffer, size_t len) {
  for (size_t i = 0; i < len; i++) buffer[i] = EEPROM.read(addr + i);
}
void eepromWriteBytes(int addr, const uint8_t* buffer, size_t len) {
  for (size_t i = 0; i < len; i++) EEPROM.write(addr + i, buffer[i]);
}

// Проверка, пустая ли ячейка
bool isSlotFree(int index) {
  for (int i = 0; i < SECRET_SIZE; i++) {
    if (EEPROM.read(index * SECRET_SIZE + i) != 0xFF) return false;
  }
  return true;
}

// Сохранение только секрета
bool saveSecret(const uint8_t* secret) {
  for (int i = 0; i < MAX_RECORDS; i++) {
    if (isSlotFree(i)) {
      eepromWriteBytes(i * SECRET_SIZE, secret, SECRET_SIZE);
      return true;
    }
  }
  return false;
}

// Генерация TOTP через HOTP
uint32_t generateTOTP(const uint8_t* secret, size_t len, uint64_t unixTime) {
  uint64_t counter = unixTime / 30;
  Key key(const_cast<uint8_t*>(secret), len);
  SimpleHOTP hotp(key, counter);
  hotp.setDigits(8);
  return hotp.generateHOTP();
}

// Список и удаление
void listAndDeleteUIDs() {
  Serial.println("\n--- Список UID ---");
  int count = 0;
  for (int i = 0; i < MAX_RECORDS; i++) {
    if (!isSlotFree(i)) {
      uint8_t secret[SECRET_SIZE];
      eepromReadBytes(i * SECRET_SIZE, secret, SECRET_SIZE);
      uint32_t uid = generateTOTP(secret, SECRET_SIZE, FIXED_TIME);
      Serial.print(count + 1);
      Serial.print(". UID: ");
      Serial.print(pad6(uid));
      Serial.print(" (запись #");
      Serial.print(i);
      Serial.println(")");
      count++;
    }
  }

  if (count == 0) {
    Serial.println("Нет сохранённых UID.");
    return;
  }

  while (true){
    Serial.println("\nУдалить запись? Введите номер (1-" + String(count) + ") или 0 для отмены:");
    while (!Serial.available()) delay(10);
    String input = Serial.readStringUntil('\n');
    int choice = input.toInt();

    if (choice >= 1 && choice <= count) {
      int realIndex = -1;
      int found = 0;
      for (int i = 0; i < MAX_RECORDS; i++) {
        if (!isSlotFree(i)) {
          found++;
          if (found == choice) {
            realIndex = i;
            break;
          }
        }
      }

      if (realIndex >= 0) {
        for (int j = 0; j < SECRET_SIZE; j++) {
          EEPROM.write(realIndex * SECRET_SIZE + j, 0xFF);
        }
        Serial.println("✅ Запись удалена.");
      }
    } else {
      Serial.println("Отмена.");
      break;
    }
  }
}

void setup() {
  Serial.begin(9600);
  while (!Serial);
  Serial.println("🔐 Send 0 to create UID, or existing UID to use it.");
}

void loop() {
  if (Serial.available()) {
    String input = Serial.readStringUntil('\n');
    input.trim();
    if (input.length() == 0) return;

    long choice = input.toInt();

    if (choice == 0) {
      uint8_t newSecret[SECRET_SIZE];
      generateSecret(newSecret);

      uint32_t uid = generateTOTP(newSecret, SECRET_SIZE, FIXED_TIME);

      // Проверка уникальности UID
      bool exists = false;
      for (int i = 0; i < MAX_RECORDS && !exists; i++) {
        if (!isSlotFree(i)) {
          uint8_t existing[SECRET_SIZE];
          eepromReadBytes(i * SECRET_SIZE, existing, SECRET_SIZE);
          uint32_t existingUID = generateTOTP(existing, SECRET_SIZE, FIXED_TIME);
          if (existingUID == uid) exists = true;
        }
      }

      if (exists) {
        Serial.println("⚠️ UID collision! Try again.");
        return;
      }

      if (!saveSecret(newSecret)) {
        Serial.println("❌ EEPROM full!");
        return;
      }

      char b32[36];
      base32Encode(newSecret, SECRET_SIZE, b32);
      Serial.println("✅ New UID: " + pad6(uid));
      Serial.print("🔑 Key: ");
      Serial.println(b32);

    } else if (choice == 1234567) {
      listAndDeleteUIDs();

    } else if (choice > 0 && choice <= 999999) {
      uint32_t targetUID = (uint32_t)choice;

      Serial.print("Enter Unix timestamp: ");
      while (!Serial.available()) delay(10);
      String timeInput = Serial.readStringUntil('\n');
      unsigned long unixTime = timeInput.toInt();

      if (unixTime < 1000000000UL) {
        Serial.println("❌ Invalid time!");
        return;
      }

      bool foundAny = false;
      for (int i = 0; i < MAX_RECORDS; i++) {
        if (!isSlotFree(i)) {
          uint8_t secret[SECRET_SIZE];
          eepromReadBytes(i * SECRET_SIZE, secret, SECRET_SIZE);
          uint32_t uid = generateTOTP(secret, SECRET_SIZE, FIXED_TIME);

          if (uid == targetUID) {
            uint32_t totp = generateTOTP(secret, SECRET_SIZE, unixTime);
            Serial.print("🔢 TOTP for UID ");
            Serial.print(pad6(uid));
            Serial.print(" (key #");
            Serial.print(i);
            Serial.print("): ");
            Serial.println(pad6(totp));
            foundAny = true;
          }
        }
      }

      if (!foundAny) {
        Serial.println("❌ UID not found!");
      }

    } else {
      Serial.println("❌ Send 0 or 6-digit UID.");
    }
  }
  delay(100);
}