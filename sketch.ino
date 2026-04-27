// ============================================================
// SISTEM PARKIR OTOMATIS BERBASIS ARDUINO UNO
// Mata Kuliah  : Sistem Mikrokontroler (TK244004)
// Universitas  : Universitas Jenderal Soedirman
// ============================================================

#include <SPI.h>
#include <MFRC522.h>
#include <Servo.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// ── PIN ──────────────────────────────────────────
#define PIN_RFID_SS    10
#define PIN_RFID_RST   9
#define PIN_SERVO      6
#define PIN_TRIG       5
#define PIN_ECHO       4
#define PIN_BUZZER     7
#define PIN_LED_HIJAU  8
#define PIN_LED_MERAH  3
#define PIN_BUTTON     2

// ── KONFIGURASI ──────────────────────────────────
#define KAPASITAS_SLOT  3
#define JARAK_BATAS     20    // cm
#define SERVO_BUKA      90
#define SERVO_TUTUP     0

// ── UID KARTU RFID TERDAFTAR ─────────────────────
// Jalankan dulu, scan kartu, lihat UID di Serial Monitor
// lalu ganti nilai di bawah ini
const byte UID_TERDAFTAR[][4] = {
  {0xDE, 0xAD, 0xBE, 0xEF},  // Kartu 1 — ganti sesuai UID kartu kamu
  {0x12, 0x34, 0x56, 0x78},  // Kartu 2
  {0xAB, 0xCD, 0xEF, 0x01}   // Kartu 3
};
const int JUMLAH_KARTU = 3;

// ── OBJEK ────────────────────────────────────────
MFRC522 rfid(PIN_RFID_SS, PIN_RFID_RST);
Servo servoSalang;
LiquidCrystal_I2C lcd(0x27, 16, 2);

// ── VARIABEL GLOBAL ───────────────────────────────
int slotTerisi     = 0;
bool palangTerbuka = false;
volatile bool resetDarurat = false;

// ── ISR INTERRUPT ─────────────────────────────────
// Dipanggil otomatis saat tombol ditekan
void ISR_ResetDarurat() {
  resetDarurat = true;
}

// ══════════════════════════════════════════════════
// SETUP
// ══════════════════════════════════════════════════
void setup() {
  // Serial UART — untuk monitoring debug
  Serial.begin(9600);
  Serial.println("=== SISTEM PARKIR OTOMATIS ===");

  // GPIO pin mode
  pinMode(PIN_TRIG,      OUTPUT);
  pinMode(PIN_ECHO,      INPUT);
  pinMode(PIN_BUZZER,    OUTPUT);
  pinMode(PIN_LED_HIJAU, OUTPUT);
  pinMode(PIN_LED_MERAH, OUTPUT);
  pinMode(PIN_BUTTON,    INPUT_PULLUP); // Pull-up internal, tidak perlu resistor

  // Interrupt eksternal — D2 (INT0), aktif saat tombol ditekan (FALLING)
  attachInterrupt(digitalPinToInterrupt(PIN_BUTTON), ISR_ResetDarurat, FALLING);

  // SPI + RFID
  SPI.begin();
  rfid.PCD_Init();
  Serial.println("RFID siap.");

  // Servo — mulai posisi tutup
  servoSalang.attach(PIN_SERVO);
  servoSalang.write(SERVO_TUTUP);
  Serial.println("Servo siap.");

  // LCD I2C
  lcd.init();
  lcd.backlight();
  tampilkanLCD("Sistem Parkir", "Siap Beroperasi");
  Serial.println("LCD siap.");

  delay(2000);
  updateStatusSistem();
  Serial.println("Scan kartu RFID untuk masuk...");
}

