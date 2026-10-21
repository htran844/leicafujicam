# Generates Fotufilm UI strings for every locale and the Kotlin resource mapping.
# Order per entry: en, zh-CN, zh-TW (also zh-HK and b+zh+Hant), ja, ko, de, fr, in, pt-BR, th
import re, os, sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "app", "src", "main")
LOCALES = ["values", "values-zh-rCN", "values-zh-rTW", "values-ja", "values-ko", "values-de",
           "values-fr", "values-in", "values-pt-rBR", "values-th"]
ALIASES = {"values-zh-rHK": "values-zh-rTW", "values-b+zh+Hant": "values-zh-rTW"}

S = {}
def add(key, *t):
    assert len(t) == 10, key
    S[key] = t

# Sections
add("fotufilm_section_filmStock", "Film", "胶片", "底片", "フィルム", "필름", "Film", "Pellicule", "Film", "Filme", "ฟิล์ม")
add("fotufilm_section_filmGrain", "Grain", "颗粒", "顆粒", "粒状", "입자", "Korn", "Grain", "Butiran", "Grão", "เกรน")
add("fotufilm_section_filmHalation", "Halation", "光晕", "光暈", "ハレーション", "헐레이션", "Lichthof", "Halo", "Halasi", "Halação", "ฮาเลชัน")
add("fotufilm_section_filmEmulsion", "Emulsion", "乳剂", "乳劑", "乳剤", "유제", "Emulsion", "Émulsion", "Emulsi", "Emulsão", "อิมัลชัน")
add("fotufilm_section_filmLab", "Lab", "冲洗", "沖洗", "現像", "현상", "Labor", "Laboratoire", "Lab", "Laboratório", "แล็บ")
add("fotufilm_section_printPaper", "Output", "输出", "輸出", "出力", "출력", "Ausgabe", "Sortie", "Keluaran", "Saída", "เอาต์พุต")
add("fotufilm_section_printLamp", "Lamp", "光源", "光源", "光源", "광원", "Lampe", "Lampe", "Lampu", "Lâmpada", "หลอดไฟ")
add("fotufilm_section_lightExposure", "Exposure", "曝光", "曝光", "露光", "노출", "Belichtung", "Exposition", "Eksposur", "Exposição", "การรับแสง")

# Control titles and descriptions
def ctl(field, titles, descs):
    add(f"fotufilm_control_{field}", *titles)
    add(f"fotufilm_control_{field}_description", *descs)

ctl("stock",
    ["Film", "胶片", "底片", "フィルム", "필름", "Film", "Pellicule", "Film", "Filme", "ฟิล์ม"],
    ["Choose the film used to render the photo.", "选择渲染照片所用的胶片。", "選擇算繪照片所用的底片。", "写真の描画に使うフィルムを選びます。", "사진 렌더링에 사용할 필름을 선택합니다.", "Wählen Sie den Film, mit dem das Foto entwickelt wird.", "Choisissez la pellicule utilisée pour le rendu de la photo.", "Pilih film yang digunakan untuk merender foto.", "Escolha o filme usado para renderizar a foto.", "เลือกฟิล์มที่ใช้เรนเดอร์ภาพ"])
ctl("gauge",
    ["Format", "画幅", "片幅", "フォーマット", "포맷", "Format", "Format", "Format", "Formato", "ขนาดฟิล์ม"],
    ["Set the film frame size, which affects visible grain and halation.", "设置胶片画幅，影响可见颗粒与光晕的尺度。", "設定底片片幅，影響可見顆粒與光暈的尺度。", "フィルムのフレームサイズを設定します。粒状とハレーションの見え方に影響します。", "필름 프레임 크기를 설정합니다. 보이는 입자와 헐레이션에 영향을 줍니다.", "Legt die Bildgröße auf dem Film fest; sie beeinflusst sichtbares Korn und Lichthöfe.", "Définit la taille de l’image sur le film, qui influe sur le grain et le halo visibles.", "Atur ukuran bingkai film, yang memengaruhi butiran dan halasi yang terlihat.", "Define o tamanho do quadro do filme, que afeta o grão e a halação visíveis.", "กำหนดขนาดเฟรมฟิล์ม ซึ่งส่งผลต่อเกรนและฮาเลชันที่มองเห็น"])
ctl("grain",
    ["Grain", "颗粒", "顆粒", "粒状", "입자", "Korn", "Grain", "Butiran", "Grão", "เกรน"],
    ["Adjust grain strength relative to the selected film.", "相对所选胶片调整颗粒强度。", "相對所選底片調整顆粒強度。", "選択したフィルムに対する粒状の強さを調整します。", "선택한 필름 대비 입자 강도를 조정합니다.", "Passt die Kornstärke relativ zum gewählten Film an.", "Règle l’intensité du grain par rapport à la pellicule choisie.", "Sesuaikan kekuatan butiran relatif terhadap film yang dipilih.", "Ajusta a intensidade do grão em relação ao filme selecionado.", "ปรับความแรงของเกรนเทียบกับฟิล์มที่เลือก"])
ctl("grainMottle",
    ["Mottle", "斑驳", "斑駁", "モトル", "모틀", "Mottle", "Marbrure", "Bercak", "Mosqueado", "ด่างเกรน"],
    ["Add larger, softer clusters to the grain pattern.", "为颗粒图案加入更大、更柔和的团簇。", "為顆粒圖案加入更大、更柔和的團簇。", "粒状パターンに大きく柔らかい集まりを加えます。", "입자 패턴에 더 크고 부드러운 뭉침을 더합니다.", "Fügt dem Kornmuster größere, weichere Cluster hinzu.", "Ajoute des amas plus gros et plus doux au motif de grain.", "Tambahkan gumpalan lebih besar dan lembut pada pola butiran.", "Adiciona aglomerados maiores e mais suaves ao padrão de grão.", "เพิ่มกลุ่มเกรนที่ใหญ่และนุ่มกว่าในลวดลายเกรน"])
ctl("filmGrainSize",
    ["Grain Size", "颗粒尺寸", "顆粒尺寸", "粒子サイズ", "입자 크기", "Korngröße", "Taille du grain", "Ukuran Butiran", "Tamanho do grão", "ขนาดเกรน"],
    ["Make the film's grain coarser or finer at the same measured granularity.", "在相同实测颗粒度下让胶片颗粒更粗或更细。", "在相同實測顆粒度下讓底片顆粒更粗或更細。", "同じ測定粒状度のまま、粒子を粗く、または細かくします。", "측정된 입상도는 그대로 두고 입자를 더 굵거나 곱게 만듭니다.", "Macht das Korn bei gleicher gemessener Körnigkeit gröber oder feiner.", "Rend le grain plus grossier ou plus fin à granularité mesurée identique.", "Buat butiran lebih kasar atau halus pada granularitas terukur yang sama.", "Torna o grão mais grosso ou mais fino com a mesma granularidade medida.", "ทำให้เกรนหยาบหรือละเอียดขึ้นโดยคงค่าความเป็นเกรนที่วัดได้"])
