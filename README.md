# TEK — 8 kanallı performans sequencer'ı (Faz 1 + Faz 2: Chord, Stochastic, Matriceal, Domino, Saga, MIDI Player)

TEK, OXI ONE MKII kılavuzunu temel alan bir VST3/AU sequencer plugin'i. Ses üretmiyor, MIDI üretiyor. Ableton'daki synth'leri, drum rack'leri ve harici cihazları sürüyor.

## Faz 2: MIDI Player modu (yeni)

Setup'ta knob 4 ile **PLYR** seç. Ardından **Shift + PERF**'e bas ve bilgisayarından bir **.mid** dosyası seç. Sequencer dosyadaki notaları döngü halinde, Ableton'a senkron çalar.

- Yalnızca nota on/off bilgisi alınır. CC, pitch bend ve sysex yok sayılır. En fazla 2000 nota, 96 PPQN çözünürlük.
- Dosya projeye gömülür. Ableton setini kaydettiğinde dosya da onunla birlikte kaydedilir; orijinal .mid dosyası gerekmez.
- Grid'de mini bir piyano rulo görünür, beyaz sütun çalma konumudur. Ekranda dosya adı, nota sayısı, uzunluk ve ilerleme yüzdesi görünür.
- Notalar sequencer'ın MIDI kanalından çıkar. Transpoze (Trnsp), mute ve nudge çalışır.
- Her pattern ayrı bir MIDI dosyası taşıyabilir. LOAD görünümünden pattern değiştirerek dosyalar arasında geçiş yaparsın.

## Faz 2: Saga modu (yeni)

Setup'ta knob 4 ile **SAGA** seç. Saga'da adımlar sabit bir grid'e bağlı değil: her pad bir **stage** ve her stage'in kendi süresi (Durat) var.

- **Üst 6 sıra (96 stage):** Soldan sağa, yukarıdan aşağıya sayılır.
  - Dizinin dışında kalan bir pad'e dokunmak dizinin uzunluğunu oraya kadar uzatır. Dizi koyu kırmızı görünür.
  - Dizideki bir stage'e dokunmak onu açar (son seçilen nota ile) veya kapatır.
  - INIT/END + pad de başlangıç ve bitişi ayarlar.
- **Alt 2 sıra klavye:** Domino ile aynı. Stage'i basılı tutup (veya sağ tıklayıp) tuşlara basarak nota veya akor girersin.
- **Stage basılıyken knob'lar:** PAGE ile 3 sayfa arasında geçilir.
  - Sayfa 1: Vel / Note / Gate / **Durat**. Durat, stage'in kaç adım süreceği (1–32); yüksek Durat'lı stage'ler daha parlak görünür.
  - Sayfa 2: Offst / Rng / Trig / Rept.
  - Sayfa 3: repeat süresi / Rpt% / Oct.
- **Zamanlama:** Playhead her stage'de Durat kadar bekler, sonra sonraki stage'e geçer. Toplam süre ekranda T.Durat olarak görünür. Nota uzunluğu "Gate × Durat" ile hesaplanır.
- **Tie:** Stage'i basılı tut, sonraki bir stage'e dokun. Nota bağlanan stage'lerin süreleri boyunca uzar.
- **Mono/Poly ve ChMx:** Setup sayfa 3'te.

## Faz 2: Domino modu

Setup'ta knob 4 ile **DOMI** seç. Grid üç bölüme ayrılır:

- **Üst 2 sıra, nota bölümü (32 adım):** Boş adıma dokununca son seçilen nota girilir. Dolu adıma dokununca silinir. Adımı basılı tutup (veya sağ tıklayıp) klavyeden bir tuşa basınca o nota girilir. Poly modunda en fazla 4 notalık akor olur: tuşa tekrar basmak notayı çıkarır.
- **Orta 4 sıra, trigger bölümü (64 adım):** Dokununca trigger açılır/kapanır. Basılı tutarken knob 1 = repeat sayısı, knob 2 = süre, knob 3 = Rpt%, knob 4 = Trig koşulu.
- **Alt 2 sıra, klavye:** Ölçekteki notalar; alt sıra temel oktav, üst sıra bir üst oktav. Üst sıranın 13. ve 14. pad'leri klavyeyi oktav oktav kaydırır (sarı/mavi). Sağdaki gri pad'ler girilecek notanın velocity'sini seçer.

