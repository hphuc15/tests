# test_ili9341_lib

Repo này chứa folder tôi đã phát triển thư viện ili9341, trong này có một số thư như:
- `ili9341/tests/ORIGIN__ESPRESSIF_ILI9341__ESP_LCD`: Đây là khối thư viện gốc mà esp-idf viết gồm component `esp_lcd` (tôi đã rút gọn những phần không liên quan, chỉ còn lại những thứ liên quan tới ili9341) và component `espressif__esp_lcd_ili9341-v2.0.2` (đây là driver mà esp-idf viết để tích hợp vào đống esp_lcd)`.

- `ili9341/tests/SIMPLIFY__ESPRESSIF_ILI9341__ESP_LCD`: cái này là component `espressif__esp_lcd_ili9341-v2.0.2` nhưng tôi đã gộp tất cả logic lại trong 1 cặp file src + header, mục đích là để dễ đọc hơn, đỡ phải tìm từng file trong driver gốc, mất thời gian.

- `ili9341/tests/USAGE_EXAMPLE__ESPRESSIF_ILI9341`: cái này là example chạy thử `espressif__esp_lcd_ili9341-v2.0.2` để biết cách dùng, flow hoạt động.

Note: thật ra đây là bản thư viện cũ rồi, bản mới nhất tôi có update thêm một số thứ cho nên hãy lên `hphuc15/ili9341' để xem mã nguồn thư viện của tôi còn mấy cái ở đây chỉ là tôi thiết lập để refactor thư viện thôi.