ctl("filmColourGrain",
    ["Colour Grain", "彩色颗粒", "彩色顆粒", "カラー粒状", "컬러 입자", "Farbkorn", "Grain coloré", "Butiran Warna", "Grão colorido", "เกรนสี"],
    ["Blend each layer's grain from independent colour speckle toward one shared grain.", "让各感光层的颗粒从独立的彩色斑点过渡到共享的单一颗粒。", "讓各感光層的顆粒從獨立的彩色斑點過渡到共享的單一顆粒。", "各層の粒状を独立した色斑点から共通の粒状へ混ぜ合わせます。", "각 층의 입자를 독립적인 색 반점에서 하나의 공유 입자로 섞습니다.", "Mischt das Korn jeder Schicht von unabhängigem Farbrauschen zu einem gemeinsamen Korn.", "Fait passer le grain de chaque couche d’un moucheté coloré indépendant à un grain commun.", "Campur butiran tiap lapisan dari bintik warna independen menuju satu butiran bersama.", "Mistura o grão de cada camada de pontos coloridos independentes para um grão comum.", "ผสมเกรนแต่ละชั้นจากจุดสีอิสระไปสู่เกรนร่วมเดียวกัน"])
ctl("filmRedLayer",
    ["Red Layer", "红感层", "紅感層", "赤感層", "적감층", "Rotschicht", "Couche rouge", "Lapisan Merah", "Camada vermelha", "ชั้นแดง"],
    ["Scale the grain of the red-sensitive layer when Grain Model is Film.", "颗粒模型为胶片时，缩放红感层的颗粒。", "顆粒模型為底片時，縮放紅感層的顆粒。", "粒状モデルがフィルムのとき、赤感層の粒状を調整します。", "입자 모델이 필름일 때 적감층 입자를 조정합니다.", "Skaliert das Korn der rotempfindlichen Schicht im Kornmodell Film.", "Ajuste le grain de la couche sensible au rouge quand le modèle de grain est Film.", "Skalakan butiran lapisan peka merah saat Model Butiran adalah Film.", "Ajusta o grão da camada sensível ao vermelho quando o modelo de grão é Filme.", "ปรับเกรนของชั้นไวแสงแดงเมื่อโมเดลเกรนเป็นฟิล์ม"])
ctl("filmGreenLayer",
    ["Green Layer", "绿感层", "綠感層", "緑感層", "녹감층", "Grünschicht", "Couche verte", "Lapisan Hijau", "Camada verde", "ชั้นเขียว"],
    ["Scale the grain of the green-sensitive layer when Grain Model is Film.", "颗粒模型为胶片时，缩放绿感层的颗粒。", "顆粒模型為底片時，縮放綠感層的顆粒。", "粒状モデルがフィルムのとき、緑感層の粒状を調整します。", "입자 모델이 필름일 때 녹감층 입자를 조정합니다.", "Skaliert das Korn der grünempfindlichen Schicht im Kornmodell Film.", "Ajuste le grain de la couche sensible au vert quand le modèle de grain est Film.", "Skalakan butiran lapisan peka hijau saat Model Butiran adalah Film.", "Ajusta o grão da camada sensível ao verde quando o modelo de grão é Filme.", "ปรับเกรนของชั้นไวแสงเขียวเมื่อโมเดลเกรนเป็นฟิล์ม"])
ctl("filmBlueLayer",
    ["Blue Layer", "蓝感层", "藍感層", "青感層", "청감층", "Blauschicht", "Couche bleue", "Lapisan Biru", "Camada azul", "ชั้นน้ำเงิน"],
    ["Scale the grain of the blue-sensitive layer when Grain Model is Film.", "颗粒模型为胶片时，缩放蓝感层的颗粒。", "顆粒模型為底片時，縮放藍感層的顆粒。", "粒状モデルがフィルムのとき、青感層の粒状を調整します。", "입자 모델이 필름일 때 청감층 입자를 조정합니다.", "Skaliert das Korn der blauempfindlichen Schicht im Kornmodell Film.", "Ajuste le grain de la couche sensible au bleu quand le modèle de grain est Film.", "Skalakan butiran lapisan peka biru saat Model Butiran adalah Film.", "Ajusta o grão da camada sensível ao azul quando o modelo de grão é Filme.", "ปรับเกรนของชั้นไวแสงน้ำเงินเมื่อโมเดลเกรนเป็นฟิล์ม"])
ctl("filmScanSoftness",
    ["Scan Softness", "扫描柔度", "掃描柔度", "スキャンの柔らかさ", "스캔 부드러움", "Scan-Weichheit", "Douceur du scan", "Kelembutan Pindai", "Suavidade do scan", "ความนุ่มของการสแกน"],
    ["Read a wider patch of film per pixel, as a softer scan does, smoothing the grain.", "像较软的扫描那样，每个像素读取更大的胶片区域，使颗粒更平滑。", "像較軟的掃描那樣，每個像素讀取更大的底片區域，使顆粒更平滑。", "柔らかいスキャンのように、1 画素あたりでより広い範囲を読み取り、粒状を滑らかにします。", "부드러운 스캔처럼 픽셀마다 더 넓은 필름 영역을 읽어 입자를 매끄럽게 합니다.", "Liest wie ein weicherer Scan pro Pixel eine größere Filmfläche und glättet das Korn.", "Lit une zone de film plus large par pixel, comme un scan plus doux, ce qui adoucit le grain.", "Membaca area film lebih luas per piksel seperti pindaian yang lebih lembut, menghaluskan butiran.", "Lê uma área maior do filme por pixel, como um scan mais suave, suavizando o grão.", "อ่านพื้นที่ฟิล์มกว้างขึ้นต่อพิกเซลเหมือนการสแกนที่นุ่มกว่า ทำให้เกรนเรียบขึ้น"])
ctl("grainModel",
    ["Grain Model", "颗粒模型", "顆粒模型", "粒状モデル", "입자 모델", "Kornmodell", "Modèle de grain", "Model Butiran", "Modelo de grão", "โมเดลเกรน"],
    ["Choose how film grain is formed across the emulsion.", "选择颗粒在乳剂中的形成方式。", "選擇顆粒在乳劑中的形成方式。", "乳剤内で粒状がどのように形成されるかを選びます。", "유제 전반에서 입자가 형성되는 방식을 선택합니다.", "Wählt, wie das Filmkorn in der Emulsion entsteht.", "Choisit comment le grain se forme dans l’émulsion.", "Pilih cara butiran terbentuk di seluruh emulsi.", "Escolhe como o grão se forma na emulsão.", "เลือกวิธีที่เกรนก่อตัวในอิมัลชัน"])
