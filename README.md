# TEK — 8 kanallı performans sequencer'ı (Faz 1–3 tamam: modlar, performans araçları, generator'lar, kayıt)

TEK, OXI ONE MKII kılavuzunu temel alan bir VST3/AU sequencer plugin'i. Ses üretmiyor, MIDI üretiyor. Ableton'daki synth'leri, drum rack'leri ve harici cihazları sürüyor.

## Faz 3c: Generator'lar, kayıt, pattern ve proje yönetimi (yeni)

**Euclid generator (GEN):** Mono, Poly, Chord ve Multitrack modlarında çalışır. Multitrack'te seçili track'e uygulanır.
- Knob çevirdiğin anda desen yeniden üretilir.
- SHIFT basılıyken çevirmek yalnızca değeri ayarlar, deseni değiştirmez.
- Bir knoba dokunmak aynı ayarlarla yeni bir varyasyon üretir.
- Menü açıldıktan sonra yapılan değişikliklerin tamamı tek UNDO ile geri alınır.
- PAGE ile 4 sayfa arasında geçilir:
  1. eLen, ePuls, eRot, Dens (katman 1 ve yoğunluk)
  2. NteR, Nte%, OctR, eInv (nota aralığı, olasılık, oktav, ters çevirme)
  3. Tie%, eLen2, eRot2, NtOrd (bağlama, katman 2, nota sırası)
  4. Lock, ePul2, Logic (OR / AND / XOR / Accnt), Vel%
- Fixed veya Locked koşullu adımlara dokunulmaz.
- **Lock:** "Pitch" seçiliyse ritim değişir ama notalar korunur. "Rhythm" seçiliyse adım yerleri korunur, yalnızca notalar değişir.

**Random generator (SHIFT+RANDOM):** RANDOM'a dokunmak bu menü ile Random Perform arasında geçiş yapar.
- Sayfa 1: Bias, Rang, Rand, Den. Sayfa 2: Scal, V&G, Ties.
- **Knob 4 (Den)'e dokununca desen üretilir.**
- Den sağa çevrildiğinde yüzde değeri alır ve yeni desen üretir.
- Den sola çevrildiğinde mevcut deseni değiştiren seçeneklere geçer:
  - **Human:** zamanlamaya ve gate'e insan eli hissi verir.
  - **Order:** notaların sırasını karıştırır.
  - **Pitch:** sırayı korur, notaları yeniler.
  - **Flip:** deseni tersten çalar.
- Diğer knoblara dokunmak o değeri varsayılana döndürür.

**Kayıt (REC).** Klavye görünümündeki pad'lerle ya da harici MIDI klavyeyle kayıt yapılır:
- **REC'e dokun:**
  - Transport çalıyorsa kayıt hemen başlar (punch-in).
  - Duruyorsa kayıt hazır bekler (REC yavaş yanıp söner) ve Ableton Play'e basınca başlar. Count-in ayarlıysa önce sayım yapılır.
  - Kayıt sürerken tekrar dokunmak kaydı bitirir.
- **REC basılıyken STOP:** Kayıt ilk çaldığın notayla başlar.
- **REC basılıyken PLAY:** Adım adım kayıt açılır veya kapanır.
  - Her nota (ya da akor) bir adıma yazılır ve imleç ilerler.
  - Knob 1'e dokunmak başa döner, knob 2 imleci kaydırır, knob 3'e dokunmak boş adım (rest) ekler, knob 4'e dokunmak bağ (tie) ekler.
- **MIDI capture:** Çaldığın son notalar arka planda hep tutulur; tamponda nota varsa REC hafif kırmızı yanar.
  - **REC'e çift dokununca** bu notalar sırayla sequencer'a yazılır. Aynı anda çalınan notalar tek adımda akor olur.
  - REC'i basılı tutmak tamponu temizler.
