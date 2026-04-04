#include <EEPROM.h>          // Подключение библиотеки для работы с энергонезависимой памятью Arduino
#include "Key.h"             // Подключение пользовательской библиотеки для работы с криптографическими ключами
#include "SimpleHOTP.h"      // Подключение библиотеки для генерации HOTP/TOTP кодов
#include <SoftwareSerial.h> // Подключение библиотеки для программного создания последовательного портa
// #include <CustomJWT.h>
#include "qrcode.h"

// === ПИНЫ ===
#define BT_RX_PIN      2     // Определение пина 2 как приёмника (RX) для связи с модулем HC-05 (Arduino RX → HC-05 TX)
#define BT_TX_PIN      3     // Определение пина 3 как передатчика (TX) для связи с модулем HC-05 (Arduino TX → HC-05 RX)
#define BT_POWER_PIN  10     // Определение пина 10 для управления питанием модуля HC-05 (вместо прямого подключения к 3.3V)
#define RED_PIN        9
#define YELLOW_PIN     8
#define LOCKER_PIN    12

SoftwareSerial btSerial(BT_RX_PIN, BT_TX_PIN);  // Создание программного последовательного порта для общения с Bluetooth модулем

// === КОНСТАНТЫ ===
#define SECRET_SIZE  20      // Размер секретного ключа в байтах (20 байт = 160 бит)
#define MAX_RECORDS  51      // Максимальное количество записей ключей в EEPROM (51 запись × 20 байт = 1020 байт)
#define FIXED_TIME   1769272850ULL  // Фиксированное временное значение для генерации тестовых UID (Unix timestamp)
#define JWT_KEY      "QHYD4M44ZZYHYD7AH7777774B6APQ7HG"
#define BASE_32      "YBEZELDNIYDHBJSVKW22UVUN75RZMNRJ"

// Правильный ключ из Base32: YBEZELDNIYDHBJSVKW22UVUN75RZMNRJ
const uint8_t FIXED_KEY_BYTES[SECRET_SIZE] PROGMEM = {  // Хранение секретного ключа в программной памяти (экономия RAM)
  216, 225, 113, 46, 201, 147, 103, 205,  // Байтовое представление секретного ключа (раскодировано из Base32)
  155, 54, 108, 217, 179, 102, 205, 155,  // Каждое число — один байт ключа
  54, 108, 217, 179                       // Всего 20 байт
};



// CustomJWT jwt(JWT_KEY, 256);

// === ПЕЧАТЬ С ВЕДУЩИМИ НУЛЯМИ ===
void printPadded(uint32_t num, uint8_t digits) {  // Функция для вывода числа с ведущими нулями (для красивого форматирования кодов)
  char buf[11];                                  // Буфер для формирования строки (макс. 10 цифр + терминатор)
  if (digits == 8) sprintf(buf, "%08lu", num);   // Форматирование 8-значного числа (например, 00123456)
  else if (digits == 4) sprintf(buf, "%04lu", num);  // Форматирование 4-значного числа (например, 0123)
  else sprintf(buf, "%lu", num);                 // Обычное форматирование без ведущих нулей
  Serial.print(buf);                             // Вывод отформатированной строки в монитор порта
}

uint32_t generateTOTP8(const uint8_t* secret, size_t len, uint64_t unixTime) {  // Генерация 8-значного TOTP кода
  uint64_t counter = unixTime;                    // Использование Unix-времени как счётчика для TOTP
  Key key(const_cast<uint8_t*>(secret), len);     // Создание объекта ключа из байтового массива
  SimpleHOTP hotp(key, counter);                  // Инициализация генератора HOTP с заданным ключом и счётчиком
  hotp.setDigits(8);                              // Установка длины кода в 8 цифр
  return hotp.generateHOTP();                     // Генерация и возврат итогового кода
}