ctl("halation",
    ["Halation", "光晕", "光暈", "ハレーション", "헐레이션", "Lichthof", "Halo", "Halasi", "Halação", "ฮาเลชัน"],
    ["Adjust the glow around bright areas caused by light reflecting inside the film.", "调整光线在胶片内部反射造成的亮部周围辉光。", "調整光線在底片內部反射造成的亮部周圍輝光。", "フィルム内部の反射で明部の周囲に生じる光のにじみを調整します。", "필름 내부 반사로 밝은 영역 주변에 생기는 빛번짐을 조정합니다.", "Passt das Leuchten um helle Bereiche an, das durch Reflexion im Film entsteht.", "Règle la lueur autour des zones claires due à la réflexion de la lumière dans le film.", "Sesuaikan pendaran di sekitar area terang akibat pantulan cahaya di dalam film.", "Ajusta o brilho ao redor das áreas claras causado pela reflexão da luz dentro do filme.", "ปรับแสงเรืองรอบบริเวณสว่างที่เกิดจากแสงสะท้อนภายในฟิล์ม"])
ctl("halationColour",
    ["Halo Colour", "光晕颜色", "光暈顏色", "ハロの色", "헤일로 색", "Lichthof-Farbe", "Couleur du halo", "Warna Halo", "Cor do halo", "สีของฮาโล"],
    ["Control how much the halo keeps the color of the light source.", "控制光晕保留光源颜色的程度。", "控制光暈保留光源顏色的程度。", "ハロが光源の色をどれだけ保つかを調整します。", "헤일로가 광원의 색을 얼마나 유지할지 조정합니다.", "Steuert, wie stark der Lichthof die Farbe der Lichtquelle behält.", "Détermine dans quelle mesure le halo garde la couleur de la source lumineuse.", "Atur seberapa banyak halo mempertahankan warna sumber cahaya.", "Controla quanto o halo mantém a cor da fonte de luz.", "ควบคุมว่าฮาโลคงสีของแหล่งแสงไว้มากเพียงใด"])
ctl("couplers",
    ["Couplers", "成色剂", "成色劑", "カプラー", "커플러", "Kuppler", "Coupleurs", "Kopler", "Acopladores", "คัปเปลอร์"],
    ["Adjust how development affects color separation and edge contrast.", "调整显影对色彩分离和边缘反差的影响。", "調整顯影對色彩分離和邊緣反差的影響。", "現像が色分離とエッジコントラストに与える影響を調整します。", "현상이 색 분리와 가장자리 대비에 미치는 영향을 조정합니다.", "Passt an, wie die Entwicklung Farbtrennung und Kantenkontrast beeinflusst.", "Règle l’effet du développement sur la séparation des couleurs et le contraste des contours.", "Sesuaikan pengaruh pengembangan terhadap pemisahan warna dan kontras tepi.", "Ajusta como a revelação afeta a separação de cores e o contraste de bordas.", "ปรับผลของการล้างฟิล์มต่อการแยกสีและคอนทราสต์ขอบ"])
ctl("couplerReach",
    ["Separation", "层间分离", "層間分離", "層間分離", "층간 분리", "Trennung", "Séparation", "Pemisahan", "Separação", "การแยกชั้น"],
    ["Adjust how strongly neighboring film layers affect each other during development.", "调整显影时相邻感光层相互影响的强度。", "調整顯影時相鄰感光層相互影響的強度。", "現像中に隣接する層が互いに及ぼす影響の強さを調整します。", "현상 중 인접한 필름 층이 서로 미치는 영향의 강도를 조정합니다.", "Legt fest, wie stark sich benachbarte Filmschichten bei der Entwicklung beeinflussen.", "Règle l’influence mutuelle des couches voisines pendant le développement.", "Sesuaikan seberapa kuat lapisan film yang berdekatan saling memengaruhi saat pengembangan.", "Ajusta o quanto camadas vizinhas do filme se influenciam durante a revelação.", "ปรับความแรงที่ชั้นฟิล์มข้างเคียงส่งผลต่อกันระหว่างการล้าง"])
ctl("couplerSelf",
    ["Edge Contrast", "边缘反差", "邊緣反差", "エッジコントラスト", "가장자리 대비", "Kantenkontrast", "Contraste des contours", "Kontras Tepi", "Contraste de borda", "คอนทราสต์ขอบ"],
    ["Adjust the local contrast around edges created during development.", "调整显影在边缘周围形成的局部反差。", "調整顯影在邊緣周圍形成的局部反差。", "現像で生じるエッジ周辺の局所コントラストを調整します。", "현상 중 가장자리 주변에 생기는 국부 대비를 조정합니다.", "Passt den lokalen Kontrast an Kanten an, der bei der Entwicklung entsteht.", "Règle le contraste local autour des contours créé au développement.", "Sesuaikan kontras lokal di sekitar tepi yang terbentuk saat pengembangan.", "Ajusta o contraste local ao redor das bordas criado na revelação.", "ปรับคอนทราสต์เฉพาะที่รอบขอบที่เกิดขึ้นระหว่างการล้าง"])
ctl("chromaticFringeAmount",
    ["Fringe Amount", "色边强度", "色邊強度", "フリンジ量", "프린지 양", "Farbsaum-Stärke", "Intensité de frange", "Jumlah Pinggiran", "Intensidade da franja", "ปริมาณขอบสี"],
    ["Add a broader color effect around edges.", "在边缘周围加入更宽的色彩效果。", "在邊緣周圍加入更寬的色彩效果。", "エッジ周辺により広い色の効果を加えます。", "가장자리 주변에 더 넓은 색 효과를 더합니다.", "Fügt um Kanten einen breiteren Farbeffekt hinzu.", "Ajoute un effet coloré plus large autour des contours.", "Tambahkan efek warna yang lebih lebar di sekitar tepi.", "Adiciona um efeito de cor mais amplo ao redor das bordas.", "เพิ่มเอฟเฟกต์สีที่กว้างขึ้นรอบขอบ"])
ctl("chromaticFringeRadius",
    ["Fringe Radius", "色边半径", "色邊半徑", "フリンジ半径", "프린지 반경", "Farbsaum-Radius", "Rayon de frange", "Radius Pinggiran", "Raio da franja", "รัศมีขอบสี"],
    ["Set the width of the color fringe. Requires Fringe Amount above zero.", "设置色边宽度，需色边强度大于零。", "設定色邊寬度，需色邊強度大於零。", "色フリンジの幅を設定します。フリンジ量が 0 より大きい必要があります。", "색 프린지의 폭을 설정합니다. 프린지 양이 0보다 커야 합니다.", "Legt die Breite des Farbsaums fest. Erfordert eine Farbsaum-Stärke über null.", "Définit la largeur de la frange colorée. Nécessite une intensité de frange supérieure à zéro.", "Atur lebar pinggiran warna. Memerlukan Jumlah Pinggiran di atas nol.", "Define a largura da franja colorida. Requer intensidade da franja acima de zero.", "กำหนดความกว้างของขอบสี ต้องมีปริมาณขอบสีมากกว่าศูนย์"])