// ══════════════════════════════════════════════════
// LOOP UTAMA
// ══════════════════════════════════════════════════
void loop() {

  // ── CEK INTERRUPT RESET ──────────────────────
  if (resetDarurat) {
    resetDarurat = false;
    prosesReset();
    return;
  }

  // ── JIKA PALANG TERBUKA: tunggu mobil lewat ──
  if (palangTerbuka) {
    float jarak = ukurJarak();

    Serial.print("Jarak: ");
    Serial.print(jarak);
    Serial.println(" cm");

    // Jika mobil terdeteksi lewat sensor (jarak < batas)
    if (jarak < JARAK_BATAS && jarak > 0) {
      delay(500); // Tunggu mobil benar-benar lewat

      tutupPalang();      // Tutup palang
      slotTerisi++;       // Tambah jumlah slot terisi

      Serial.print("Mobil masuk. Slot terisi: ");
      Serial.println(slotTerisi);

      updateStatusSistem();
    }

    delay(100); // Interval polling sensor
    return;
  }

  // ── JIKA PALANG TERTUTUP: cek RFID ──────────
  if (!rfid.PICC_IsNewCardPresent() || !rfid.PICC_ReadCardSerial()) {
    return; // Tidak ada kartu — lanjut loop
  }

  // Kartu terdeteksi — print UID ke Serial Monitor
  Serial.print("Kartu terdeteksi! UID: ");
  for (byte i = 0; i < rfid.uid.size; i++) {
    Serial.print(rfid.uid.uidByte[i] < 0x10 ? " 0" : " ");
    Serial.print(rfid.uid.uidByte[i], HEX);
  }
  Serial.println();

  // Percabangan: cek slot dan validasi kartu
  if (slotTerisi >= KAPASITAS_SLOT) {
    // Slot penuh — tolak masuk
    Serial.println("SLOT PENUH!");
    tampilkanLCD("  SLOT PENUH!", "Maaf, coba lagi");
    bunyikanBuzzer(3, 100); // 3x beep cepat = ditolak
    delay(2000);
    updateStatusSistem();

  } else if (cekKartuTerdaftar(rfid.uid.uidByte, rfid.uid.size)) {
    // Kartu valid + slot tersedia — buka palang
    Serial.println("Kartu VALID. Buka palang!");
    tampilkanLCD("Akses Diterima", "Palang Terbuka..");
    bunyikanBuzzer(1, 500); // 1x beep panjang = diterima
    bukaPalang();

  } else {
    // Kartu tidak terdaftar — tolak
    Serial.println("Kartu TIDAK TERDAFTAR!");
    tampilkanLCD("Kartu Invalid!", "Akses Ditolak");
    bunyikanBuzzer(2, 300); // 2x beep = ditolak
    delay(2000);
    updateStatusSistem();
  }

  // Stop komunikasi RFID
  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();
}

// ══════════════════════════════════════════════════
// FUNGSI-FUNGSI
// ══════════════════════════════════════════════════

// Ukur jarak HC-SR04 → return cm
float ukurJarak() {
  digitalWrite(PIN_TRIG, LOW);
  delayMicroseconds(2);
  digitalWrite(PIN_TRIG, HIGH);
  delayMicroseconds(10);
  digitalWrite(PIN_TRIG, LOW);

  long durasi = pulseIn(PIN_ECHO, HIGH, 30000);
  return (durasi * 0.0343) / 2.0;
}

// Cek UID kartu vs daftar terdaftar
bool cekKartuTerdaftar(byte* uid, byte ukuranUID) {
  for (int i = 0; i < JUMLAH_KARTU; i++) {
    bool cocok = true;
    for (byte j = 0; j < 4; j++) {
      if (uid[j] != UID_TERDAFTAR[i][j]) {
        cocok = false;
        break;
      }
    }
    if (cocok) return true;
  }
  return false;
}

// Buka palang — servo ke 90 derajat (PWM)
void bukaPalang() {
  servoSalang.write(SERVO_BUKA);
  palangTerbuka = true;
  Serial.println("Palang TERBUKA.");
}

// Tutup palang — servo ke 0 derajat (PWM)
void tutupPalang() {
  servoSalang.write(SERVO_TUTUP);
  palangTerbuka = false;
  Serial.println("Palang TERTUTUP.");
  bunyikanBuzzer(2, 200);
}

// Bunyi buzzer — jumlah beep & durasi (ms)
void bunyikanBuzzer(int jumlah, int durasi) {
  for (int i = 0; i < jumlah; i++) {
    digitalWrite(PIN_BUZZER, HIGH);
    delay(durasi);
    digitalWrite(PIN_BUZZER, LOW);
    if (i < jumlah - 1) delay(durasi);
  }
}

// Update LCD + LED sesuai status slot
void updateStatusSistem() {
  int slotKosong = KAPASITAS_SLOT - slotTerisi;

  // UART — kirim status ke Serial Monitor
  Serial.print("Status → Terisi: ");
  Serial.print(slotTerisi);
  Serial.print(" | Kosong: ");
  Serial.println(slotKosong);

  // LED indikator — percabangan if/else
  if (slotKosong <= 0) {
    digitalWrite(PIN_LED_MERAH, HIGH);
    digitalWrite(PIN_LED_HIJAU, LOW);
  } else {
    digitalWrite(PIN_LED_HIJAU, HIGH);
    digitalWrite(PIN_LED_MERAH, LOW);
  }

  // LCD I2C update
  String baris1 = "Slot: " + String(slotTerisi) + "/" + String(KAPASITAS_SLOT);
  String baris2 = (slotKosong > 0)
    ? "Kosong: " + String(slotKosong)
    : "  PARKIR PENUH!";
  tampilkanLCD(baris1, baris2);
}

// Tampilkan 2 baris ke LCD I2C
void tampilkanLCD(String baris1, String baris2) {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(baris1);
  lcd.setCursor(0, 1);
  lcd.print(baris2);
}

// Reset darurat — dipanggil via flag dari ISR
void prosesReset() {
  Serial.println("!!! RESET DARURAT !!!");
  tampilkanLCD("!! RESET !!", "Sistem direset..");
  bunyikanBuzzer(5, 100);
  tutupPalang();
  slotTerisi    = 0;
  palangTerbuka = false;
  delay(1000);
  updateStatusSistem();
  Serial.println("Reset selesai.");
}