// === ОТПРАВКА AT-КОМАНДЫ С ОТЛАДКОЙ ===
bool sendATCommand(Stream& serial, const char* cmd, unsigned long timeout = 2000) {  // Отправка AT-команды в режиме настройки HC-05
  serial.print(cmd);                              // Отправка текста команды в последовательный порт
  serial.write('\r');                             // Добавление символа возврата каретки (обязательно для HC-05)
  serial.write('\n');                             // Добавление символа новой строки (обязательно для HC-05)
  delay(800);                                     // Задержка для обработки команды модулем
  
  unsigned long start = millis();                 // Запоминание времени начала ожидания ответа
  String response = "";                           // Буфер для накопления ответа от модуля
  
  while (millis() - start < timeout) {            // Цикл ожидания ответа в течение заданного таймаута
    if (serial.available()) {                     // Если есть доступные данные в порту
      char c = serial.read();                     // Чтение одного символа
      response += c;                              // Добавление символа в буфер ответа
      Serial.write(c);                            // Одновременный вывод символа в монитор порта для отладки
    }
    delay(5);                                     // Короткая задержка для снижения нагрузки на процессор
  }
  
  Serial.println();                               // Переход на новую строку после завершения вывода ответа
  return response.indexOf("OK") >= 0;             // Проверка наличия "OK" в ответе (успешное выполнение команды)
}

// === СБРОС МОДУЛЯ ЧЕРЕЗ УПРАВЛЕНИЕ ПИТАНИЕМ ===
void resetBluetoothModule() {                     // Функция полного сброса модуля HC-05 через управление питанием
  Serial.println(F("\n🔄 СБРОС BLUETOOTH МОДУЛЯ..."));  // Вывод сообщения (F() экономит RAM, размещая строку во флеш-памяти)
  digitalWrite(RED_PIN, HIGH);
  digitalWrite(YELLOW_PIN, HIGH);
  
  // Полное отключение питания
  pinMode(BT_POWER_PIN, OUTPUT);                  // Настройка пина управления питанием как выход
  digitalWrite(BT_POWER_PIN, LOW);                // Отключение питания модуля (лог. 0 на пине)
  delay(400);                                     // Задержка для полной разрядки конденсаторов модуля
  digitalWrite(YELLOW_PIN, LOW);
  // Включение питания
  digitalWrite(BT_POWER_PIN, HIGH);               // Включение питания модуля (лог. 1 на пине)
  delay(1800);                                    // Задержка для полной инициализации модуля после включения
  digitalWrite(YELLOW_PIN, HIGH);
  // Перезапуск соединения
  btSerial.end();                                 // Прекращение текущего программного последовательного соединения
  btSerial.begin(38400);                           // Перезапуск соединения на скорости 9600 бод
  delay(300);                                     // Короткая задержка для стабилизации соединения
  digitalWrite(YELLOW_PIN, LOW);
  Serial.println(F("✅ Модуль перезагружен. Требуется новое подключение.\n"));  // Подтверждение успешного сброса
  digitalWrite(RED_PIN, LOW);
}