ctl("push",
    ["Push", "增感/减感", "增感/減感", "増減感", "증감", "Push/Pull", "Poussage", "Push", "Puxar/Empurrar", "พุช/พูล"],
    ["Develop longer or shorter, estimated from the process's published push response.", "延长或缩短显影，依据冲洗工艺公布的增感响应估算。", "延長或縮短顯影，依據沖洗工藝公布的增感響應估算。", "現像時間を延長・短縮します。処理の公表された増感特性から推定します。", "현상 시간을 늘리거나 줄입니다. 공정의 공개된 증감 응답으로 추정합니다.", "Länger oder kürzer entwickeln, geschätzt aus der veröffentlichten Push-Kennlinie des Prozesses.", "Développement prolongé ou raccourci, estimé d’après la réponse au poussage publiée du procédé.", "Kembangkan lebih lama atau singkat, diperkirakan dari respons push yang dipublikasikan proses.", "Revelação mais longa ou curta, estimada pela resposta de push publicada do processo.", "ล้างนานขึ้นหรือสั้นลง ประมาณจากผลตอบสนองการพุชที่กระบวนการเผยแพร่"])
ctl("bleach",
    ["Bleach Bypass", "跳漂白", "跳漂白", "銀残し", "블리치 바이패스", "Bleach Bypass", "Sans blanchiment", "Bleach Bypass", "Bleach bypass", "ข้ามการฟอก"],
    ["Leave silver in the negative to increase contrast and reduce color saturation.", "在负片中保留银，提高反差并降低饱和度。", "在負片中保留銀，提高反差並降低飽和度。", "ネガに銀を残し、コントラストを上げて彩度を下げます。", "네거티브에 은을 남겨 대비를 높이고 채도를 낮춥니다.", "Lässt Silber im Negativ, erhöht den Kontrast und senkt die Sättigung.", "Laisse l’argent dans le négatif pour augmenter le contraste et réduire la saturation.", "Biarkan perak dalam negatif untuk menaikkan kontras dan menurunkan saturasi.", "Mantém prata no negativo para aumentar o contraste e reduzir a saturação.", "คงเงินไว้ในฟิล์มเนกาทีฟเพื่อเพิ่มคอนทราสต์และลดความอิ่มสี"])
ctl("expired",
    ["Expired", "过期", "過期", "期限切れ", "유효기간 경과", "Abgelaufen", "Périmée", "Kedaluwarsa", "Vencido", "หมดอายุ"],
    ["Simulate film stored at room temperature past its expiry date, in years.", "模拟在室温下存放并过期的胶片（以年计）。", "模擬在室溫下存放並過期的底片（以年計）。", "室温で保管され期限を過ぎたフィルムを年単位で再現します。", "실온에서 보관되어 유효기간이 지난 필름을 연 단위로 재현합니다.", "Simuliert Film, der bei Raumtemperatur über das Verfallsdatum hinaus gelagert wurde, in Jahren.", "Simule une pellicule conservée à température ambiante au-delà de sa date, en années.", "Simulasikan film yang disimpan pada suhu ruang melewati tanggal kedaluwarsa, dalam tahun.", "Simula filme guardado em temperatura ambiente após o vencimento, em anos.", "จำลองฟิล์มที่เก็บในอุณหภูมิห้องเลยวันหมดอายุ เป็นจำนวนปี"])
ctl("shutter",
    ["Long Exposure", "长曝光", "長曝光", "長時間露光", "장노출", "Langzeitbelichtung", "Pose longue", "Eksposur Panjang", "Longa exposição", "เปิดรับแสงนาน"],
    ["Set exposure time to apply the film's measured long-exposure correction.", "设置曝光时间，以应用胶片实测的长曝光（倒易律失效）校正。", "設定曝光時間，以套用底片實測的長曝光（倒易律失效）校正。", "露光時間を設定し、フィルムの実測された相反則不軌補正を適用します。", "노출 시간을 설정해 필름의 측정된 장노출(상반칙 불궤) 보정을 적용합니다.", "Legt die Belichtungszeit fest, um die gemessene Langzeitkorrektur des Films anzuwenden.", "Définit le temps de pose pour appliquer la correction de pose longue mesurée du film.", "Atur waktu eksposur untuk menerapkan koreksi eksposur panjang terukur dari film.", "Define o tempo de exposição para aplicar a correção de longa exposição medida do filme.", "ตั้งเวลารับแสงเพื่อใช้การแก้ไขการรับแสงนานที่วัดได้ของฟิล์ม"])
ctl("cameraPreflash",
    ["Camera Preflash", "相机预曝光", "相機預曝光", "カメラ側プリフラッシュ", "카메라 프리플래시", "Kamera-Vorbelichtung", "Pré-exposition caméra", "Pra-kilat Kamera", "Pré-exposição na câmera", "การฉายแสงล่วงหน้าในกล้อง"],
    ["Pre-expose the camera frame to lift shadow contrast.", "预先曝光胶片画面，以抬起暗部反差。", "預先曝光底片畫面，以抬起暗部反差。", "フレームを事前露光し、シャドウのコントラストを持ち上げます。", "프레임을 미리 노출해 어두운 부분 대비를 끌어올립니다.", "Belichtet das Bild vor, um den Schattenkontrast anzuheben.", "Pré-expose l’image pour relever le contraste des ombres.", "Pra-ekspos bingkai untuk mengangkat kontras bayangan.", "Pré-expõe o quadro para elevar o contraste das sombras.", "ฉายแสงล่วงหน้าบนเฟรมเพื่อยกคอนทราสต์ในเงา"])
ctl("paper",
    ["Output Medium", "输出介质", "輸出介質", "出力メディア", "출력 매체", "Ausgabemedium", "Support de sortie", "Media Keluaran", "Meio de saída", "สื่อเอาต์พุต"],
    ["Choose how the film is printed, scanned, or viewed.", "选择胶片的印放、扫描或观看方式。", "選擇底片的印放、掃描或觀看方式。", "フィルムをプリント、スキャン、または直接観察する方法を選びます。", "필름을 인화, 스캔 또는 직접 보는 방식을 선택합니다.", "Wählt, wie der Film vergrößert, gescannt oder betrachtet wird.", "Choisit comment le film est tiré, numérisé ou regardé.", "Pilih cara film dicetak, dipindai, atau dilihat.", "Escolhe como o filme é ampliado, digitalizado ou visto.", "เลือกวิธีอัดภาพ สแกน หรือดูฟิล์ม"])
ctl("printLight",
    ["Viewing Illuminant", "观看光源", "觀看光源", "観察光源", "관찰 광원", "Betrachtungslicht", "Éclairage d’observation", "Iluminan Pengamatan", "Iluminante de visualização", "แสงที่ใช้ดูภาพ"],
    ["Choose the light used to view the print.", "选择观看照片时使用的光源。", "選擇觀看照片時使用的光源。", "プリントを観察する光源を選びます。", "인화물을 볼 때 사용할 광원을 선택합니다.", "Wählt das Licht, unter dem der Abzug betrachtet wird.", "Choisit la lumière utilisée pour regarder le tirage.", "Pilih cahaya untuk melihat hasil cetak.", "Escolhe a luz usada para ver a ampliação.", "เลือกแสงที่ใช้ดูภาพพิมพ์"])