Nasıl çalışır: her trigger, ve her trigger repeat'i, nota bölümünü bir adım ilerletir. Melodiyi nota bölümü, ritmi trigger bölümü belirler. İki bölümün uzunluğu INIT/END + pad ile ayrı ayrı ayarlanır.

- **Tie:** Bir adımı basılı tut, aynı bölümde daha sonraki bir adıma dokun.
- **Mono/Poly ve ChMx:** Setup sayfa 3'te. Mono modda akorun yalnızca ilk notası çalar.
- **Track menüsü:** trigger bölümünün division'ını ve yönünü ayarlar.
- **Not:** Kılavuzda nota adımları için anlatılan accumulator, Accumulator özelliğiyle birlikte ekleneceği için bu sürümde yok.

## Faz 2: Matriceal modu

Setup'ta knob 4 ile **MATR** seç. Grid 4 dikey bölüme ayrılır, her bölüm bir iz (4 sütun genişliğinde):

- **Üst 4 sıra:** izin seçili şeridinin 16 step'i (soldan sağa, yukarıdan aşağıya 1–16).
- **Orta 3 sıra:** 10 şerit pad'i. Sıralama şöyle: Trigger (beyaz), Note (sarı), Interval (pembe), Pulses (turuncu) / Velocity (turkuaz), Octave (mavi), Gate (mor), Trig Prob (eflatun) / Retrig (yeşil), Glide (açık mavi). Şeridi seçmek için pad'ine dokun.
- **En alt sıra:** izi seçer. MUTE + bu sıra izi susturur.

Temel kural: nota yalnızca **Trigger** şeridindeki aktif step'lerde çalar. O anda diğer şeritler hangi değerdeyse nota o değerleri alır. Şerit uzunlukları farklı olunca desen sürekli değişir.