// === НАСТРОЙКА HC-05 (ручной режим AT) ===
void configureHC05() {                            // Функция настройки параметров Bluetooth модуля HC-05
  // (Закомментированные инструкции для ручного входа в режим AT — не используются в автоматическом режиме)
  digitalWrite(RED_PIN, LOW);
  // Включаем питание модуля
  pinMode(BT_POWER_PIN, OUTPUT);                  // Настройка пина управления питанием как выход
  digitalWrite(BT_POWER_PIN, HIGH);               // Подача питания на модуль
  delay(1500);                                    // Задержка для инициализации модуля после включения
  
  const long speeds[] = {38400, 9600};            // Массив возможных скоростей для определения режима AT
  bool atMode = false;                            // Флаг успешного входа в режим AT
  
  for (int i = 0; i < 2 && !atMode; i++) {        // Перебор двух возможных скоростей
    Serial.print(F("📡 Пробую скорость "));       // Вывод текущей проверяемой скорости
    Serial.print(speeds[i]);
    Serial.println(F("..."));
    
    btSerial.end();                               // Остановка текущего соединения
    btSerial.begin(speeds[i]);                    // Запуск соединения на проверяемой скорости
    delay(1000);                                  // Задержка для стабилизации соединения
    
    Serial.print(F("→ AT: "));                    // Отправка базовой AT-команды для проверки режима
    if (sendATCommand(btSerial, "AT")) {          // Если модуль ответил "OK"
      Serial.println(F("✅ Режим AT активен"));   // Подтверждение входа в режим настройки
      atMode = true;                              // Установка флага успешного входа
      
      Serial.print(F("→ AT+NAME=BLE_LOCKER: "));  // Установка имени устройства
      sendATCommand(btSerial, "AT+NAME=BLE_LOCKER");  // Отправка команды смены имени
      
      uint8_t fixedKeyRam[SECRET_SIZE];           // Буфер для копирования ключа из PROGMEM в RAM
      memcpy_P(fixedKeyRam, FIXED_KEY_BYTES, SECRET_SIZE);  // Копирование ключа из флеш-памяти в оперативную
      uint32_t fullCode = generateTOTP8(fixedKeyRam, SECRET_SIZE, FIXED_TIME);  // Генерация 8-значного кода на основе фиксированного времени
      if (fullCode == 0 || fullCode == 4294967295UL) fullCode = 3627;  // Обработка ошибочных значений кода
      uint32_t pin4 = fullCode % 10000;           // Получение 4-значного PIN из 8-значного кода
      
      char cmd[20];                               // Буфер для формирования AT-команды смены пароля
      sprintf(cmd, "AT+PSWD=%04lu", pin4);        // Формирование команды вида "AT+PSWD=XXXX"
      Serial.print(F("→ "));                      // Вывод команды в монитор порта
      Serial.print(cmd);
      Serial.print(F(": "));
      if (sendATCommand(btSerial, cmd)) {         // Отправка команды установки пароля
        Serial.println(F("✅ Пароль установлен")); // Подтверждение успешной установки
      } else {
        Serial.println(F("⚠️ Предупреждение: пароль может не примениться без перезагрузки"));  // Предупреждение при ошибке
      }
      
      Serial.print(F("→ AT+UART=9600,0,0: "));    // Установка параметров UART (скорость 9600, 1 стоп-бит, без контроля чётности)
      sendATCommand(btSerial, "AT+UART=9600,0,0");  // Отправка команды настройки UART
      
      Serial.print(F("\n🔑 Финальный пароль: ")); // Вывод итогового 4-значного пароля для сопряжения
      printPadded(pin4, 4);
      Serial.println(F("\n✅ Настройка завершена!"));  // Подтверждение завершения настройки
      
      // Сбрасываем модуль для применения настроек
      // resetBluetoothModule();                     // Перезагрузка модуля для применения всех изменений
      break;                                      // Выход из цикла перебора скоростей
    }
  }
  
  if (!atMode) {                                  // Если режим AT не был активирован ни на одной скорости
    Serial.println(F("\n❌ Модуль не отвечает в режиме AT"));  // Сообщение об ошибке
    Serial.println(F("   Проверьте:"));           // Рекомендации по устранению неполадок
    Serial.println(F("   • Пин 10 → питание модуля (вместо 3.3V)"));  // Требование к схеме подключения
    Serial.println(F("   • EN замкнут на пин 10 ДО включения питания"));  // Условие входа в режим AT
    Serial.println(F("   • RX/TX подключены крест-накрест"));  // Проверка правильности перекрёстного подключения
    while(!atMode){digitalWrite(YELLOW_PIN, LOW);delay(1000);digitalWrite(YELLOW_PIN, HIGH);delay(1000);}
  }
}