ctl("printCorrection",
    ["Channel Contrast Match", "通道反差匹配", "通道反差匹配", "チャンネル コントラスト整合", "채널 대비 정합", "Kanalkontrast-Abgleich", "Égalisation du contraste des canaux", "Penyesuaian Kontras Kanal", "Equalização de contraste dos canais", "ปรับคอนทราสต์ช่องสีให้ตรงกัน"],
    ["Optional digital channel-contrast correction; not a printer-light adjustment.", "可选的数字通道反差校正，并非配光调整。", "可選的數位通道反差校正，並非配光調整。", "任意のデジタル チャンネル コントラスト補正です。プリンター光量の調整ではありません。", "선택적 디지털 채널 대비 보정이며 프린터 광량 조정이 아닙니다.", "Optionale digitale Kanalkontrast-Korrektur, keine Lichtbestimmung.", "Correction numérique facultative du contraste des canaux ; ce n’est pas un réglage d’étalonnage.", "Koreksi kontras kanal digital opsional; bukan penyesuaian cahaya printer.", "Correção digital opcional de contraste dos canais; não é ajuste de luzes de copiagem.", "การแก้คอนทราสต์ช่องสีแบบดิจิทัลที่เลือกใช้ได้ ไม่ใช่การปรับแสงเครื่องพิมพ์"])
ctl("printerRed",
    ["Red Light", "红光", "紅光", "レッド光量", "레드 광량", "Rotlicht", "Lumière rouge", "Cahaya Merah", "Luz vermelha", "แสงแดง"],
    ["Printer points; more red light adds cyan to the print.", "配光点数；红光越多，照片越偏青。", "配光點數；紅光越多，照片越偏青。", "プリンター ポイント。赤を増やすとプリントにシアンが加わります。", "프린터 포인트. 빨간빛이 많을수록 인화물에 시안이 더해집니다.", "Printerpunkte; mehr Rotlicht fügt dem Abzug Cyan hinzu.", "Points de tirage ; plus de rouge ajoute du cyan au tirage.", "Poin printer; lebih banyak cahaya merah menambah sian pada cetakan.", "Pontos de copiagem; mais luz vermelha adiciona ciano à ampliação.", "จุดแสงเครื่องพิมพ์ แสงแดงมากขึ้นจะเพิ่มสีฟ้าอมเขียวในภาพ"])
ctl("printerGreen",
    ["Green Light", "绿光", "綠光", "グリーン光量", "그린 광량", "Grünlicht", "Lumière verte", "Cahaya Hijau", "Luz verde", "แสงเขียว"],
    ["Printer points; more green light adds magenta to the print.", "配光点数；绿光越多，照片越偏品红。", "配光點數；綠光越多，照片越偏洋紅。", "プリンター ポイント。緑を増やすとプリントにマゼンタが加わります。", "프린터 포인트. 초록빛이 많을수록 인화물에 마젠타가 더해집니다.", "Printerpunkte; mehr Grünlicht fügt dem Abzug Magenta hinzu.", "Points de tirage ; plus de vert ajoute du magenta au tirage.", "Poin printer; lebih banyak cahaya hijau menambah magenta pada cetakan.", "Pontos de copiagem; mais luz verde adiciona magenta à ampliação.", "จุดแสงเครื่องพิมพ์ แสงเขียวมากขึ้นจะเพิ่มสีม่วงแดงในภาพ"])
ctl("printerBlue",
    ["Blue Light", "蓝光", "藍光", "ブルー光量", "블루 광량", "Blaulicht", "Lumière bleue", "Cahaya Biru", "Luz azul", "แสงน้ำเงิน"],
    ["Printer points; more blue light adds yellow to the print.", "配光点数；蓝光越多，照片越偏黄。", "配光點數；藍光越多，照片越偏黃。", "プリンター ポイント。青を増やすとプリントにイエローが加わります。", "프린터 포인트. 파란빛이 많을수록 인화물에 노랑이 더해집니다.", "Printerpunkte; mehr Blaulicht fügt dem Abzug Gelb hinzu.", "Points de tirage ; plus de bleu ajoute du jaune au tirage.", "Poin printer; lebih banyak cahaya biru menambah kuning pada cetakan.", "Pontos de copiagem; mais luz azul adiciona amarelo à ampliação.", "จุดแสงเครื่องพิมพ์ แสงน้ำเงินมากขึ้นจะเพิ่มสีเหลืองในภาพ"])
ctl("digitalReference",
    ["Screen Conversion", "屏幕转换", "螢幕轉換", "画面変換", "화면 변환", "Bildschirmkonvertierung", "Conversion écran", "Konversi Layar", "Conversão para tela", "การแปลงสำหรับหน้าจอ"],
    ["Choose how developed film is converted on Digital Reference.", "选择数字参考输出时显影后胶片的转换方式。", "選擇數位參考輸出時顯影後底片的轉換方式。", "デジタル リファレンスで現像済みフィルムを変換する方法を選びます。", "디지털 레퍼런스에서 현상된 필름을 변환하는 방식을 선택합니다.", "Wählt, wie entwickelter Film bei Digitaler Referenz umgewandelt wird.", "Choisit comment le film développé est converti en Référence numérique.", "Pilih cara film yang dikembangkan dikonversi pada Referensi Digital.", "Escolhe como o filme revelado é convertido na Referência digital.", "เลือกวิธีแปลงฟิล์มที่ล้างแล้วในโหมดอ้างอิงดิจิทัล"])
ctl("screenCyan",
    ["Cyan / Red", "青 / 红", "青 / 紅", "シアン / レッド", "시안 / 레드", "Cyan / Rot", "Cyan / Rouge", "Sian / Merah", "Ciano / Vermelho", "ฟ้าอมเขียว / แดง"],
    ["Positive adds cyan by reducing red; negative adds red.", "正值通过减少红色增加青色；负值增加红色。", "正值透過減少紅色增加青色；負值增加紅色。", "正の値は赤を減らしてシアンを加え、負の値は赤を加えます。", "양수는 빨강을 줄여 시안을 더하고, 음수는 빨강을 더합니다.", "Positiv fügt durch weniger Rot Cyan hinzu, negativ fügt Rot hinzu.", "Positif ajoute du cyan en réduisant le rouge ; négatif ajoute du rouge.", "Positif menambah sian dengan mengurangi merah; negatif menambah merah.", "Positivo adiciona ciano reduzindo o vermelho; negativo adiciona vermelho.", "ค่าบวกเพิ่มสีฟ้าอมเขียวโดยลดแดง ค่าลบเพิ่มแดง"])