- **Kayıt ayarları (SHIFT+REC):**
  - Sayfa 1: Qt (kuantize açık/kapalı), Qt% (kuantize oranı), Ovdb/NoOv, Mode.
    - Mode: Loop sürekli kaydeder, 1Shot bir tur kaydeder, Xtend kayıt sürdükçe deseni uzatır.
  - Sayfa 2: Cnt in (count-in süresi), Metro (metronomun MIDI kanalı), Md (metronom her zaman mı, yalnızca count-in'de mi).

**Pattern'ler:**
- **SAVE** görünümünde bir pad'e çift dokununca o sequencer'ın güncel pattern'i o slota kopyalanır.
- **SHIFT+LOAD** güncel pattern'i Ableton setinin en son kaydedildiği hale döndürür.
- **LOAD** görünümünde:
  - SHIFT+pad o sütundaki pattern'i 8 sequencer'da birden yükler.
  - SHIFT+PASTE+pad slotu siler. (Eskiden yalnızca SHIFT+pad siliyordu.)
- Setup sayfa 2'deki **In** ayarı pattern geçişinin ne zaman olacağını belirler: OFF (hemen), P.End (pattern sonu), Beat, 1/2B, Bar, 2/4/8 Bar.
- **PC / Bnk** ayarlandığında program change, ayar değiştiğinde ve transport her başladığında gönderilir.

**Proje menüsü (SHIFT+PERF).** Knob 1 seçeneği seçer, knob 2 slotu veya değeri seçer, knob 4'e dokunmak seçeneği uygular. Geri alınamayan işlemler ikinci bir dokunuşla onay ister. Seçenekler:
- **Reload project:** Ableton setinin son kaydedildiği hale döner.
- **Load project file / Save project file:** Projeyi .tek dosyası olarak yükler veya kaydeder. Varsayılan klasör Belgeler/TEK/Projects.
- **Clear project:** Bütün sequencer'ları siler.
- **Save as template / Load template:** 32 slot vardır, dosyalar Belgeler/TEK/Templates klasöründe durur. Template, sequencer'ın ayarlarını ve güncel pattern'ini saklar.
- **Load MIDI file:** PLYR modu için MIDI dosyası yükler.
- **Scale master:** Seçilen sequencer'ın ölçeği ve kök notası diğer bütün sequencer'lara uygulanır. Harmonizer'daki ve Multitrack moddaki sequencer'lar hariç.
- **Reload pattern:** Güncel pattern'i son kaydedilen hale döndürür.

## Faz 3b: Flow, Roller, Random Perform, Perform, Snapshot

- **Flow step:** FLOW'u basılı tutup bir pad'e dokun. Adım yoksa "Flow" koşullu bir adım oluşur, varsa Flow koşulu açılır/kapanır. Bu adımlar yalnızca FLOW basılıyken çalar.
- **Roller** (Keyboard görünümü, 13. sütun): Bir roller pad'ini basılı tutarken basılı notalar sürekli tekrar çalar. Hız en üstte 1/32, en altta 1/4 noktalı. Önce nota sonra roller, ya da önce roller sonra nota; ikisi de olur. Transport dururken de çalışır.
- **Random Perform** (RANDOM: dokun = menüyü aç/kapa, basılı tut = geçici). Pattern değişmez, yalnızca çalış sırasında rastgelelik eklenir.
  - Sayfa 1: Vel, Oct, Gate, Rept. Sayfa 2: Note, Trig, Retri. Sayfalar arasında PAGE ile geçilir.
  - "+-" değer iki yöne de sapar. Sola çevirip "-" yaparsan yalnızca aşağı yöne sapar.
  - Trig, adımın çalma olasılığıdır. Retri "+" ise rastgele retrig ekler, "skip" ise rastgele adım atlatır.
  - Knoba dokunmak o değeri sıfırlar.
  - Multitrack'te RANDOM basılıyken 1. sütundaki bir pad o track'i dışarıda bırakır.
  - Değerlerden biri açıkken RANDOM tuşu turuncu yanar.
- **Perform görünümü** (PERF: dokun = aç/kapa, basılı tut = geçici). Her satır bir sequencer'dır, SEQ1 en üsttedir.
  - **Tek track'li modlar:**
    - 1. sütun mute, SHIFT ile solo.
    - 2–16. sütunlar transpoze eder: −7…+7 ölçek derecesi, ortadaki gri pad 0'dır.
    - SHIFT basılıyken aynı sütunlar adım kaydırma (rotate) yapar.
  - **Multitrack:**
    - 1–8. sütunlar track mute, SHIFT ile solo.
    - 9–12. sütunlar fill: basılıyken her adım 2–5 kez tekrar çalar.
    - 13–16. sütunlar canlı loop: çalan yerden itibaren 4/3/2/1 adım.
    - SHIFT ile basılırsa kilitlenir; tekrar SHIFT ile basınca bırakılır.
  - **Matriceal:** 1–4. sütunlar track mute, 5–16. sütunlar rastgele trigger fill (sağa doğru yoğunluk artar).
- **Snapshot:**
  - LOAD basılıyken SAVE'e basınca o anki 8 sequencer'ın kopyası alınır. Ekranda "SNAP" yazar, LOAD ve UNDO yeşil yanıp söner.
  - Ardından istediğin gibi değiştir. LOAD veya UNDO'ya basınca snapshot'a geri döner.
  - SHIFT+LOAD (veya SHIFT+UNDO) snapshot'ı silip değişiklikleri tutar.

## Faz 3a: Keyboard, Loop, Arpeggiator

**Keyboard görünümü** (KEYBOARD tuşu: dokun = aç/kapa, basılı tut = geçici): Grid bir klavyeye dönüşür.
- **Düzen:** Satırlar oktav (en alt = temel oktav), 1–12. sütunlar C'den B'ye notalar. Notalar anında çalar; Ableton kayıttayken MIDI olarak kaydedilir.
- **Renkler:** Ölçek dışı notalar kırmızı, kök nota parlak. Kromatik ölçekte beyaz tuşlar renkli, siyahlar sönük.
- **Multitrack'te drum pad'leri:** Satır = iz, sütun = velocity.
- **Knob'lar:** Vel / Bend (pitch bend) / At (aftertouch) / Base (temel oktav C-2…C4). Knob'a dokunmak sıfırlar.
- **Akor tanıma:** Basılı notaların akor adı ekranda görünür (Cmaj, Am7, G/B…).
- **Harici klavye:** Ableton'da TEK'in track'ine gelen MIDI notaları seçili sequencer'ın klavyesine gider, arp açıksa arpeggiator'ı besler. Trnsp atanmış kanallar hariç.
- **Shift + KEYBOARD = Preview:** Grid'e nota girerken o notayı kısa bir süre duyarsın.

**Loop** (Mono, Poly, Chord, Multitrack): Pattern'in içinde ayrı bir döngü kurar.
- **Kurma:** INIT'i basılı tut ve END'e bas, loop görünümü açılır. Başlangıç pad'ini basılı tutup bitiş pad'ine dokun. Döngü mor görünür, INIT/END yanıp söner.
- **Knob'lar:** Len (×2 / ÷2) / Pos (kaydır, Shift = loop uzunluğu kadar) / Rate (loop'a ayrı division) / Track (multitrack).
- **Temizleme ve çıkış:** INIT veya END'e dokunmak loop'u temizler. BACK görünümden çıkar.