// === ОСТАЛЬНЫЕ ФУНКЦИИ ===
void generateQR(const char* text) {
  QRCode qrcode;
  uint8_t qrcodeData[qrcode_getBufferSize(4)]; 
  
  // Генерация QR-кода (L — низкая ошибка коррекции)
  qrcode_initText(&qrcode, qrcodeData, 4, 0, text);
  
  // Вывод в ASCII (21x21 модуль для версии 3)
  // Рамка сверху
  Serial.print("┌");
  for (uint8_t i = 0; i < qrcode.size * 2; i++) Serial.print("─");
  Serial.println("┐");
  
  // Тело QR (инверсия: █ = белый, пробел = чёрный)
  for (uint8_t y = 0; y < qrcode.size; y++) {
    Serial.print("│");
    for (uint8_t x = 0; x < qrcode.size; x++) {
      bool pixel = qrcode_getModule(&qrcode, x, y);
      Serial.print(pixel ? "██" : "  ");  // █ = чёрный (сканируется!)  // Инверсия для сканеров
    }
    Serial.println("│");
  }
  
  // Рамка снизу
  Serial.print("└");
  for (uint8_t i = 0; i < qrcode.size * 2; i++) Serial.print("─");
  Serial.println("┘");
}

bool verifyTOTPFromString(String input) {
  // Проверка длины строки (8 цифр TOTP + 10 цифр времени = 18)
  if (input.length() < 18) {
    Serial.println(F("❌ Ошибка: строка слишком короткая"));
    digitalWrite(RED_PIN, HIGH);
    delay(1000);
    digitalWrite(RED_PIN, LOW);
    return false;
  }

  // 1. Извлекаем TOTP (первые 8 символов)
  String totpStr = input.substring(0, 8);
  uint32_t targetTOTP = totpStr.toInt();

  // 2. Извлекаем время (символы с 8 по 17)
  String timeStr = input.substring(8, 18);
  unsigned long unixTime = timeStr.toInt();

  // Проверка корректности времени
  if (unixTime < 1000000000UL) {
    Serial.println(F("❌ Ошибка: некорректное время"));
    digitalWrite(RED_PIN, HIGH);
    delay(1000);
    digitalWrite(RED_PIN, LOW);
    return false;
  }

  Serial.print(F("🔍 Проверка TOTP: "));
  Serial.print(totpStr);
  Serial.print(F(" | Время: "));
  Serial.println(timeStr);

  // 3. Перебор всех ключей в EEPROM
  for (int i = 0; i < MAX_RECORDS; i++) {
    if (!isSlotFree(i)) {  // Если ячейка занята
      uint8_t secret[SECRET_SIZE];
      
      // Чтение ключа из EEPROM
      for (int j = 0; j < SECRET_SIZE; j++) {
        secret[j] = EEPROM.read(i * SECRET_SIZE + j);
      }

      // Генерация TOTP для этого ключа с извлечённым временем
      uint32_t generatedTOTP = generateTOTP8(secret, SECRET_SIZE, unixTime);

      // Сравнение
      Serial.print(F("  🔄 Ячейка #"));
      Serial.print(i);
      Serial.print(F(" → "));
      Serial.println(generatedTOTP);
      if (generatedTOTP == targetTOTP) {
        Serial.print(F("✅ СОВПАДЕНИЕ! Ключ найден в ячейке #"));
        Serial.println(i);
        digitalWrite(LOCKER_PIN, LOW);
        digitalWrite(YELLOW_PIN, HIGH);
        delay(10000);
        digitalWrite(YELLOW_PIN, LOW);
        digitalWrite(LOCKER_PIN, HIGH);
        return true;  // Ключ найден
      }
    }
  }

  Serial.println(F("❌ Ключ не найден"));
  digitalWrite(RED_PIN, HIGH);
  delay(1000);
  digitalWrite(RED_PIN, LOW);
  return false;  // Ни один ключ не подошёл
}

uint8_t pseudoRandomByte() {                      // Генерация псевдослучайного байта на основе шума АЦП
  uint8_t val = 0;                                // Инициализация результата нулём
  for (int i = 0; i < 8; i++) {                   // Цикл для формирования 8 бит
    val = (val << 1) | (analogRead(A0) & 1);      // Сдвиг аккумулятора и добавление младшего бита с АЦП
    delayMicroseconds(50);                        // Короткая задержка между чтениями для увеличения энтропии
  }
  return val;                                     // Возврат сгенерированного байта
}

