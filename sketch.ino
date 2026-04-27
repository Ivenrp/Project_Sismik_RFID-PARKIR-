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
#define PIN_LED_HIJAU  8    // Berkedip saat kartu VALID (slot tersedia)
#define PIN_LED_MERAH  3    // Menyala saat slot penuh / kartu invalid
#define PIN_BUTTON     2
#define PIN_LED_KUNING A0   // LED indikator RFID siap discan
#define PIN_LDR        A1   // Sensor cahaya LDR (ANALOG)
#define PIN_LED_LAMPU  A2   // Lampu parkir otomatis (OUTPUT)

// ── KONFIGURASI ──────────────────────────────────
#define KAPASITAS_SLOT  3
#define JARAK_BATAS     120    // cm — batas deteksi kendaraan untuk scan RFID
#define JARAK_DEKAT     3     // cm — trigger hitung mundur
#define JEDA_TUTUP      5000  // ms — hitung mundur 5 detik
#define AMBANG_GELAP    500   // Ambang batas LDR untuk malam
#define SERVO_BUKA      90
#define SERVO_TUTUP     0

// ── UID KARTU RFID TERDAFTAR ─────────────────────
const byte UID_TERDAFTAR[][4] = {
  {0x01, 0x02, 0x03, 0x04},  // Kartu 1
  {0x11, 0x22, 0x33, 0x44},  // Kartu 2
  {0x55, 0x66, 0x77, 0x88}   // Kartu 3
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
bool kendaraanTerdeteksi = false;     // Status deteksi kendaraan oleh ultrasonic (mode tunggu)
bool hitungMundurAktif = false;       // Flag hitung mundur sedang berjalan
unsigned long waktuMulaiHitung = 0;   // Timestamp mulai hitung mundur 5 detik
int detikTerakhir = -1;               // Untuk notifikasi per detik
bool lampuMenyala = false;            // Status lampu parkir
unsigned long waktuCekLDR = 0;        // Interval cek LDR

// ── ISR INTERRUPT ─────────────────────────────────
void ISR_ResetDarurat() {
  resetDarurat = true;
}

// ══════════════════════════════════════════════════
// SETUP
// ══════════════════════════════════════════════════
void setup() {
  Serial.begin(9600);
  Serial.println("=== SISTEM PARKIR OTOMATIS AKTIF===");

  // GPIO pin mode
  pinMode(PIN_TRIG,      OUTPUT);
  pinMode(PIN_ECHO,      INPUT);
  pinMode(PIN_BUZZER,    OUTPUT);
  pinMode(PIN_LED_HIJAU, OUTPUT);
  pinMode(PIN_LED_MERAH, OUTPUT);
  pinMode(PIN_BUTTON,    INPUT_PULLUP);
  pinMode(PIN_LED_KUNING, OUTPUT);
  pinMode(PIN_LED_LAMPU, OUTPUT);     // Lampu parkir

  // Pastikan semua LED mati saat startup
  digitalWrite(PIN_LED_KUNING, LOW);
  digitalWrite(PIN_LED_HIJAU,  LOW);
  digitalWrite(PIN_LED_MERAH,  LOW);
  digitalWrite(PIN_LED_LAMPU,  LOW);  // Lampu mati saat startup

  // Interrupt eksternal — D2 (INT0)
  attachInterrupt(digitalPinToInterrupt(PIN_BUTTON), ISR_ResetDarurat, FALLING);

  // SPI + RFID
  SPI.begin();
  rfid.PCD_Init();
  Serial.println("RFID siap.");

  // Servo
  servoSalang.attach(PIN_SERVO);
  servoSalang.write(SERVO_TUTUP);
  Serial.println("Servo siap.");

  // LCD I2C
  lcd.init();
  lcd.backlight();
  tampilkanLCD("Sistem Parkir", "Siap Beroperasi");
  Serial.println("LCD siap.");

  // Cek kondisi cahaya saat startup
  cekSensorLDR();

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

  // Cek sensor LDR setiap 2 detik (non-blocking)
  if (millis() - waktuCekLDR >= 2000) {
    waktuCekLDR = millis();
    cekSensorLDR();
  }

  // ═══════════════════════════════════════════════════
  // JIKA PALANG TERBUKA: logika penutupan
  // ═══════════════════════════════════════════════════
  if (palangTerbuka) {

    // ── Jika hitung mundur BELUM aktif: cek sensor ──
    if (!hitungMundurAktif) {
      float jarak = ukurJarak();

      Serial.print("[PALANG TERBUKA] Jarak: ");
      Serial.print(jarak);
      Serial.println(" cm | Menunggu kendaraan melewati palang...");

      // Trigger: kendaraan mendekat/melewati (<= 3cm)
      if (jarak <= JARAK_DEKAT && jarak > 0) {
        hitungMundurAktif = true;
        waktuMulaiHitung = millis();
        detikTerakhir = -1;
        Serial.println("Palang tertutup dalam 5 detik...");
        tampilkanLCD("Kendaraan Lewat", " ");
      }
    }

    // ── Jika hitung mundur SUDAH aktif: berjalan MANDIRI ──
    else {
      unsigned long waktuSekarang = millis();
      unsigned long elapsed = waktuSekarang - waktuMulaiHitung;
      unsigned long sisaWaktu = JEDA_TUTUP - elapsed;
      int sisaDetik = sisaWaktu / 1000;

      // Notifikasi hanya setiap 1 detik
      if (sisaDetik != detikTerakhir) {
        detikTerakhir = sisaDetik;

        Serial.print("[HITUNG MUNDUR] Sisa: ");
        Serial.println(sisaDetik);

        if (sisaDetik == 0) {
          Serial.println("[HITUNG MUNDUR] 1 detik lagi...");
        }
      }

      // Cek apakah 5 detik sudah selesai
      if (elapsed >= JEDA_TUTUP) {
        Serial.println("Menutup palang...");
        tutupPalang();
        slotTerisi++;
        Serial.print("Mobil masuk. Slot terisi: ");
        Serial.println(slotTerisi);
        updateStatusSistem();
      }
    }

    delay(100);
    return;
  }

  // ═══════════════════════════════════════════════════
  // JIKA PALANG TERTUTUP: deteksi kendaraan & scan RFID
  // ═══════════════════════════════════════════════════

  float jarak = ukurJarak();

  // Jika ada kendaraan terdeteksi (jarak < batas 20cm)
  if (jarak < JARAK_BATAS && jarak > 0) {
    if (!kendaraanTerdeteksi) {
      kendaraanTerdeteksi = true;
      digitalWrite(PIN_LED_KUNING, HIGH);   // Kuning nyala = RFID aktif
      // LED hijau MATI saat kuning menyala (mode tunggu scan)
      digitalWrite(PIN_LED_HIJAU, LOW);
      digitalWrite(PIN_LED_MERAH, LOW);
      tampilkanLCD("  Silahkan", "   Scan!");
      Serial.println("Kendaraan terdeteksi. RFID aktif. Scan kartu!");
    }
  } else {
    // Tidak ada kendaraan terdeteksi
    if (kendaraanTerdeteksi) {
      kendaraanTerdeteksi = false;
      digitalWrite(PIN_LED_KUNING, LOW);
      updateStatusSistem();
      Serial.println("Kendaraan tidak terdeteksi. Menunggu...");
    }
  }

  // CEK RFID HANYA JIKA ADA KENDARAAN
  if (!kendaraanTerdeteksi) {
    return;
  }

  // CEK KARTU RFID
  if (!rfid.PICC_IsNewCardPresent() || !rfid.PICC_ReadCardSerial()) {
    return;
  }

  // Kartu terdeteksi — print UID ke Serial Monitor
  Serial.print("Kartu terdeteksi! UID: ");
  for (byte i = 0; i < rfid.uid.size; i++) {
    Serial.print(rfid.uid.uidByte[i] < 0x10 ? " 0" : " ");
    Serial.print(rfid.uid.uidByte[i], HEX);
  }
  Serial.println();

  // PERCABANGAN KONDISI KARTU
  if (slotTerisi >= KAPASITAS_SLOT) {
    // ── SLOT PENUH ──
    Serial.println("SLOT PENUH!");
    tampilkanLCD("  SLOT PENUH!", "Maaf, coba lagi");
    // LED merah NYALA = slot penuh
    digitalWrite(PIN_LED_MERAH, HIGH);
    digitalWrite(PIN_LED_HIJAU, LOW);
    digitalWrite(PIN_LED_KUNING, LOW);
    bunyikanBuzzer(3, 150);
    delay(2000);
    // Kembali ke mode tunggu scan
    digitalWrite(PIN_LED_MERAH, LOW);
    digitalWrite(PIN_LED_KUNING, HIGH);
    tampilkanLCD("  Silahkan", "   Scan!");

  } else if (cekKartuTerdaftar(rfid.uid.uidByte, rfid.uid.size)) {
    // ── KARTU VALID + SLOT TERSEDIA ──
    Serial.println("Kartu VALID. Buka palang!");
    tampilkanLCD("Akses Diterima", "Palang Terbuka..");
    // LED hijau BERKEDIP = indikator slot tersedia
    digitalWrite(PIN_LED_KUNING, LOW);
    digitalWrite(PIN_LED_MERAH, LOW);
    bunyikanBuzzer(1, 800);
    // Berkedip 3x sebagai indikator slot tersedia
    for (int i = 0; i < 3; i++) {
      digitalWrite(PIN_LED_HIJAU, HIGH);
      delay(200);
      digitalWrite(PIN_LED_HIJAU, LOW);
      delay(200);
    }
    digitalWrite(PIN_LED_HIJAU, HIGH);  // Nyala terus setelah berkedip
    bukaPalang();

  } else {
    // ── KARTU TIDAK TERDAFTAR ──
    Serial.println("Kartu TIDAK TERDAFTAR!");
    tampilkanLCD("Kartu Invalid!", "Akses Ditolak");
    // LED merah NYALA = kartu invalid
    digitalWrite(PIN_LED_MERAH, HIGH);
    digitalWrite(PIN_LED_HIJAU, LOW);
    digitalWrite(PIN_LED_KUNING, LOW);
    bunyikanBuzzer(5, 100);
    delay(2000);
    // Kembali ke mode tunggu scan
    digitalWrite(PIN_LED_MERAH, LOW);
    digitalWrite(PIN_LED_KUNING, HIGH);
    tampilkanLCD("  Silahkan", "   Scan!");
  }

  // Stop komunikasi RFID
  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();
}

// ══════════════════════════════════════════════════
// FUNGSI-FUNGSI
// ══════════════════════════════════════════════════

// Sensor LDR — deteksi cahaya (ANALOG)
void cekSensorLDR() {
  int nilaiCahaya = analogRead(PIN_LDR);  // Baca sensor analog (0-1023)
  Serial.print("[SENSOR LDR] Nilai cahaya: ");
  Serial.println(nilaiCahaya);

  if (nilaiCahaya < AMBANG_GELAP) {
    // Malam / gelap → nyalakan lampu parkir
    if (!lampuMenyala) {
      lampuMenyala = true;
      digitalWrite(PIN_LED_LAMPU, HIGH);
      Serial.println("[SENSOR LDR] Malam terdeteksi. Lampu parkir MENYALA.");
    }
  } else {
    // Siang → matikan lampu parkir
    if (lampuMenyala) {
      lampuMenyala = false;
      digitalWrite(PIN_LED_LAMPU, LOW);
      Serial.println("[SENSOR LDR] Siang terdeteksi. Lampu parkir MATI.");
    }
  }
}

float ukurJarak() {
  digitalWrite(PIN_TRIG, LOW);
  delayMicroseconds(2);
  digitalWrite(PIN_TRIG, HIGH);
  delayMicroseconds(10);
  digitalWrite(PIN_TRIG, LOW);

  long durasi = pulseIn(PIN_ECHO, HIGH, 30000);
  return (durasi * 0.0343) / 2.0;
}

// Perbaikan BUG: perbandingan UID menggunakan memcmp()
bool cekKartuTerdaftar(byte* uid, byte ukuranUID) {
  if (ukuranUID != 4) return false;

  for (int i = 0; i < JUMLAH_KARTU; i++) {
    if (memcmp(uid, UID_TERDAFTAR[i], 4) == 0) {
      Serial.print("[DEBUG] Kartu cocok dengan index: ");
      Serial.println(i);
      return true;
    }
  }
  return false;
}

// Buka palang + reset flag hitung mundur
void bukaPalang() {
  servoSalang.write(SERVO_BUKA);
  palangTerbuka = true;
  kendaraanTerdeteksi = false;
  hitungMundurAktif = false;
  waktuMulaiHitung = 0;
  detikTerakhir = -1;
  digitalWrite(PIN_LED_KUNING, LOW);
  // LED Hijau tetap nyala (sudah dinyalakan setelah berkedip)
  Serial.println("Palang TERBUKA. Menunggu kendaraan melewati palang...");
}

// Tutup palang + reset flag
void tutupPalang() {
  servoSalang.write(SERVO_TUTUP);
  palangTerbuka = false;
  hitungMundurAktif = false;
  waktuMulaiHitung = 0;
  detikTerakhir = -1;
  // LED Hijau/Merah dikontrol oleh updateStatusSistem() setelah ini
  Serial.println("Palang TERTUTUP.");
  bunyikanBuzzer(2, 200);
}

void bunyikanBuzzer(int jumlah, int durasi) {
  for (int i = 0; i < jumlah; i++) {
    digitalWrite(PIN_BUZZER, HIGH);
    delay(durasi);
    digitalWrite(PIN_BUZZER, LOW);
    if (i < jumlah - 1) delay(durasi);
  }
}

// Update LCD + LED sesuai status slot (sama seperti kode asli)
void updateStatusSistem() {
  int slotKosong = KAPASITAS_SLOT - slotTerisi;

  Serial.print("Status -> Terisi: ");
  Serial.print(slotTerisi);
  Serial.print(" | Kosong: ");
  Serial.println(slotKosong);

  // LED indikator — percabangan if/else (sama seperti kode asli)
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

void tampilkanLCD(String baris1, String baris2) {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(baris1);
  lcd.setCursor(0, 1);
  lcd.print(baris2);
}

// Reset semua flag dan LED
void prosesReset() {
  Serial.println("!!! RESET DARURAT !!!");
  tampilkanLCD("!! RESET !!", "Sistem direset..");
  bunyikanBuzzer(5, 100);
  tutupPalang();
  slotTerisi    = 0;
  palangTerbuka = false;
  kendaraanTerdeteksi = false;
  hitungMundurAktif = false;
  waktuMulaiHitung = 0;
  detikTerakhir = -1;
  lampuMenyala = false;        // Reset lampu
  digitalWrite(PIN_LED_KUNING, LOW);
  digitalWrite(PIN_LED_LAMPU,  LOW);  // Matikan lampu
  delay(1000);
  updateStatusSistem();
  Serial.println("Reset selesai.");
}
