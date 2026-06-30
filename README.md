# OTA Test Demo - ESP32-S3 + GitHub Pages (HTTPS)

Demo nhanh tính năng OTA: ESP32-S3 tải `firmware.bin` (hiện nằm ở repo `demos` branch `ota`) qua HTTPS từ GitHub Pages, trigger bằng lệnh `ota` trên Serial Monitor.

## 1. Cấu trúc project

```
ota_test_demo/
├── CMakeLists.txt
├── partitions.csv          # 2 OTA slots (ota_0 / ota_1)
├── sdkconfig.defaults       # cert bundle, rollback, console
└── main/
    ├── CMakeLists.txt
    ├── app_config.h         # SSID/password + URL firmware
    ├── main.c
    ├── wifi_connect.c/.h
    ├── ota_update.c/.h
    └── console_cmds.c/.h
```

## 2. Build lần đầu (firmware v1 - "bản gốc")

```bash
cd ota_test_demo
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor      # Linux/Mac
# idf.py -p COM5 flash monitor            # Windows
```

Sau khi flash xong, mở Serial Monitor, gõ:

```
version
```

Sẽ thấy version mặc định lấy từ `git describe` (nếu repo chưa có git tag thì IDF tự gán version dạng `1`). Để test OTA có ý nghĩa, nên gán version rõ ràng - xem bước 3.

## 3. Tạo repo GitHub Pages chứa firmware

```bash
# Tạo repo mới, ví dụ "ota-demo", bật GitHub Pages (Settings → Pages →
# Deploy from branch "main", root folder)

git clone https://github.com/<username>/ota-demo.git
cd ota-demo
```

Sửa `main/app_config.h` trong project ESP-IDF, đổi `OTA_FIRMWARE_URL` thành:
```c
#define OTA_FIRMWARE_URL "https://<username>.github.io/ota-demo/firmware.bin"
```

## 4. Build firmware "v2" để test OTA

Đặt version cho bản OTA test (để phân biệt với bản gốc), sửa trong `app_config.h`
hoặc đơn giản hơn - gán version qua CMake project version, hoặc chỉ cần sửa 1 dòng
log/print trong `main.c` (vd đổi text `"OTA Test Demo - firmware version"` thành
`"OTA Test Demo v2 - firmware version"`) rồi build lại:

```bash
cd ota_test_demo
idf.py build
```

File `.bin` sẽ nằm ở:
```
build/ota_test_demo.bin
```

Copy file này sang repo GitHub Pages:

```bash
cp build/ota_test_demo.bin ../ota-demo/firmware.bin
cd ../ota-demo
git add firmware.bin
git commit -m "firmware v2"
git push
```

Đợi 1-2 phút để GitHub Pages deploy xong. Kiểm tra bằng trình duyệt:
```
https://<username>.github.io/ota-demo/firmware.bin
```
Nếu trình duyệt tự tải file `.bin` về (không báo 404) là deploy thành công.

## 5. Test OTA trên thiết bị

Board đang chạy firmware "v1" (bản gốc, đã flash ở bước 2), mở Serial Monitor:

```bash
idf.py -p /dev/ttyUSB0 monitor
```

Gõ lệnh:
```
ota
```

Log mong đợi:
```
I (xxxx) ota_update: Starting OTA from: https://<username>.github.io/ota-demo/firmware.bin
I (xxxx) ota_update: New firmware version: ...
I (xxxx) ota_update: OTA progress: 10% (xxxx/xxxxx bytes)
I (xxxx) ota_update: OTA progress: 20% (...)
...
I (xxxx) ota_update: OTA progress: 100% (...)
I (xxxx) ota_update: OTA Success! Rebooting in 2 seconds...
```

Sau khi reboot, gõ lại `version` - sẽ thấy log "v2" xác nhận đã chạy firmware mới.

## 6. Các lệnh console có sẵn

| Lệnh      | Chức năng                                      |
|-----------|--------------------------------------------------|
| `ota`     | Trigger HTTPS OTA update từ URL trong app_config.h |
| `version` | In version, thời gian build, IDF version hiện tại |
| `reboot`  | Reboot thiết bị (test rollback)                  |
| `help`    | Liệt kê tất cả lệnh                              |

## 7. Test rollback (tùy chọn)

Để test cơ chế rollback (khi firmware mới bị lỗi, tự quay lại bản cũ):

1. Cố tình build 1 bản firmware bị crash ngay sau boot (vd: thêm `abort()` đầu `app_main`,
   đặt TRƯỚC dòng gọi `ota_confirm_boot_ok()`).
2. Upload bản này lên `firmware.bin`, chạy `ota` từ board đang chạy bản ổn định.
3. Thiết bị flash xong, reboot, crash ngay → bootloader phát hiện app chưa
   được `mark_app_valid` → tự động rollback về bản cũ ở lần reset tiếp theo.

## Lưu ý khi dùng GitHub Pages cho demo

- Đủ dùng cho test/demo, **không phù hợp cho production** (không versioning
  tự động, không kiểm soát băng thông, cache CDN có thể delay vài phút).
- GitHub Pages dùng chứng chỉ public (Let's Encrypt/DigiCert) nên
  `esp_crt_bundle_attach` (cert bundle có sẵn trong ESP-IDF) là đủ - không cần
  tự nhúng file `.pem`.
- Nếu thấy lỗi `ESP_ERR_HTTPS_OTA...` hoặc TLS handshake fail, thử lại sau
  vài phút - đôi khi do GitHub Pages cache chưa kịp deploy.