void generateSecret(uint8_t* out) {
  for (int i = 0; i < SECRET_SIZE; i++) {
    out[i] = random(256); // Генерация числа от 0 до 255
  }
}

const char base32Alphabet[] PROGMEM = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";  // Алфавит кодировки Base32 во флеш-памяти
void base32Encode(const uint8_t* data, size_t len) {  // Кодирование бинарных данных в строку Base32
  int bits = 0, value = 0, chars = 0;             // Переменные для битовой обработки и подсчёта символов
  for (size_t i = 0; i < len; i++) {              // Обработка каждого байта входных данных
    value = (value << 8) | data[i];               // Накопление битов в 32-битном регистре
    bits += 8;                                    // Учёт количества накопленных битов
    while (bits >= 5) {                           // Пока накоплено >=5 бит (один символ Base32)
      bits -= 5;                                  // Уменьшение счётчика битов
      Serial.write(pgm_read_byte(&base32Alphabet[(value >> bits) & 0x1F]));  // Вывод символа из алфавита
      chars++;                                    // Инкремент счётчика выведенных символов
    }
  }
  if (bits > 0) {                                 // Если остались необработанные биты (<5)
    Serial.write(pgm_read_byte(&base32Alphabet[(value << (5 - bits)) & 0x1F]));  // Добавление последнего символа
    chars++;                                      // Инкремент счётчика
  }
  while (chars % 8 != 0) {                        // Добавление отступа до кратности 8 символам (стандарт Base32)
    Serial.write('=');                            // Вывод символа заполнения
    chars++;
  }
}

// === НОВАЯ ФУНКЦИЯ: Возвращает Base32 как String ===
String getBase32String(const uint8_t* data, size_t len) {
  String result = "";
  int bits = 0, value = 0, chars = 0;
  for (size_t i = 0; i < len; i++) {
    value = (value << 8) | data[i];
    bits += 8;
    while (bits >= 5) {
      bits -= 5;
      result += (char)pgm_read_byte(&base32Alphabet[(value >> bits) & 0x1F]);  // ← Добавлено (char)
      chars++;
    }
  }
  if (bits > 0) {
    result += (char)pgm_read_byte(&base32Alphabet[(value << (5 - bits)) & 0x1F]);  // ← Добавлено (char)
    chars++;
  }
  while (chars % 8 != 0) {
    result += '=';
    chars++;
  }
  return result;
}

bool isSlotFree(int index) {                      // Проверка, свободна ли ячейка в EEPROM по указанному индексу
  for (int i = 0; i < SECRET_SIZE; i++) {         // Проверка всех байтов ячейки
    if (EEPROM.read(index * SECRET_SIZE + i) != 0xFF) return false;  // Если найден не-0xFF байт — ячейка занята
  }
  return true;                                    // Все байты 0xFF — ячейка свободна (0xFF = стёртое состояние EEPROM)
}

bool saveSecret(const uint8_t* secret) {          // Сохранение секретного ключа в первую свободную ячейку EEPROM
  for (int i = 0; i < MAX_RECORDS; i++) {         // Перебор всех возможных ячеек
    if (isSlotFree(i)) {                          // Если ячейка свободна
      for (size_t j = 0; j < SECRET_SIZE; j++) EEPROM.write(i * SECRET_SIZE + j, secret[j]);  // Запись ключа побайтово
      return true;                                // Успешное сохранение
    }
  }
  return false;                                   // Нет свободных ячеек — ошибка
}

void delete_uid(int choice){
  int realIndex = -1, found = 0;                // Переменные для поиска физического индекса записи
    for (int i = 0; i < MAX_RECORDS; i++) {       // Перебор ячеек EEPROM
      if (!isSlotFree(i) && ++found == choice) {  // При совпадении порядкового номера
        realIndex = i;                            // Запоминание физического индекса
        break;
      }
    }
    if (realIndex >= 0) {                         // Если индекс найден
      for (int j = 0; j < SECRET_SIZE; j++) EEPROM.write(realIndex * SECRET_SIZE + j, 0xFF);  // Стирание ключа (запись 0xFF)
    }
}