- **Trigger step'i:** dokununca açılır/kapanır. Basılı tutarken knob 1/2 = Euclid Len/Puls, knob 3'e dokunmak = temizle, knob 4 = rastgelelik oranı (dokununca uygular).
- **Diğer şeritlerin step'leri:** basılı tut (veya sağ tıkla) ve knob 1 ile değeri, knob 2 ile olasılığı ayarla. Pulses şeridinde knob 2 tipi seçer: First, All veya Tie. Ayarlı bir step'e kısa dokunmak onu sıfırlar.
- **Şerit ayarları:** şerit pad'ini basılı tut. Knob 3 = division, knob 4 = yön. Interval şeridinde knob 1/2 = Min/Max.
- **Şerit uzunluğu:** INIT veya END + step pad'i.
- **İz ayarları:** iz pad'ini basılı tut veya TRACK'e bas. Ch, Ofs (temel nota) ve RLen (bu kadar adımda bir tüm şeritleri başa sarar) burada. Basılı tutarken knob 4'e dokunmak izi temizler.
- **Rastgele şerit:** RANDOM'ı basılı tut ve bir şerit pad'ine dokun.
- **Pulses:** step'i N adım tutar. Bu sürede bütün şeritler o step'te bekler.
- **Interval:** her tetiklemede notayı ölçek aralığı kadar kaydırır. Min/Max'a ulaşınca başa döner.
- Glide yalnızca CV için anlamlı olduğundan MIDI'de etkisi yok, değer olarak saklanıyor.
- Kılavuzdaki 16×8 tam ekran şerit editörü (parametre pad'ine çift dokunma) sonraki güncellemeye kaldı.

## Faz 2: Stochastic modu

Setup'ta knob 4 ile **STCH** seç. Bu modda nota girmiyorsun: grid bir kontrol paneline dönüşüyor ve sequencer her döngüde yeni bir cümle üretiyor.

| Sütun | İşlevi |
|---|---|
| 1–12 | Kökten itibaren her yarım ton bir sütun. Sütunun yüksekliği o notanın çıkma olasılığı. Aynı yüksekliğe tekrar dokunmak notayı kapatır. Ölçek dışı notalar kırmızı görünür. |
| 13 | Oktav aralığı. Üst yarı +1…+4 oktav (yeşil), alt yarı −1…−4 oktav (mavi). |
| 14 | Ritim aralığı. Beyaz = temel division. Aşağısı daha uzun notalar (×2, ×4), yukarısı ratchet'ler (1/2…1/8). |
| 15 | Perde rastgeleliği: her döngüde step'lerin yüzde kaçının perdesi yeniden üretilir. En alt pad = **kilit** (kırmızı). |
| 16 | Ritim rastgeleliği, aynı mantıkla. En alt pad = kilit. |

- **Kilitleme:** 15 ve 16'yı en alta çekersen o an çalan cümle donar. Kilitli cümle Ableton projesiyle birlikte kaydedilir.
- **Velocity/gate profili:** Knob 1 veya 3'e dokunup sütun editöründe çiz. Knob'u çevirmek profili toptan değiştirir. Knob 2 cümleyi oktav oktav kaydırır.
- **Kopyalama:** COPY + bu sequencer'ın tuşuna bas, sonra PASTE + Mono veya Poly bir sequencer'ın tuşuna bas. Üretilen cümle düzenlenebilir step'lere dönüşür.
- **Yeni cümle:** Shift + PASTE cümleyi silip yenisini üretir. Shift + 16/64 cümleyi sola/sağa kaydırır.
- **Uzunluk:** INIT/END + pad ile değişir. Knob 4'teki MOD şeridi Faz 4'te (modülasyon) gelecek.

## Faz 2: Chord modu

- **Modu açma:** Shift + sequencer tuşu ile Setup'ı aç, knob 4 ile **CHRD** seç, knob 4'e bas.
- **Grid:** her satır, ölçeğin o derecesindeki diatonik akoru çalar (örneğin C majörde I = C major, ii = D minor…). Step başına bir akor girilir; aynı sütunda başka satıra dokunmak akoru değiştirir. Akorun diğer notaları sütunda soluk renkte görünür.
- **Pad basılıyken:** PAGE ile 4 sayfa arasında geçilir.
  - Sayfa 1: Vel / Oct / Gate / Trig
  - Sayfa 2: Rept / Durat / Ramp / Accel
  - Sayfa 3: Offst / **Strm** (strum, ±%100) / Rng / Rpt%
  - Sayfa 4: **Sprd** (üst oktav notaları, 0–6) / **Type** / **Voicing**
- **Akor tipleri:** Triad, 7th, 9th, 11th, 13th, 6th, 6/9, add9, sus2, sus4, 7sus4, Power. Akorun majör, minör veya dim olması ölçekten gelir.
- **Voicing:** No Inv, I Invr, II Invr, Open, I Open, II Open. Bir akor en fazla 8 nota olabilir.
- **Chord Global:** Shift + STEP ile açılır, bütün step'leri birlikte değiştirir: Sprd, Type, Voic ve **Bass**. Bass seçenekleri: OFF, ON (akorun bir oktav altına kök nota ekler) veya Ch1–16 (bas notasını ayrı bir kanala gönderir, örneğin ayrı bir bas synth'ine).
- **ChMx:** Setup sayfa 3'te açılır. Akorun her notasını sırayla bir sonraki MIDI kanalına dağıtır (mono synth'lerden akor kurmak için).
- **Mod dönüşümü:** Chord → Poly geçişinde akorlar tek tek düzenlenebilir notalara dönüşür. Poly → Chord geçişinde her step'in en alt notası akorun kökü olur.
- Chord palette/keyboard görünümü ve chord arp Faz 3'te (keyboard ve arpeggiator ile birlikte) gelecek.

## Faz 1'de olanlar

