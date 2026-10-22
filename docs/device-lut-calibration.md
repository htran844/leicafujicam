# Hiệu chuẩn LUT theo máy (offline) — quy trình

Mục tiêu: mỗi model máy có một "gói LUT theo máy" đã hiệu chuẩn từ ảnh chụp thực tế,
đóng kèm trong APK. Máy không có gói riêng dùng bộ LUT chung.

## Cách app chọn gói

- `assets/luts/devices/index.json`: danh sách gói `{key, manufacturer, models[], folder}`.
- Khi khởi tạo, `DeviceLutPack` khớp `Build.MANUFACTURER` + `Build.MODEL` của máy với
  index. Gói khớp được nạp từ `assets/luts/devices/<folder>/` (config.json cùng schema
  với `assets/luts/config.json`) và **đè lên các LUT chung có cùng id** trong
  `LutManager.initialize()`. Không khớp gói nào → dùng toàn bộ bản chung.

## Quy trình tạo gói cho một model máy mới

1. Trên máy thật, mở app, chọn LUT "Không" (None), khóa cân bằng trắng + phơi sáng,
   chụp bảng **X-Rite ColorChecker 24** dưới ánh sáng D65/đèn 5500K, bảng chiếm phần
   lớn khung hình, tránh bóng đổ/chói.
2. Chạy công cụ trên PC (xem `tools/calibration/README.md`):
   `python3 tools/calibration/calibrate.py --chart <ảnh> --device-key <slug> --manufacturer <hãng> --models <MODEL1,MODEL2>`
   Công cụ trích 24 ô màu, fit hiệu chỉnh, tổ hợp với các LUT phong cách, ghi thẳng
   gói vào `assets/luts/devices/<slug>/` + cập nhật `index.json`, kèm `report.json`
   chứa ΔE2000 trước/sau hiệu chuẩn.
3. Cổng chất lượng: ΔE00 trung bình < 3 (mục tiêu), ô tệ nhất < 6 — kiểm chứng bằng
   một ảnh chụp bảng thứ hai sau khi đã áp gói.
4. Build bản APK mới (tăng version), gói theo máy tự đến tay toàn bộ người dùng khi
   họ cập nhật app. Không cần server, không cần mạng.

## Ghi chú

- File .plut theo máy có cùng định dạng PLUT v1 / 33 / UINT16 như LUT chung (~216KB/file).
- Thứ tự áp dụng khi bake: LUT phong cách trước, LUT hiệu chỉnh của máy sau
  (out = correction(style(in))) — khớp với việc hiệu chỉnh đo trên đầu ra cuối của máy.