void eeget(int &count){   
  count =0;                               
  for (int i = 0; i < MAX_RECORDS; i++) {         // Перебор всех ячеек EEPROM
    if (!isSlotFree(i)) {                         // Если ячейка занята
      uint8_t secret[SECRET_SIZE];                // Буфер для чтения ключа
      for (int j = 0; j < SECRET_SIZE; j++) secret[j] = EEPROM.read(i * SECRET_SIZE + j);  // Чтение ключа из EEPROM
      uint32_t uid = generateTOTP8(secret, SECRET_SIZE, FIXED_TIME);  // Генерация UID на основе фиксированного времени
      Serial.print(F("\n"));
      Serial.print(count + 1);                    // Нумерация записи
      Serial.print(F(". UID: "));
      printPadded(uid, 8);                        // Вывод UID с ведущими нулями
      count++;                                    // Инкремент счётчика записей
    }
  }
}

void listAndDeleteUIDs() {                        // Функция просмотра и удаления сохранённых UID
  Serial.println(F("\n--- Список UID ---")); 
  int count = 0;     
  eeget(count);
  if (count == 0) {                               // Если записей нет
    Serial.println(F("📭 Нет записей"));          // Информирование пользователя
    return;                                       // Выход из функции
  }
  Serial.print(F("\nУдалить? (1-"));              // Запрос на удаление записи
  Serial.print(count);
  Serial.println(F(") или all для полной очистки. Для выхода используйте 0:"));
  while (!Serial.available()) delay(10);          // Ожидание ввода пользователя
  String input = Serial.readStringUntil('\n');    // Чтение введённой строки
  input.trim();
  int choice = input.toInt(); 
  if (choice >= 1 && choice <= count) {           // Если выбрана корректная запись для удаления
    delete_uid(choice);
    Serial.println(F("🗑️ Удалено"));
  } else if (input == "all"){
    while (count !=0){eeget(count); delete_uid(1);}
    listAndDeleteUIDs();
  } else {
    Serial.println(F("↩️ Отмена"));               // Отмена операции при вводе 0 или некорректного значения
  }
  
  // Сброс после операции (закомментирован для предотвращения излишних перезагрузок)
  // resetBluetoothModule();
}

void help(){
  Serial.println(F("💡 Команды:"));               // Справка по командам
  Serial.println(F("  help    → помощь по командам"));
  Serial.println(F("  0       → новый UID"));     // Генерация нового секретного ключа и UID
  Serial.println(F("  1       → список/удаление"));  // Просмотр и удаление сохранённых UID
  Serial.println(F("  2       → отправить что-то по Bluetooth"));
  Serial.println(F("  XXXXXXXX → TOTP для UID")); // Генерация TOTP кода для указанного UID
  Serial.println(F("  e       → эскалация пользователя по UID"));
  Serial.println(F("==========================================\n"));
}

// === ОСНОВНАЯ ПРОГРАММА ===
void setup() {
  pinMode(RED_PIN, OUTPUT);
  pinMode(LOCKER_PIN, OUTPUT);
  pinMode(YELLOW_PIN, OUTPUT);
  digitalWrite(RED_PIN, LOW); 
  digitalWrite(YELLOW_PIN, LOW); 
  digitalWrite(RED_PIN, HIGH);
  digitalWrite(LOCKER_PIN, HIGH);
  Serial.begin(9600);                             // Инициализация аппаратного последовательного порта для отладки
  // jwt.allocateJWTMemory();
  while (!Serial) delay(10);                      // Ожидание подключения монитора порта (для плат с USB-конвертером)
  Serial.setTimeout(1800000);
  digitalWrite(YELLOW_PIN, HIGH);
  Serial.println("Таймаут по умолчанию: " + String(Serial.getTimeout()) + "мс");
  Serial.println(F("=========================================="));  // Разделительная линия
  Serial.println(F("🔐 MyTOTP System v4.0 (с автосбросом)"));  // Приветственное сообщение
  Serial.println(F("=========================================="));
  
  pinMode(A0, INPUT);                             // Настройка аналогового пина A0 как вход для генерации случайных чисел
  randomSeed(analogRead(A0) + millis());                     // Инициализация генератора случайных чисел шумом с АЦП
  
  configureHC05();                                // Автоматическая настройка Bluetooth модуля при старте
  digitalWrite(YELLOW_PIN, LOW);
  help();
}

