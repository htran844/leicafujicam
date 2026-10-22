# REBRAND — Các thay đổi so với mã nguồn gốc

Dự án này là bản sửa đổi (modified version) của **leicafujicam**
(https://github.com/bjzhou/leicafujicam), Apache License 2.0.

Thay đổi do Hoàng & Muse thực hiện (2026-10-21):
- Đổi tên ứng dụng thành **leicafujicam** (hiển thị: LeicaFujiCam), version 1.0.0 (code 1)
- Đổi package/namespace: `com.hinnka.mycamera` → `vn.hoang.leicafujicam`
- Thay icon launcher mới; đổi tên theme `Theme.MyCamera` → `Theme.LeicaFujiCam`
- Đổi nhận diện hiển thị: tên file ảnh `leicafujicam_*`, thư mục lưu `DCIM/leicafujicam`,
  thẻ EXIF Software = `leicafujicam`, nhãn Pro/donation/community trong Cài đặt
- Gỡ endpoint máy chủ của tác giả gốc: API built-in → `https://api.openai.com/v1` (không kèm key),
  kiểm tra cập nhật → endpoint trung lập (vô hiệu), link Telegram/QQ/Alipay của tác giả → gỡ
- Vô hiệu hóa Bugly crash-report với app-id của tác giả gốc (không gửi dữ liệu crash đi đâu)
- Thêm credit trong màn hình About: "Phát triển bởi Hoàng & Muse • Dựa trên mã nguồn mở leicafujicam (Apache License 2.0)"

File `LICENSE` (Apache License 2.0) của dự án gốc được giữ nguyên không thay đổi.
Một số định danh nội bộ trong mã nguồn (tên class/thuật toán như PhotonLookProvider,
rawPhotonPgtmToneMap...) được giữ nguyên vì lý do kỹ thuật — chúng không phải nhận diện hiển thị tới người dùng.

## Bản 1.0.1 — sửa lỗi văng app khi bấm chụp
Bản 1.0.0 còn sót tên package cũ dạng JNI trong mã C++ (`com/hinnka/mycamera` ở FindClass/chữ ký
hàm và 68 symbol `Java_com_hinnka_mycamera_*`). Kotlin đã mang package mới nên khi bấm chụp,
pipeline xử lý ảnh native không tìm thấy class/symbol → app sập, không lưu được ảnh.
Đã đổi toàn bộ sang `vn/hoang/leicafujicam` / `Java_vn_hoang_leicafujicam_*` trong
`app/src/main/cpp` và `app/src/test/cpp`. Giữ nguyên định danh định dạng preset
`com.hinnka.photon.preset` (PresetPackageManager) vì đó là marker tương thích file preset,
không phải đường dẫn class.

## Bản 1.0.2 — tiếng Việt + bộ lọc mới
- Thêm locale Tiếng Việt: `res/values-vi/strings.xml` (dịch toàn bộ 1.433 chuỗi giao diện),
  đăng ký `vi` trong `locales_config.xml` và thêm lựa chọn "Tiếng Việt" trong Cài đặt ngôn ngữ
  (SettingsScreen). Tên bộ lọc có bản địa hóa `vi` trong `assets/luts/config.json`;
  `LutConfig.getName()` tra theo mã ngôn ngữ thực tế (trước đây cứng en/zh).
- Bộ lọc mới (LUT .plut tự tạo theo đặc tính màu, script `tools/make_custom_luts.py`):
  Kodak Portra 400, Kodak Gold 200, Teal & Orange, Miyazaki Anime.
  "CC"/"NC" có sẵn được đổi tên đầy đủ: Fujifilm Classic Chrome / Fujifilm Classic Negative.
- Favorites mặc định (config.json, theo thứ tự): Leica, Fujifilm Classic Chrome, Hasselblad,
  Miyazaki Anime, Kodak Portra 400, Kodak Gold 200, Fujifilm Classic Negative, Teal & Orange.

## Bản 1.0.3 — tab mặc định khi mở bộ chọn LUT
Bộ chọn bộ lọc (LutSelector) mở lên ở tab "Yêu thích" thay vì "Tích hợp sẵn".
Effect bám tab chỉ còn nhảy khi LUT đang dùng thay đổi thật trong lúc bộ chọn đang mở;
nếu danh sách yêu thích trống thì mặc định quay về tab Tích hợp sẵn.

## Bản 1.0.4 — nền tảng gói LUT theo máy (offline)
Thêm cơ chế gói LUT riêng theo model máy, đóng kèm trong APK (không cần mạng):
`DeviceLutPack` đọc `assets/luts/devices/index.json`, khớp manufacturer + Build.MODEL;
LUT trong gói đè LUT chung cùng id khi LutManager khởi tạo. Quy trình hiệu chuẩn bằng
bảng ColorChecker và công cụ PC nằm ở `docs/device-lut-calibration.md` +
`tools/calibration/`. Danh sách gói hiện để trống — gói thật đầu tiên sẽ được tạo khi
có ảnh chụp bảng màu từ máy thật.