- **8 bağımsız sequencer.** Her biri Mono, Poly (adım başına 7 nota) veya Multitrack (8 iz) modunda çalışıyor.
- **Pattern'ler:** sequencer başına 16 pattern, pattern başına en fazla 128 step (8 sayfa).
- **Step parametreleri:** velocity, octave/note, gate (2–99, LEG), tie, step mute.
- **Trig koşulları:** %0–100, A:B, !A:B, PRE, !PRE, 1st, !1st, FIXED, LOCK, FLOW, !FLOW.
- **Step alt menüsü:** Repeat, Duration, Ramp, Accel / Offset, Retrig, Rng, Rpt%.
- **Pattern ayarları:** başlangıç/bitiş (Init/End), time division (BAR…1/64, triplet'ler dahil), yön (FWD, REV, PEND, PONG, RND, DRUNK), rastgele division.
- **Zamanlama:** swing (pattern başına, multitrack'te iz başına), groove (14 şablon), nudge (±100 ms) ve time bend (±%12).
- **Ölçek ve kök:** 31 ölçek, kök nota. MIDI girişinden transpoze (Trnsp).
- **Düzenleme:** kopyala/yapıştır (step, sayfa, sequencer, pattern), duplicate, clear, döndürme/transpoze, expand/condense, 15 adımlık undo/redo.
- **Mute:** sequencer mute, multitrack iz mute/solo, mute latch.
- **Görünümler:** sütun editörleri (velocity, gate, retrig/repeat), Load görünümü (pattern değiştirme bar sonunda olur), multitrack'te piyano rulo görünümü.
- **Saat:** Ableton'ın tempo ve çalma konumuna kilitli. İstersen dahili saat de var.
- **Kayıt:** her şey Ableton projesinin içine kaydediliyor.

Sonraki fazlarda gelecekler: Chord, Stochastic, Matriceal, Domino, Saga ve MIDI Player modları, accumulator, harmonizer, keyboard, arpeggiator, Flow, Roller ve Perform, generator'lar, kayıt, modülasyon/LFO, arranger. Bu fazda bunların tuşlarına basınca ekranda "COMING IN A LATER PHASE" yazıyor.

## Mac'e kurulum

1. GitHub'daki `TEK-mac` paketini indirip aç.
2. Dosyaları kopyala:
   - `TEK.vst3` → `~/Library/Audio/Plug-Ins/VST3/`
   - `TEK.component` → `~/Library/Audio/Plug-Ins/Components/`
3. Plugin imzasız olduğu için macOS ilk açılışta onu engelleyebilir. Terminal'de şu komutu bir kez çalıştır:
   ```
   xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/TEK.vst3 ~/Library/Audio/Plug-Ins/Components/TEK.component
   ```
4. Ableton > Preferences > Plug-Ins bölümünde **Use VST3 Plug-in System Folders** açık olsun, sonra **Rescan**.

## Ableton'da bağlantı

1. Bir MIDI track'e **TEK**'i instrument olarak yükle. Instruments listesinde görünür.
2. **Önerilen yol, 8 sequencer'ı ayrı synth'lere göndermek:** TEK açılınca Mac'te **"TEK Out"** adlı sanal bir MIDI portu oluşturuyor.
   - Ableton > Preferences > Link/Tempo/MIDI'de "TEK Out" girişinin **Track** kutusunu aç. Bu bir kerelik ayar.
   - Her synth track'inde *MIDI From* = **TEK Out**, kanal = **Ch. 1** (Seq 1), **Ch. 2** (Seq 2)… seç. Monitor = **In**.
   - Varsayılan olarak sequencer N, MIDI kanalı N'den çıkıyor. Bunu Shift + sequencer tuşu ile açılan Setup menüsünden değiştirebilirsin.
3. **Tek synth için daha basit yol:** synth track'inde *MIDI From* = TEK'in olduğu track, alttaki seçici = **TEK**. Bu yolda bütün kanallar aynı synth'e gider, çünkü Ableton plugin çıkışını kanallara ayıramıyor.
4. Çıkış yolu Setup sayfa 1 > **Out** ayarından seçiliyor: HOST, PORT veya BOTH (varsayılan BOTH).
5. Sanal port üzerinden giden notalar ses kartı tamponu kadar gecikmeli gelebilir. Fark edersen Shift + MUTE (nudge) menüsünden ilgili sequencer'a eksi ms ver.

## Kontroller

Fare donanımdaki parmak gibi çalışıyor. "Basılı tutma" gereken işler için üç yol var:

| İşlem | Fare / klavye |
|---|---|
| Step aç/kapat | Pad'e tıkla |
| Step düzenle (pad basılıyken knob) | Pad'e uzun bas ve klavyeden Q/A, W/S, E/D, R/F'ye bas; **ya da** pad'e sağ tıkla (basılı kalır), knob'u çevir, sonra yine sağ tıkla |
| Tie | Pad'den aynı satırda sağa sürükle (ilk pad + son pad) |
| Fill (ters tie) | Sağdan sola sürükle |
| Knob çevir | Knob üstünde yukarı/aşağı sürükle veya fare tekerleği (Alt = ince ayar) |
| Knob'a bas | Knob'a tıkla |
| Tuşu basılı tut | Tuşa sağ tıkla (kilitlenir, tekrar sağ tıklayınca bırakılır) veya klavye kısayolu |

Klavye kısayolları: **Shift** = SHIFT, **1–8** = sequencer, **M** = MUTE, **C/V** = COPY/PASTE, **I/O** = INIT/END, **T** = TRACK, **P** = PAGE, **K** = STEP, **L** = LOAD, **X** = FLOW, **Z** = UNDO, **B** = BACK.

Sık kullanılanlar (kılavuzla aynı):

- **Seq view knob'ları:** VEL / OCT / GATE / REPT. Bunlar göreli global düzenleme yapar. Knob'a dokunmak sütun editörünü açar. OCT knob'una dokunmak scroll modunu açar; multitrack'te piyano ruloyu açar.
- **Shift + knob'lar:** BPM (dahili saat), SWING, SCALE, ROOT. Shift + knob'a dokunmak sıfırlar.
- **Pad basılıyken knob'lar:** Vel / Oct (multitrack'te Note) / Gate / Trig. PAGE ile alt menü sayfaları 2/3 ve 3/3'e geçilir.
- **Shift + 1–8:** Setup menüsü (Ch, Out, PC, Mode / In, Reset, Bnk, Trnsp / nVoice veya T.Swg, Color). Mod değişikliği knob 4'e basınca onaylanır.
- **TRACK:** track menüsü (yön, division, rastgele division; multitrack'te kanal, temel nota, swing).
- **STEP:** global step menüsü (repeat ayarlarını izin tüm step'lerine uygular).
- **INIT / END + pad:** pattern başlangıcı ve bitişi. Shift + INIT / END: division ×2 / ÷2.
- **16 / 32 / 48 / 64:** sayfa seçimi. Çift dokunuş 5–8. sayfalar. Shift + bunlar: ← ↑ ↓ → kaydırma.
- **Shift + PAGE / Shift + LFO:** expand / condense.
- **MUTE + 1–8:** sequencer mute. MUTE + pad: step mute. Multitrack'te MUTE + 1. sütun iz mute, MUTE + 16. sütun solo. MUTE'a çift dokunuş: latch.
- **Shift + MUTE:** nudge menüsü. Bir kez daha basınca (veya PAGE) time bend.
- **Shift + FLOW:** groove menüsü. FLOW basılıyken FLOW step'leri çalar.
- **LOAD:** satırlar sequencer, sütunlar pattern. COPY/PASTE + pad pattern kopyalar. Shift + pad pattern siler.
- **Shift + STOP:** saat kaynağı HOST ↔ INT. STOP'a çift dokunuş: tüm notaları kes.

## Donanımdan bilinçli farklar

- CV/Gate, Bluetooth, SD kart, pil, firmware ve kalibrasyon yok, bunlar plugin'de anlamsız.
- Groove şablonları özgün, donanımdaki şablonların kopyası değil.
- Step alt menüsünün 3. sayfasında CV Glide yerine Retrig var. CV glide MIDI'de olmadığı için.
- Pattern'ler projeyle canlı kaydediliyor, ayrı bir "Save" adımı yok.

## Kaynak koddan derleme

```
cmake -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
cmake --build build --config Release --target TEK_VST3 TEK_AU
ctest --test-dir build      # motor + kontrolcü testleri
```
Kod `core/` (framework'süz motor ve donanım mantığı) ile `plugin/` (JUCE kabuğu ve arayüz) olarak ikiye ayrılıyor. JUCE derlemede otomatik indiriliyor. JUCE'un lisansı AGPLv3; kişisel kullanım için sorun yok.