void menu(String input, int choice, bool processed){
   if (input == "0") {                            // Команда "0" — генерация нового ключа
      uint8_t newSecret[SECRET_SIZE];             // Буфер для нового секретного ключа
      generateSecret(newSecret);                  // Генерация случайного ключа
      uint32_t uid = generateTOTP8(newSecret, SECRET_SIZE, FIXED_TIME);  // Генерация UID на основе фиксированного времени
      
      bool exists = false;                        // Флаг обнаружения коллизии UID
      int attempts = 0;
      const int MAX_ATTEMPTS = 10;

      do {
        attempts++;
        generateSecret(newSecret);
        uid = generateTOTP8(newSecret, SECRET_SIZE, FIXED_TIME);

        exists = false;
        for (int i = 0; i < MAX_RECORDS && !exists; i++) {
          if (!isSlotFree(i)) {
            uint8_t existing[SECRET_SIZE];
            for (int j = 0; j < SECRET_SIZE; j++) {
              existing[j] = EEPROM.read(i * SECRET_SIZE + j);
            }
            if (generateTOTP8(existing, SECRET_SIZE, FIXED_TIME) == uid) {
              exists = true;
            }
          }
        }

        if (exists) {
          Serial.println(F("⚠️ Коллизия UID! Повторная генерация..."));
        }
      } while (exists && attempts < MAX_ATTEMPTS);

      if (exists) {
        Serial.println(F("❌ Не удалось создать уникальный UID после 10 попыток."));
      } else if (!saveSecret(newSecret)) {
        Serial.println(F("❌ EEPROM полна!"));
      } else {
        // Успешное сохранение
      }
      if (!exists){
        Serial.print(F("✅ UID: "));              // Успешное сохранение
        printPadded(uid, 8);                      // Вывод нового UID
        Serial.println();
        Serial.print(F("🔑 Base32: "));            // Вывод ключа в формате Base32 для ручного ввода в приложение
        base32Encode(newSecret, SECRET_SIZE);
        Serial.println();
        uint8_t fixedKeyRam[SECRET_SIZE];           // Буфер для копирования ключа из PROGMEM в RAM
        memcpy_P(fixedKeyRam, FIXED_KEY_BYTES, SECRET_SIZE);  // Копирование ключа из флеш-памяти в оперативную
        uint32_t fullCode = generateTOTP8(fixedKeyRam, SECRET_SIZE, FIXED_TIME);  // Генерация 8-значного кода на основе фиксированного времени
        if (fullCode == 0 || fullCode == 4294967295UL) fullCode = 3627;  // Обработка ошибочных значений кода
        uint32_t pin4 = fullCode % 10000;
        String qrData = String(pin4) + getBase32String(newSecret, SECRET_SIZE);
        Serial.print(F("📱 QR строка: "));
        Serial.println(qrData);  // Проверка перед генерацией
        generateQR(qrData.c_str());
        processed = true;                         // Пометка команды как обработанной
      }

    } else if (choice == 1) {               // Специальная команда для просмотра/удаления записей
      listAndDeleteUIDs();                        // Вызов функции управления записями
      processed = true;                           // Пометка команды как обработанной

    } else if (choice == 2){
      Serial.print(F("\nType something for sending: "));
      String input = Serial.readStringUntil('\n');
      input.trim(); 
      
      btSerial.print(F("\n💻 Отправлено: "));         // Эхо-вывод команды в Bluetooth для отладки
      btSerial.println(input);
      Serial.print(F("\n✅ Sent: "));
      Serial.print(input);
      Serial.print(F("\n"));
    } else if (choice > 0 && choice < 99999999) { // Обработка запроса TOTP кода для существующего UID
      uint32_t targetUID = (uint32_t)choice;      // Целевой UID из пользовательского ввода
      
      Serial.print(F("⏱️ Время: "));              // Запрос текущего Unix-времени от пользователя
      while (!Serial.available()) delay(10);      // Ожидание ввода времени
      String timeInput = Serial.readStringUntil('\n');  // Чтение временной метки
      unsigned long unixTime = timeInput.toInt(); // Преобразование в число
      
      if (unixTime < 1000000000UL) Serial.println(F("❌ Время!"));  // Проверка корректности времени (минимум ~2001 год)
      else {
        bool found = false;                       // Флаг нахождения соответствующего ключа
        for (int i = 0; i < MAX_RECORDS; i++) {   // Поиск ключа по UID
          if (!isSlotFree(i)) {                   // Для каждой занятой ячейки
            uint8_t secret[SECRET_SIZE];          // Чтение ключа из EEPROM
            for (int j = 0; j < SECRET_SIZE; j++) secret[j] = EEPROM.read(i * SECRET_SIZE + j);
            if (generateTOTP8(secret, SECRET_SIZE, FIXED_TIME) == targetUID) {  // Сравнение с целевым UID
              uint32_t totp = generateTOTP8(secret, SECRET_SIZE, unixTime);  // Генерация TOTP на основе текущего времени
              Serial.print(F("🔢 TOTP: "));
              printPadded(totp, 8);               // Вывод 8-значного кода
              Serial.println();
              found = true;
              processed = true;                   // Пометка команды как обработанной
            }
          }
        }
        if (!found) Serial.println(F("❌ UID не найден!"));  // Ошибка при отсутствии совпадений
      }
    } else if (input == "help"){
      help();
    } else {
      Serial.println(F("\n❓ Неизвестная команда"));  // Обработка некорректного ввода
    }
}