ctl("screenMagenta",
    ["Magenta / Green", "品红 / 绿", "洋紅 / 綠", "マゼンタ / グリーン", "마젠타 / 그린", "Magenta / Grün", "Magenta / Vert", "Magenta / Hijau", "Magenta / Verde", "ม่วงแดง / เขียว"],
    ["Positive adds magenta by reducing green; negative adds green.", "正值通过减少绿色增加品红；负值增加绿色。", "正值透過減少綠色增加洋紅；負值增加綠色。", "正の値は緑を減らしてマゼンタを加え、負の値は緑を加えます。", "양수는 초록을 줄여 마젠타를 더하고, 음수는 초록을 더합니다.", "Positiv fügt durch weniger Grün Magenta hinzu, negativ fügt Grün hinzu.", "Positif ajoute du magenta en réduisant le vert ; négatif ajoute du vert.", "Positif menambah magenta dengan mengurangi hijau; negatif menambah hijau.", "Positivo adiciona magenta reduzindo o verde; negativo adiciona verde.", "ค่าบวกเพิ่มม่วงแดงโดยลดเขียว ค่าลบเพิ่มเขียว"])
ctl("screenYellow",
    ["Yellow / Blue", "黄 / 蓝", "黃 / 藍", "イエロー / ブルー", "옐로 / 블루", "Gelb / Blau", "Jaune / Bleu", "Kuning / Biru", "Amarelo / Azul", "เหลือง / น้ำเงิน"],
    ["Positive adds yellow by reducing blue; negative adds blue.", "正值通过减少蓝色增加黄色；负值增加蓝色。", "正值透過減少藍色增加黃色；負值增加藍色。", "正の値は青を減らしてイエローを加え、負の値は青を加えます。", "양수는 파랑을 줄여 노랑을 더하고, 음수는 파랑을 더합니다.", "Positiv fügt durch weniger Blau Gelb hinzu, negativ fügt Blau hinzu.", "Positif ajoute du jaune en réduisant le bleu ; négatif ajoute du bleu.", "Positif menambah kuning dengan mengurangi biru; negatif menambah biru.", "Positivo adiciona amarelo reduzindo o azul; negativo adiciona azul.", "ค่าบวกเพิ่มเหลืองโดยลดน้ำเงิน ค่าลบเพิ่มน้ำเงิน"])
ctl("labScanDodging",
    ["Dodging", "局部遮挡", "局部遮擋", "覆い焼き", "닷징", "Abwedeln", "Masquage", "Dodging", "Máscara (dodging)", "ดอดจิง"],
    ["Hold broad highlights and open deep shadows on a frame wider than a print.", "在宽容度超过照片的画面上压住大面积高光并打开深暗部。", "在寬容度超過照片的畫面上壓住大面積高光並打開深暗部。", "プリントより広いダイナミックレンジのコマで、広いハイライトを抑え深いシャドウを持ち上げます。", "인화보다 넓은 장면에서 넓은 하이라이트를 누르고 깊은 그림자를 엽니다.", "Hält große Lichter zurück und öffnet tiefe Schatten bei Bildern, die weiter als ein Abzug reichen.", "Retient les grandes hautes lumières et ouvre les ombres profondes sur une image plus étendue qu’un tirage.", "Tahan sorotan luas dan buka bayangan gelap pada bingkai yang lebih lebar dari cetakan.", "Segura altas luzes amplas e abre sombras profundas em quadros mais amplos que uma ampliação.", "คุมไฮไลต์กว้างและเปิดเงาลึกในเฟรมที่มีช่วงกว้างกว่าภาพพิมพ์"])
ctl("labScanLook",
    ["Scanner Look", "扫描仪风格", "掃描器風格", "スキャナー ルック", "스캐너 룩", "Scanner-Look", "Rendu scanner", "Tampilan Pemindai", "Visual do scanner", "ลุคของเครื่องสแกน"],
    ["From a neutral scan to the minilab scanner's finished colour and tone.", "从中性扫描过渡到冲印店扫描仪的成品色彩与影调。", "從中性掃描過渡到沖印店掃描器的成品色彩與影調。", "ニュートラルなスキャンから、ミニラボ スキャナーの仕上がりの色と階調まで。", "중립 스캔부터 미니랩 스캐너의 완성된 색과 톤까지.", "Vom neutralen Scan bis zu Farbe und Ton des fertigen Minilab-Scans.", "D’un scan neutre jusqu’aux couleurs et tons finis d’un scanner de minilab.", "Dari pindaian netral hingga warna dan nada akhir pemindai minilab.", "De um scan neutro até a cor e o tom finais do scanner de minilab.", "จากการสแกนแบบเป็นกลางไปจนถึงสีและโทนสำเร็จของเครื่องสแกนมินิแล็บ"])
ctl("enlarger",
    ["Enlarger", "放大机", "放大機", "引伸機", "확대기", "Vergrößerer", "Agrandisseur", "Pembesar", "Ampliador", "เครื่องขยายภาพ"],
    ["Choose the enlarger lighting used to make the print.", "选择印放时放大机的照明方式。", "選擇印放時放大機的照明方式。", "プリントに使う引伸機の照明方式を選びます。", "인화에 사용할 확대기 조명을 선택합니다.", "Wählt die Beleuchtung des Vergrößerers für den Abzug.", "Choisit l’éclairage de l’agrandisseur utilisé pour le tirage.", "Pilih pencahayaan pembesar yang digunakan untuk mencetak.", "Escolhe a iluminação do ampliador usada na ampliação.", "เลือกระบบแสงของเครื่องขยายภาพที่ใช้อัดภาพ"])
ctl("printerPreflash",
    ["Printer Preflash", "相纸预曝光", "相紙預曝光", "印画紙プリフラッシュ", "인화지 프리플래시", "Papier-Vorbelichtung", "Pré-exposition du papier", "Pra-kilat Kertas", "Pré-exposição do papel", "การฉายแสงล่วงหน้าบนกระดาษ"],
    ["Pre-expose the print paper to soften highlight contrast.", "预先曝光相纸以柔化高光反差。", "預先曝光相紙以柔化高光反差。", "印画紙を事前露光し、ハイライトのコントラストを和らげます。", "인화지를 미리 노출해 하이라이트 대비를 부드럽게 합니다.", "Belichtet das Papier vor, um den Lichterkontrast abzumildern.", "Pré-expose le papier pour adoucir le contraste des hautes lumières.", "Pra-ekspos kertas cetak untuk melembutkan kontras sorotan.", "Pré-expõe o papel para suavizar o contraste das altas luzes.", "ฉายแสงล่วงหน้าบนกระดาษเพื่อลดคอนทราสต์ไฮไลต์"])

# Fixed menu choices
def ch(field, cid, *t):
    add(f"fotufilm_choice_{field}_{re.sub('[^A-Za-z0-9]', '_', cid)}", *t)

