# TEK — 8 kanallı performans sequencer'ı (Faz 1 + Faz 2: Chord, Stochastic)

TEK, OXI ONE MKII kılavuzunu temel alan bir VST3/AU sequencer plugin'i. Ses üretmiyor, MIDI üretiyor. Ableton'daki synth'leri, drum rack'leri ve harici cihazları sürüyor.

## Faz 2: Stochastic modu (yeni)

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