void loop() {
  if (Serial.available()) {                       // Если есть данные от монитора порта (ПК)
    String input = Serial.readStringUntil('\n');  // Чтение команды до символа новой строки
    input.trim();                                 // Удаление пробельных символов по краям
    if (input.length() == 0) return;              // Игнорирование пустых строк
    long choice = input.toInt();                  // Преобразование ввода в число для обработки команд
    bool processed = false;                       // Флаг успешной обработки команды

    menu(input,choice,processed);
    
    // СБРОС ПОСЛЕ ЛЮБОЙ ОБРАБОТАННОЙ КОМАНДЫ (временно отключён для стабильности)
    if (processed) {
      delay(300);                                 // Короткая задержка перед сбросом
      // resetBluetoothModule();                  // Полный сброс модуля (закомментирован для предотвращения разрывов связи)
    }
  }

  if (btSerial.available()) {                     // Если есть данные от подключённого устройства по Bluetooth
    String msg = btSerial.readString();           // Чтение полного сообщения
    Serial.print(F("📱 BT: "));                   // Вывод полученного сообщения в монитор порта
    Serial.println(msg);
    if (!msg.indexOf("ERROR")){
      resetBluetoothModule();
    }
    
    btSerial.print(F("\n✅ Получено: "));             // Подтверждение приёма сообщения обратно на устройство
    btSerial.println(msg);
    
    verifyTOTPFromString(msg);


    // СБРОС ПОСЛЕ ПОЛУЧЕНИЯ СООБЩЕНИЯ ОТ ТЕЛЕФОНА (временно отключён)
    delay(300);
    // btSerial.end();
    // btSerial.begin(9600);
    // resetBluetoothModule();
  }

  delay(50);                                      // Короткая задержка для снижения нагрузки на процессор в основном цикле
}