ch("grainMottle", "film", "Film’s Own", "胶片原有", "底片原有", "フィルム本来", "필름 고유", "Wie der Film", "Propre au film", "Bawaan Film", "Do próprio filme", "ตามฟิล์ม")
ch("grainMottle", "none", "None", "无", "無", "なし", "없음", "Keine", "Aucune", "Tidak ada", "Nenhuma", "ไม่มี")
ch("grainMottle", "light", "Light · 20%", "轻 · 20%", "輕 · 20%", "弱 · 20%", "약하게 · 20%", "Leicht · 20 %", "Léger · 20 %", "Ringan · 20%", "Leve · 20%", "เบา · 20%")
ch("grainMottle", "moderate", "Moderate · 45%", "中 · 45%", "中 · 45%", "中 · 45%", "보통 · 45%", "Mittel · 45 %", "Modéré · 45 %", "Sedang · 45%", "Moderado · 45%", "ปานกลาง · 45%")
ch("grainMottle", "heavy", "Heavy · 70%", "重 · 70%", "重 · 70%", "強 · 70%", "강하게 · 70%", "Stark · 70 %", "Fort · 70 %", "Berat · 70%", "Forte · 70%", "หนัก · 70%")
ch("grainMottle", "maximum", "Maximum · 90%", "最大 · 90%", "最大 · 90%", "最大 · 90%", "최대 · 90%", "Maximum · 90 %", "Maximum · 90 %", "Maksimum · 90%", "Máximo · 90%", "สูงสุด · 90%")
ch("grainModel", "clump", "Standard", "标准", "標準", "標準", "표준", "Standard", "Standard", "Standar", "Padrão", "มาตรฐาน")
ch("grainModel", "film", "Film", "胶片", "底片", "フィルム", "필름", "Film", "Film", "Film", "Filme", "ฟิล์ม")
ch("shutter", "off", "Instantaneous", "瞬时", "瞬時", "瞬間", "순간 노출", "Momentbelichtung", "Instantané", "Seketika", "Instantâneo", "ทันที")
ch("paper", "lab-scan", "Lab Scan", "冲印店扫描", "沖印店掃描", "ラボスキャン", "랩 스캔", "Laborscan", "Scan de laboratoire", "Pindai Lab", "Scan de laboratório", "สแกนจากแล็บ")
ch("paper", "telecine", "Telecine", "胶转磁", "膠轉磁", "テレシネ", "텔레시네", "Telecine", "Télécinéma", "Telecine", "Telecine", "เทเลซีน")
ch("paper", "screen", "Digital Reference", "数字参考", "數位參考", "デジタル リファレンス", "디지털 레퍼런스", "Digitale Referenz", "Référence numérique", "Referensi Digital", "Referência digital", "อ้างอิงดิจิทัล")
ch("paper", "negative", "Negative", "负片", "負片", "ネガ", "네거티브", "Negativ", "Négatif", "Negatif", "Negativo", "เนกาทีฟ")
# Viewing lights: upstream reuses the ids (reference, d65) across media for different lamps, so
# these are keyed by the upstream label, which names the lamp.
ch("printLight", "Display White · D65", "Display White · D65", "显示白 · D65", "顯示白 · D65", "ディスプレイ白 · D65", "디스플레이 화이트 · D65", "Bildschirmweiß · D65", "Blanc d’affichage · D65", "Putih Layar · D65", "Branco da tela · D65", "ขาวหน้าจอ · D65")
ch("printLight", "Reference Projector · Xenon 5400 K", "Reference Projector · Xenon 5400 K", "参考放映机 · 氙灯 5400 K", "參考放映機 · 氙燈 5400 K", "基準プロジェクター · キセノン 5400 K", "기준 영사기 · 제논 5400 K", "Referenzprojektor · Xenon 5400 K", "Projecteur de référence · Xénon 5400 K", "Proyektor Referensi · Xenon 5400 K", "Projetor de referência · Xenônio 5400 K", "โปรเจกเตอร์อ้างอิง · ซีนอน 5400 K")
ch("printLight", "Light Table · D65", "Light Table · D65", "看片台 · D65", "看片台 · D65", "ライトテーブル · D65", "라이트 테이블 · D65", "Leuchttisch · D65", "Table lumineuse · D65", "Meja Cahaya · D65", "Mesa de luz · D65", "โต๊ะส่องไฟ · D65")
ch("printLight", "Proofing Booth · D50", "Proofing Booth · D50", "看样台 · D50", "看樣台 · D50", "評価用ブース · D50", "검판 부스 · D50", "Normlichtkabine · D50", "Cabine d’épreuve · D50", "Bilik Proofing · D50", "Cabine de prova · D50", "ตู้ตรวจสี · D50")
ch("printLight", "Reference Booth · D50", "Reference Booth · D50", "标准看样台 · D50", "標準看樣台 · D50", "標準観察ブース · D50", "기준 관찰 부스 · D50", "Referenz-Lichtkabine · D50", "Cabine de référence · D50", "Bilik Referensi · D50", "Cabine de referência · D50", "ตู้ดูภาพมาตรฐาน · D50")
ch("printLight", "Daylight · D65", "Daylight · D65", "日光 · D65", "日光 · D65", "昼光 · D65", "주광 · D65", "Tageslicht · D65", "Lumière du jour · D65", "Cahaya Siang · D65", "Luz do dia · D65", "แสงกลางวัน · D65")
ch("printLight", "Tungsten · 2856 K", "Tungsten · 2856 K", "钨丝灯 · 2856 K", "鎢絲燈 · 2856 K", "タングステン · 2856 K", "텅스텐 · 2856 K", "Glühlampe · 2856 K", "Tungstène · 2856 K", "Tungsten · 2856 K", "Tungstênio · 2856 K", "ทังสเตน · 2856 K")
ch("digitalReference", "reference-exposure", "Reference Exposure", "参考曝光", "參考曝光", "基準露光", "기준 노출", "Referenzbelichtung", "Exposition de référence", "Eksposur Referensi", "Exposição de referência", "การรับแสงอ้างอิง")
ch("digitalReference", "graded-print", "Graded Print", "分级印放", "分級印放", "階調プリント", "계조 인화", "Gradierter Abzug", "Tirage gradué", "Cetak Bergradasi", "Ampliação graduada", "ภาพพิมพ์ตามเกรด")
ch("digitalReference", "auto-levels", "Auto Levels", "自动色阶", "自動色階", "自動レベル", "자동 레벨", "Auto-Tonwert", "Niveaux automatiques", "Level Otomatis", "Níveis automáticos", "ระดับอัตโนมัติ")
ch("enlarger", "diffuser", "Diffuser", "漫射式", "漫射式", "拡散光源", "확산식", "Diffusor", "Diffuseur", "Difuser", "Difusor", "แบบกระจายแสง")
ch("enlarger", "condenser", "Condenser", "聚光式", "聚光式", "集光式", "집광식", "Kondensor", "Condenseur", "Kondensor", "Condensador", "แบบคอนเดนเซอร์")
ch("gauge", "super8", "Super 8", "超 8 毫米", "超 8 毫米", "スーパー 8", "슈퍼 8", "Super 8", "Super 8", "Super 8", "Super 8", "ซูเปอร์ 8")
ch("gauge", "16mm", "16mm", "16 毫米", "16 毫米", "16mm", "16mm", "16 mm", "16 mm", "16mm", "16 mm", "16 มม.")
ch("gauge", "super35", "35mm motion (Super 35)", "35 毫米电影（Super 35）", "35 毫米電影（Super 35）", "35mm 映画（スーパー 35）", "35mm 영화 (슈퍼 35)", "35 mm Kino (Super 35)", "35 mm cinéma (Super 35)", "35mm film gerak (Super 35)", "35 mm cinema (Super 35)", "35 มม. ภาพยนตร์ (Super 35)")
ch("gauge", "35mm", "35mm still", "35 毫米静态", "35 毫米靜態", "35mm スチル", "35mm 스틸", "35 mm Kleinbild", "35 mm photo", "35mm foto", "35 mm fotografia", "35 มม. ภาพนิ่ง")
ch("gauge", "120", "120 medium format (6x6)", "120 中画幅（6×6）", "120 中片幅（6×6）", "120 中判（6×6）", "120 중형 (6×6)", "120 Mittelformat (6×6)", "120 moyen format (6×6)", "120 format medium (6×6)", "120 médio formato (6×6)", "120 มีเดียมฟอร์แมต (6×6)")
ch("gauge", "4x5", "4x5 large format", "4×5 大画幅", "4×5 大片幅", "4×5 大判", "4×5 대형", "4×5 Großformat", "4×5 grand format", "4×5 format besar", "4×5 grande formato", "4×5 ลาร์จฟอร์แมต")
ch("gauge", "instaxmini", "instax mini", "instax mini", "instax mini", "instax mini", "instax mini", "instax mini", "instax mini", "instax mini", "instax mini", "instax mini")
ch("gauge", "instaxsquare", "instax SQUARE", "instax SQUARE", "instax SQUARE", "instax SQUARE", "instax SQUARE", "instax SQUARE", "instax SQUARE", "instax SQUARE", "instax SQUARE", "instax SQUARE")
ch("gauge", "instaxwide", "instax wide", "instax wide", "instax wide", "instax wide", "instax wide", "instax wide", "instax wide", "instax wide", "instax wide", "instax wide")
ch("bleach", "0", "Off", "关闭", "關閉", "オフ", "끔", "Aus", "Désactivé", "Mati", "Desligado", "ปิด")
ch("bleach", "0_5", "Half", "一半", "一半", "半分", "절반", "Halb", "Moitié", "Setengah", "Metade", "ครึ่งหนึ่ง")
ch("bleach", "1", "Full", "完全", "完全", "完全", "완전", "Voll", "Totale", "Penuh", "Total", "เต็ม")

