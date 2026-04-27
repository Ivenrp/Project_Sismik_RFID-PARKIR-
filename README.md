## Project 1 Sistem Mikrokontroler 
###### Simulate this project on https://wokwi.com → https://wokwi.com/projects/462262710247219201
---
#### Anggota:
- Iven RivaL Pangestu (H1H024013)   - [Github](https://github.com/Ivenrp)
- Apriyudha (H1H024010) - [Github](https://github.com/avriyyy)
- Mohammad Zulfan Ramadhan (H1H024008) - [Github](https://github.com/mzulfanr13-code) 
- Maharani Tri Wahyuningrum (H1H024012)

---

📋 Deskripsi Proyek
Sistem Parkir Otomatis ini merupakan implementasi mikrokontroler berbasis Arduino UNO yang mampu mengelola akses kendaraan ke area parkir secara otomatis. Sistem menggunakan kartu RFID sebagai identifikasi, sensor ultrasonik untuk deteksi kendaraan, servo motor untuk palang, dan LCD I2C untuk menampilkan status slot parkir secara real-time.
Sistem ini juga dilengkapi dengan sensor cahaya LDR yang secara otomatis menyalakan lampu parkir ketika kondisi gelap, serta tombol reset darurat berbasis interrupt eksternal.

✨ Fitur Utama
Autentikasi RFID — Hanya kendaraan dengan kartu terdaftar yang dapat masuk
Deteksi Kendaraan — Sensor ultrasonik mendeteksi keberadaan kendaraan di depan palang
Palang Otomatis — Servo motor membuka/menutup palang secara otomatis
Manajemen Slot — Memantau dan menampilkan jumlah slot parkir yang tersedia (kapasitas: 3 slot)
Lampu Otomatis — Sensor LDR menyalakan lampu parkir saat gelap/malam
Reset Darurat — Tombol reset via interrupt eksternal (INT0) untuk kondisi darurat
Notifikasi Buzzer — Umpan balik audio untuk setiap kondisi (valid, invalid, penuh)
Indikator LED 3-warna — Hijau (slot tersedia), Merah (penuh/invalid), Kuning (siap scan)
Display LCD I2C — Menampilkan status slot dan instruksi pengguna secara real-time
Non-blocking Timer — Hitung mundur penutupan palang menggunakan millis() tanpa memblokir loop Utama

🛠️ Kebutuhan Hardware (Komponen)
Arduino Uno
Sensor RFID MFRC522 + Kartu/Gantungan Kunci RFID
Motor Servo (SG90/MG996R)
Sensor Ultrasonik HC-SR04
Sensor Cahaya LDR
Layar LCD 16x2 dengan Modul I2C
Buzzer Aktif
3x LED (Hijau, Merah, Kuning)
1x LED Tambahan / Modul Relay (Untuk Lampu Parkir)
1x Push Button (Untuk *Interrupt/Reset*)
Kabel Jumper secukupnya

🔌 Konfigurasi Pin
| Fungsi          | Pin |
| --------------- | --- |
| RFID SS         | 10  |
| RFID RST        | 9   |
| Servo           | 6   |
| Ultrasonik TRIG | 5   |
| Ultrasonik ECHO | 4   |
| Buzzer          | 7   |
| LED Hijau       | 8   |
| LED Merah       | 3   |
| Button Reset    | 2   |
| LED Kuning      | A0  |
| LDR             | A1  |
| Lampu Parkir    | A2  |

📚 Library yang Digunakan
1. `SPI.h` 
2. `Wire.h` 
3. `Servo.h` 
4. `MFRC522` 
5. `LiquidCrystal_I2C` 

## 🔄 Alur Kerja Rangkaian (Workflow)

Sistem ini beroperasi berdasarkan alur logika berikut, mulai dari sistem dinyalakan hingga penanganan kondisi darurat:

### 1. Fase Startup (Inisialisasi)
Saat sistem pertama kali dinyalakan:
* Semua indikator LED dalam keadaan **mati**.
* Palang parkir otomatis berada dalam posisi **tertutup**.
* Layar LCD menyala dan menampilkan pesan: `"Sistem Parkir Siap Beroperasi"`.

### 2. Fase Deteksi Kendaraan
Sistem terus memantau area depan gerbang menggunakan sensor Ultrasonik:
* Jika terdeteksi ada kendaraan mendekat pada jarak **< 120 cm**, sistem bersiap.
* **LED Kuning menyala** sebagai indikator *standby*, dan layar LCD menampilkan instruksi: `"Silahkan Scan"`.

### 3. Fase Verifikasi Akses (Scan RFID)
Saat pengendara melakukan tap kartu pada *reader*, sistem akan memberikan respons berdasarkan tiga kondisi:
* **Kartu Valid & Slot Tersedia:** **LED Hijau berkedip**, *buzzer* berbunyi 1 kali, dan palang pintu **terbuka** otomatis.
* **Kartu Invalid (Tidak Terdaftar):** **LED Merah menyala**, *buzzer* berbunyi cepat secara berulang, dan akses **ditolak** (palang tetap tertutup).
* **Slot Parkir Penuh:** Meskipun kartu terdaftar, jika kapasitas sudah penuh (3 kendaraan), **LED Merah menyala**, *buzzer* berbunyi 3 kali, dan akses **ditolak**.

### 4. Fase Kendaraan Masuk & Penutupan Otomatis
Setelah palang terbuka (akses diterima):
* Sensor Ultrasonik memantau pergerakan kendaraan yang melintasi palang.
* Jika kendaraan berada tepat di bawah palang (jarak **≤ 3 cm**), sistem akan memulai **hitung mundur 5 detik**.
* Setelah jeda 5 detik selesai, palang akan **menutup secara otomatis** dan sistem mencatat penambahan 1 slot kendaraan.

### 5. Sistem Penerangan Parkir (Otomatis)
Sensor cahaya (LDR) bekerja secara independen untuk mengatur penerangan:
* **Malam/Gelap:** Sensor LDR mendeteksi minimnya cahaya, lampu putih area parkir otomatis **menyala**.
* **Siang/Terang:** Sensor LDR mendeteksi cahaya cukup, lampu otomatis **mati**.

### 6. Mode Reset Darurat (*Emergency Reset*)
Sistem dilengkapi dengan tombol *Push Button* (Interrupt) untuk kondisi darurat:
* Kapan pun tombol ditekan, sistem akan **mereset semua status** secara instan.
* Palang pintu langsung tertutup, dan data slot parkir **dikosongkan kembali menjadi 0**.
