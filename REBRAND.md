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

## Bản 1.0.5 — hiệu chuẩn màu ngay trên điện thoại
Thêm màn "Hiệu chuẩn màu theo máy" trong Hộp công cụ (Toolbox): chọn ảnh chụp bảng
ColorChecker 24 (chụp bằng chính app, bộ lọc "Không"), kéo 4 tay nắm vào lưới ô màu
(hướng bảng tự nhận diện), app trích 24 ô, fit hiệu chỉnh đa thức trong không gian
tuyến tính, báo ΔE2000 trước/sau, tổ hợp hiệu chỉnh với các phong cách tích hợp đã
chọn rồi lưu thành LUT tùy chỉnh (mục "Device Calibration") dùng ngay trên máy.
Kèm: kiểm chứng bằng ảnh chụp độc lập thứ hai, xóa hiệu chuẩn, và xuất gói ZIP đúng
layout `assets/luts/devices/` để bake vào bản phát hành cho mọi người dùng cùng model.
Module: `lut/calibration/` (ChartCalibration, PlutIO, CalibrationPackStore) +
`ui/settings/DeviceCalibrationScreen.kt`. Chuỗi giao diện có đủ en/vi.

## Bản 1.0.6 — đổi tên Miyazaki Anime → Tomosaki, tạo lại LUT theo ảnh thật
Đổi tên toàn diện (id, file .plut, tên hiển thị cả 3 ngôn ngữ) từ `miyazaki_anime`
sang `tomosaki`, giữ nguyên vị trí thứ 4 trong favorites. LUT được thiết kế lại
theo phân tích ảnh đã xuất bản của nhiếp ảnh gia tomosaki (@photono_gen): trời
xanh cobalt đậm (hue đo ~206, bão hòa .64), xanh lá ngọc lục bảo (~110, .55),
da ấm tự nhiên (~24, .41), vùng sáng trung tính hơi lạnh, tương phản mạnh với
đen thật. Thông số trong `tools/make_custom_luts.py` (khóa `tomosaki`).

## 1.0.7 — Hướng dẫn tạo dáng (Pose Guide)

Tính năng chọn dáng theo bối cảnh với lớp phủ hình que trực tiếp trên camera
(Hộp công cụ → Hướng dẫn tạo dáng). Dữ liệu khởi điểm (76 dáng, 23 bối cảnh,
khung xương 13 điểm + mẹo cho người mẫu/người chụp) được chuyển thể từ dự án
mở **PoseGuide** (https://github.com/mergeos-bounties/PoseGuide), giấy phép
MIT, Copyright (c) 2026 MergeOS / ThanhTrucSolutions — toàn văn giấy phép nằm
tại `app/src/main/assets/poses/POSEGUIDE-LICENSE.txt`, dữ liệu đóng gói tại
`app/src/main/assets/poses/poses.json` (sinh bởi
`tools/pose-data/build_pose_catalog.py`).

## 1.0.8 — Chia dáng Nam/Nữ + ảnh mẫu kèm hình dáng

- Catalog mở rộng lên 110 dáng với trục giới tính: 76 dáng PoseGuide được gắn
  nhãn theo thực hành tạo dáng chuẩn (nữ 7, nam 6, còn lại dùng chung), cộng
  20 dáng tự thiết kế riêng cho nữ (10) và nam (10) trong
  `tools/pose-data/extra_poses.json` (khung xương + mẹo gốc của dự án,
  en/vi). Màn duyệt dáng có thêm hàng chip lọc Tất cả / Nữ / Nam (dáng dùng
  chung luôn hiển thị ở cả hai phía).
- 14 dáng kèm **ảnh mẫu gốc + hình dáng** thu thập từ các bài hướng dẫn tạo
  dáng công khai: 8 dáng nữ từ guide của shram.kiev.ua (mỗi ảnh ghi tên
  nhiếp ảnh gia ngay trong app, kèm link bài gốc) và 6 dáng nam từ Modern
  Gentleman Magazine. Khung xương 13 điểm được trích tự động bằng MediaPipe
  PoseLandmarker từ chính ảnh mẫu (kiểm chứng trực quan từng dáng; 1 dáng bị
  trích lỗi đã loại, 1 dáng sửa tay thủ công theo ảnh). Ảnh mẫu hiện ở lưới,
  panel chi tiết và trong lớp phủ camera (nút chuyển Ảnh mẫu / Hình dáng).
  Ảnh thuộc bản quyền của tác giả gốc — app giữ nguyên ghi công và link
  nguồn; chỉ phục vụ tham khảo cá nhân.

## 1.0.9 — LUT "Amelia" + ảnh ví dụ cho mọi LUT

- LUT mới **Amelia** (Creative, Yêu thích): tái tạo phong cách bộ ảnh vườn
  cẩm tú cầu của TikTok @conceptbyamelia do Hoàng gửi — đo thống kê trực tiếp
  trên 6 ảnh gốc (bóng đổ ám xanh lá và được nâng nhẹ, xanh lá ngả vàng hue
  ~104 bão hòa cao, da đào hue ~33, xanh trời/hồng pastel bão hòa thấp, trắng
  gần trung tính hơi ngả xanh), thông số ghi trong `tools/make_custom_luts.py`.
- **Quy ước mới (theo yêu cầu của Hoàng): mọi LUT đóng gói đều có ảnh ví dụ.**
  Ảnh nằm ở `assets/luts/examples/<lutId>/`, màn chọn LUT hiện một dải "Ảnh
  ví dụ" theo LUT đang chọn, bấm vào xem toàn màn hình. Amelia dùng chính 6
  ảnh gốc (ghi công "TikTok @conceptbyamelia" trong config + trong app);
  30 LUT còn lại được sinh tự động bằng `tools/make_lut_examples.py` — áp
  chính LUT đó lên một ảnh tham chiếu chung (ảnh xe đạp trong bộ Amelia) nên
  so sánh màu giữa các LUT rất trực quan; riêng nhóm LUT khôi phục Log không
  sinh ảnh (là biến đổi kỹ thuật cho footage log, ảnh sRGB sẽ gây hiểu sai).
  LUT mới thêm vào app bắt buộc kèm ảnh ví dụ theo quy ước này.