# Values
add("fotufilm_value_off", "Off", "关闭", "關閉", "オフ", "끔", "Aus", "Désactivé", "Mati", "Desligado", "ปิด")
add("fotufilm_value_seconds", "%1$s s", "%1$s 秒", "%1$s 秒", "%1$s 秒", "%1$s초", "%1$s s", "%1$s s", "%1$s dtk", "%1$s s", "%1$s วินาที")
add("fotufilm_value_minutes", "%1$s min", "%1$s 分钟", "%1$s 分鐘", "%1$s 分", "%1$s분", "%1$s min", "%1$s min", "%1$s mnt", "%1$s min", "%1$s นาที")
add("fotufilm_value_years", "%1$s yr", "%1$s 年", "%1$s 年", "%1$s 年", "%1$s년", "%1$s J.", "%1$s ans", "%1$s thn", "%1$s anos", "%1$s ปี")


def esc(text):
    text = text.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")
    text = text.replace("'", "\\'").replace('"', '\\"')
    return text


def write_locale(folder, index):
    path = os.path.join(ROOT, "res", folder, "strings.xml")
    content = open(path, encoding="utf-8").read()
    content = re.sub(r'\n\s*<!-- Fotufilm controls -->.*?<!-- /Fotufilm controls -->', '', content, flags=re.S)
    lines = ["    <!-- Fotufilm controls -->"]
    for key, values in S.items():
        attrs = ' formatted="false"' if "%1$s" not in values[index] and "%" in values[index] else ""
        lines.append(f'    <string name="{key}"{attrs}>{esc(values[index])}</string>')
    lines.append("    <!-- /Fotufilm controls -->")
    end = content.rindex("</resources>")
    content = content[:end].rstrip() + "\n" + "\n".join(lines) + "\n" + content[end:]
    open(path, "w", encoding="utf-8").write(content)


for i, folder in enumerate(LOCALES):
    write_locale(folder, i)
for alias, source in ALIASES.items():
    write_locale(alias, LOCALES.index(source))

# Kotlin mapping
fields = [k[len("fotufilm_control_"):] for k in S if k.startswith("fotufilm_control_") and not k.endswith("_description")]
sections = [k[len("fotufilm_section_"):] for k in S if k.startswith("fotufilm_section_")]
choices = [k for k in S if k.startswith("fotufilm_choice_")]
kt = []
kt.append("package com.hinnka.mycamera.ui.components\n")
kt.append("import android.content.Context")
kt.append("import androidx.annotation.StringRes")
kt.append("import com.hinnka.mycamera.R\n")
kt.append("// Generated with the Fotufilm string table; keep in step with res/values*/strings.xml.")
kt.append("internal object FotufilmStringRes {")
kt.append("    @StringRes fun section(section: String): Int? = when (section) {")
for s in sections: kt.append(f'        "{s}" -> R.string.fotufilm_section_{s}')
kt.append("        else -> null\n    }\n")
kt.append("    @StringRes fun title(field: String): Int? = when (field) {")
for f in fields: kt.append(f'        "{f}" -> R.string.fotufilm_control_{f}')
kt.append("        else -> null\n    }\n")
kt.append("    @StringRes fun description(field: String): Int? = when (field) {")
for f in fields: kt.append(f'        "{f}" -> R.string.fotufilm_control_{f}_description')
kt.append("        else -> null\n    }\n")
kt.append("    /** [key] is `<field>_<choice id>` with non-alphanumerics replaced by underscores. */")
kt.append("    @StringRes fun choice(key: String): Int? = when (key) {")
for c in choices: kt.append(f'        "{c[len("fotufilm_choice_"):]}" -> R.string.{c}')
kt.append("        else -> null\n    }")
kt.append("}\n")
open(os.path.join(ROOT, "java", "com", "hinnka", "mycamera", "ui", "components", "FotufilmStringRes.kt"), "w", encoding="utf-8").write("\n".join(kt))
print(len(S), "strings x", len(LOCALES) + len(ALIASES), "locales")