**Arpeggiator** (ARP tuşu):
- **Keyboard arp** (Mono, Multitrack, Stochastic, Matriceal ve Keyboard görünümü): ARP'a basınca arp menüsü ve klavye açılır. Type'ı seçip klavyede notaları basılı tut, notalar arpejlenir. Ableton durmuşken arp kendi saatiyle Ableton'ın temposunda çalar; Play'e basınca bar çizgisine kilitlenir.
- **Sequencer arp** (Poly ve Chord, grid görünümündeyken): Her step'in notaları/akoru o step boyunca arpejlenir.
- **Menü (PAGE ile 3 sayfa):**
  - Sayfa 1: **Type** (Up, Down, Up/Dn, 2Up2Dn, AltIn, AltOut, AltIO, Pinky, Thumb, Random, RndOnce, Order) / **OctV** (±7 oktav) / **Gate** / **Div**.
  - Sayfa 2: **Accnt** (groove velocity profili) / **Len** / **Puls** / **Rept** (Euclid: Len adımda Puls kadar nota, her darbeden sonra Rept kadar tekrar).
  - Sayfa 3: **GTim** (groove zamanlaması) / **Grov** (groove şablonu) / **Reset** (yeni nota gelince pozisyonu ve/veya groove'u başa al) / **Latch**.
- **Shift + ARP = Hold:** Bıraktığın notalar çalmaya devam eder, yeni akor basınca değişir. Latch açıkken notalar üst üste eklenir; latch'li bir notaya tekrar dokunmak onu çıkarır.
- **Kısayol:** Menünün 1. sayfasında knob 1'e dokunmak arp'ı son seçilen tiple açıp kapatır.

## iPad dokunmatik controller

TEK, açıldığı bilgisayarda küçük bir web sunucusu başlatır. iPad, iPhone veya başka bir tablet aynı Wi-Fi ağındaysa TEK'i dokunmatik olarak, çoklu dokunmayla kontrol edebilir.

1. TEK penceresinin alt satırında bir adres yazar, örneğin **iPad: http://192.168.1.20:8765**.
2. iPad'de **Safari**'yi açıp bu adrese git.
3. Paylaş düğmesi → **Ana Ekrana Ekle** dersen TEK, tam ekran açılan bir uygulama simgesi olur.
4. İlk seferde macOS "Ableton Live gelen ağ bağlantılarını kabul etsin mi?" diye sorabilir. **İzin Ver** de.

Kullanım:
- Gerçek cihazdaki gibi: bir parmakla pad'i basılı tut, diğer parmakla knob'u çevir (yukarı/aşağı kaydır).
- SHIFT, MUTE, COPY, INIT gibi tuşları basılı tutup başka bir yere dokunabilirsin.
- Bir pad'den aynı satırda başka bir pad'e kaydırmak tie yapar.
- Ekran, LED'ler ve grid canlı güncellenir. Bilgisayardaki pencereyle aynı anda kullanılabilir.
- Birden fazla TEK açıksa her biri sırayla 8766, 8767… portlarını kullanır. Adres her pencerede ayrı yazar.
- Bağlantı koparsa sayfa kendiliğinden yeniden bağlanır. Sol üstteki durum yazısı "connected" olmalı.

## Faz 2: Accumulator, Harmonizer, ChMx

**Accumulator** (Mono, Poly, Domino, Saga): Step her çaldığında notası ayarlanan aralık kadar kayar.
- **Açma:** Step'i basılı tutup PAGE ile accumulator sayfasına git. Mono/Poly'de sayfa 4–5, Domino'da 3–4, Saga'da 4–5.
  - İlk sayfa: **Amt** (her seferde kaç ölçek aralığı, ±24) / **Mode** / **Acml+** / **Acml-**.
  - İkinci sayfa: **Trig** (All = step + repeat'ler, Step, Rept) / **%Mde** (olasılık tutmazsa: Ignor = orijinal nota, Skip = son değerde kal) / **Acm%** (ilerleme olasılığı).
- **Modlar:**
  - Wrap: sınırı aşınca öbür uçtan devam eder.
  - Clip: sınıra gelince öbür uçtan baştan başlar.
  - Pend: sınırda yön değiştirir.
  - Hold: sınırda sabit kalır.
- **Global (STEP → 3. sayfa):** **AcmM** = Step (sınırlar her step'te ayrı) veya Trck (aşağıdaki Acml+/- tüm step'ler için geçerli).

**Harmonizer:** Bir sequencer'ın notalarını, başka bir sequencer'ın (lider: Chord veya Poly) o an çaldığı akora uydurur.
- **Takipçiyi ayarlama:** Takipçi sequencer'da Shift + knob 3'ü (SCAL) en baştan da geriye çevir, ölçek **HARMON** olur. Shift + knob 4 (ROOT) ile lideri seç (Sq1–Sq8).
- **Kullanım:** Takipçide grid satırları liderin akor notalarına denk gelir. En alt satır akorun ilk notası, sonrakiler diğer notalar, daha yukarısı bir üst oktav. Lider akor değiştirdikçe takipçinin melodisi kendiliğinden uyum sağlar.
- **Desteklenen takipçiler:** Mono, Poly, Multitrack, Matriceal, Domino, Saga. Lider henüz çalmadıysa C major varsayılır.

**ChMx (Channel Multiplex):** Setup sayfa 3'te seçilir (Poly, Chord, Domino, Saga).
- **2 Lo…8 Lo:** En alt nota temel kanalda kalır, üstündekiler sırayla sonraki kanallara dağılır.
- **2 Hi…8 Hi:** Aynı mantık ama en üst nota temel kanalda kalır.
- Sayı, kaç kanala dağıtılacağıdır. Notalar o sayıya ulaşınca dağıtım temel kanala döner.

## Faz 2: MIDI Player modu

Setup'ta knob 4 ile **PLYR** seç. Ardından **SHIFT + PERF**'e bas. Proje menüsü "Load MIDI file" seçeneğiyle açılır; knob 4'e dokunup bilgisayarından bir **.mid** dosyası seç. Sequencer dosyadaki notaları döngü halinde, Ableton'a senkron çalar.

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
5. **Öneri:** TEK'in kendi track'inde *MIDI From* ayarını **All Ins** yerine **No Input** ya da kullandığın MIDI klavye yap. TEK, TEK Out'tan geri gelen kendi notalarını artık filtreliyor, ama bu ayar en temiz çözüm.
6. Sanal port üzerinden giden notalar ses kartı tamponu kadar gecikmeli gelebilir. Fark edersen Shift + MUTE (nudge) menüsünden ilgili sequencer'a eksi ms ver.

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
