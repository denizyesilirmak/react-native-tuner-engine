# DSP Pipeline — Sesin Mikrofondan JavaScript'e Yolculuğu

Bu belge, `react-native-tuner-engine`'in ses işleme mimarisini baştan sona açıklar. Mikrofona giren ham ses dalgasının nasıl alındığını, hangi aşamalardan geçtiğini, hangi algoritmaların ne iş yaptığını ve sonunda JavaScript tarafına nasıl ulaştığını anlatır.

---

## 1. Genel Akış

```
Mikrofon
   │
   ▼
Platform Audio API
(AVAudioEngine / Oboe)
   │  ham PCM float32 örnekler
   ▼
SPSC Ring Buffer          ← audio callback buraya yazar, allocate etmez
   │
   ▼
Worker Thread             ← burada tüm DSP işleri yapılır
   │
   ├─► RMS Gate           → ses yeterince güçlü mü?
   ├─► BiquadHpf          → alçak frekansları kes
   ├─► EnsembleSelector
   │     ├─► YinPitchDetector
   │     ├─► PyinPitchDetector
   │     └─► CepstrumPitchDetector
   ├─► SnrEstimator       → güven skorunu ortam gürültüsüne göre ayarla
   ├─► PostProcessor      → median filtre + EMA + histerezis
   └─► NoteMapper         → Hz → nota adı + oktav + cent sapması
         │
         ▼
   PitchResult
         │
         ▼
jsInvoker->invokeAsync    ← JS thread'ine geç
         │
         ▼
onPitch event             ← React'te useTuner hook'una ulaşır
```

---

## 2. Sesin Yakalanması

### Platform katmanı

**iOS — `IosAudioSource`**

`AVAudioEngine` kullanılır. `AVAudioSession` kategorisi `.playAndRecord`, modu `.measurement` olarak ayarlanır. Measurement modu önemlidir: iOS'un dahili ekolayzerını ve ses işleme zincirini devre dışı bırakır, böylece ham sinyal gelir. Input node üzerine bir "tap" kurulur:

```objc
[inputNode installTapOnBus:0
                bufferSize:2048
                    format:format
                     block:^(AVAudioPCMBuffer* buf, AVAudioTime* when) {
    // Bu blok audio thread'inde çalışır
    // Tek yapması gereken şey: veriyi ring buffer'a kopyalamak
}];
```

Tap callback'i `float*` verisini alır ve `AudioFrameDispatcher`'ın SPSC kuyruğuna yazar. Burada **hiçbir heap allocation yapılmaz** — bu kuralın ihlali audio glitch'e yol açar.

**Android — `OboeAudioSource`**

Oboe kütüphanesi kullanılır. `PerformanceMode::LowLatency` istenir; cihaz desteklemezse `Shared` moduna düşülür. Android'de audio callback Java'ya dokunmaz, tamamen native tarafta kalır. Oboe callback'i de aynı şekilde ring buffer'a yazıp döner.

### SPSC Ring Buffer ve Worker Thread

`AudioFrameDispatcher` iki ayrı thread'i birbirine bağlar:

- **Audio thread**: Veriye yazar. Hiçbir mutex, hiçbir allocation yok. moodycamel'ın `ReaderWriterQueue` kullanılır — bu kuyruk, producer ve consumer aynı anda çalışsa bile lock-free çalışır. Memory order garantileri sayesinde mutex'e gerek yoktur.

- **Worker thread**: Kuyruktan okur, DSP işlerini yapar. Bu thread platform tarafından yüksek priority'e alınır (iOS'ta `AVAudioSession`'ın real-time thread'i, Android'de elevated priority). Worker thread frame hazır olunca `Pipeline::process`'i çağırır ve sonucu `jsInvoker->invokeAsync` ile JS thread'ine gönderir.

Bu ayrım sayesinde ne audio thread bloklanır ne de JS thread gecikir.

---

## 3. Pipeline — Sıralı İşlem Aşamaları

`Pipeline::process(const float* input, int frameSize)` her frame için şu adımları çalıştırır:

### Adım 1: RMS Gate

```cpp
const float rmsLinear = calculateRmsLinear(input, frameSize_);
const float rmsDb     = 20.0f * std::log10(std::max(rmsLinear, kMinLinear));

if (rmsDb < noiseGateDb_) return silent;
```

**RMS (Root Mean Square)**: Bir frame'deki tüm örneklerin karelerinin ortalamasının karekökü. Sinyalin güç seviyesini ölçer.

```
rms = sqrt( (x₀² + x₁² + ... + xₙ₋₁²) / n )
```

Sonuç decibel'e çevrilir: `dBFS = 20 * log10(rms)`. Eğer bu değer ayarlanan `noiseGateDb` eşiğinin altındaysa (varsayılan -55 dB), frame tamamen sessiz sayılır ve işlenmez. Bu hem CPU tasarrufu sağlar hem de gürültülü ama sessiz ortamlarda yanlış nota tespitini önler.

### Adım 2: Yüksek Geçiren Filtre (HPF)

```cpp
std::copy(input, input + frameSize_, workBuffer_.begin());
hpf_.process(workBuffer_.data(), frameSize_);
```

Filtreleme ham input üzerinde değil, kopyası üzerinde yapılır. Sonraki adımlar bu kopyayı kullanır; orijinal değişmez.

---

## 4. BiquadHpf — Biquad Yüksek Geçiren Filtre

**Dosya**: `cpp/src/BiquadHpf.cpp`

Bu filtre, sesin 70 Hz altındaki bileşenlerini keser. Neden gerekli? Mikrofonlar mekanik titreşim, hava akımı, zemin gürültüsü gibi çok düşük frekanslı sinyaller alır. Bu bileşenler pitch tespitini bozar; hem YIN hem PYIN'in fark fonksiyonu bu düşük frekanslı bileşenlerden etkilenir.

### Direct Form II Transposed

Implementasyon Audio EQ Cookbook'tan alınan formüllerle yapılmıştır:

```cpp
// Konstruktörde katsayılar hesaplanır (bir kez)
const float w0    = 2π * cutoffHz / sampleRate;
const float cosW0 = cos(w0);
const float alpha = sin(w0) / (2 * q);
const float a0    = 1 + alpha;

b0_ =  (1 + cosW0) / 2 / a0;
b1_ = -(1 + cosW0) / a0;
b2_ =  (1 + cosW0) / 2 / a0;
a1_ = -2 * cosW0 / a0;
a2_ =  (1 - alpha) / a0;
```

Her örnek için (frame döngüsünde):

```cpp
const float y = b0_ * x + w1_;
w1_ = b1_ * x - a1_ * y + w2_;
w2_ = b2_ * x - a2_ * y;
frame[i] = y;
```

`w1_` ve `w2_` değerleri **frame'ler arası korunur**. Yani bir frame biterken filtre "nerede kaldığını" hatırlar. Aksi hâlde her frame başında keskin bir geçiş oluşur ve bu da yapay frekanslar üretir.

Q değeri 0.707 (Butterworth) seçilmiştir. Bu değer, kesim frekansında maksimum düzlüğü sağlar — resonance yok, aşırı yumuşama yok.

---

## 5. Pitch Tespiti — Üç Algoritma

Filtrelenmiş frame üç farklı algoritmaya aynı anda verilir. Her biri bağımsız çalışır, sonra `EnsembleSelector` bunların çıktısını değerlendirir.

---

### 5a. YIN Algoritması

**Dosya**: `cpp/src/YinPitchDetector.cpp`

YIN, 2002 yılında de Cheveigné ve Kawahara tarafından yayınlanmış klasik bir pitch tespit algoritmasıdır. Zaman domeninde çalışır, FFT gerekmez.

**Temel fikir**: Eğer bir sinyal `f` Hz frekanslıysa, `T = 1/f` saniye sonra kendini tekrar eder. YIN bu öz-korelasyonu ölçer ama doğrudan korelasyon yerine **fark fonksiyonu** kullanır:

#### Adım 1: Fark Fonksiyonu

```
d(τ) = Σ (x[i] - x[i+τ])²    i = 0..N-τ
```

`τ` (tau) gecikme miktarıdır — kaç örnek ileri baktığımız. Eğer `τ` tam bir periyodsa, `x[i]` ile `x[i+τ]` birbirine çok yakın olur ve fark küçük çıkar. Yani `d(τ)` minimum olan `τ` değeri periyodu verir.

Bu basit yaklaşımın problemi: `d(0) = 0` her zaman, ve düşük `τ` değerleri de küçük çıkma eğilimindedir. Bu yüzden normalize etmek gerekir.

#### Adım 2: Kümülatif Ortalama Normalize Fark (CMND)

```
cmnd[0] = 1
cmnd[τ] = d(τ) * τ / (Σ d(j), j=1..τ)
```

Bu formül, küçük `τ` değerlerindeki sahte minimumlara ceza uygular. `τ`'ya bölerek uzun periyodları büyük tutmak yerine, kayan ortalamaya bölerek normalize eder. Sonuç olarak gerçek periyottaki minimum çok daha belirgin hâle gelir.

#### Adım 3: Eşik ve Minimum Arama

```cpp
for (int tau = tauMin; tau <= tauMax; ++tau) {
    if (cmnd_[tau] < threshold_) {         // varsayılan eşik: 0.2
        while (tau + 1 <= tauMax && cmnd_[tau + 1] < cmnd_[tau]) {
            tau++;
        }
        tauEstimate = tau;
        break;
    }
}
```

İlk eşiği geçen minimum bulunur, sonra yerel minimuma kayılır (bir sonraki adım daha düşük olduğu sürece ileri gidilir).

#### Adım 4: Parabolik İnterpolasyon

Integer `tau` değeri sample-rate/tau olan frekansı verir ama bu çözünürlük yetersizdir. Örneğin 48000 Hz sample rate'te tau=109 → 440.4 Hz, tau=110 → 436.4 Hz. Aralarında 4 Hz boşluk var.

Parabolik interpolasyon bu boşluğu kapatır:

```cpp
float YinPitchDetector::parabolicInterpolation(int tau) const {
    const float left   = cmnd_[tau - 1];
    const float center = cmnd_[tau];
    const float right  = cmnd_[tau + 1];
    const float denom  = left - 2.0f * center + right;
    // parabola'nın tepesi
    return tau + 0.5f * (left - right) / denom;
}
```

Üç noktadan geçen parabola hesaplanır, bu parabolanın tepe noktası kesirli bir `tau` değeri verir. `sampleRate / betterTau` ile frekans bulunur.

**YIN'in güçlü yönü**: Hızlı ve güvenilir. Gitar, keman, şan gibi harmonik bakımından zengin seslerde çok iyi çalışır.

**YIN'in zayıf yönü**: Sadece ilk CMND minimumunu alır. CMND formülünün matematiksel özelliği gereği gerçek periyodun katları (2τ₀, 3τ₀) da minimum oluşturur. Eğer sinyal karmaşıksa ya da çok harmonikliyse, YIN bazen yanlış minimumda takılır ve oktav hatası yapar.

---

### 5b. Probabilistik YIN (PYIN)

**Dosya**: `cpp/src/PyinPitchDetector.cpp`

PYIN, YIN'in 2014'te Mauch ve Dixon tarafından geliştirilmiş versiyonudur. YIN'in matematiksel altyapısını kullanır ama "ilk minimum" yerine "tüm minimumlar" yaklaşımını benimser.

#### YIN'den Farkı

YIN ilk eşiği geçen minimumda durur. PYIN ise tüm CMND minimumlarını toplar ve bunların her birine bir olasılık değeri atar.

```cpp
candidates_.clear();

// Tüm yerel minimumlara bak
for (int tau = tauMin + 1; tau < tauMax; ++tau) {
    if (cmnd_[tau] < cmnd_[tau - 1] && cmnd_[tau] < cmnd_[tau + 1]) {
        if (cmnd_[tau] < threshold_) {
            candidates_.push_back({tau, 1.0f - cmnd_[tau] / threshold_});
        }
    }
}
```

Olasılık formülü: `prob = 1 - cmnd[τ] / threshold`. CMND değeri ne kadar düşükse, bu tau değeri o kadar güçlü bir aday demektir ve olasılığı yüksektir.

#### Harmonik Alias Budama — Kritik Düzeltme

Burada önemli bir matematiksel problem ortaya çıkar. CMND formülünün yapısı gereği, eğer gerçek periyot `τ₀` ise, `2τ₀`, `3τ₀`, `4τ₀`... değerleri de CMND'de minimum oluşturur. Dahası, bu harmonik katların CMND değerleri `τ₀`'ınkinden **numerik olarak daha küçük** çıkar. Çünkü payda (kümülatif toplam) büyüdükçe normalize değer küçülür.

Bu budanmazsa PYIN her zaman `en uzun periyodu` — yani `en düşük frekansı` — en yüksek olasılıklı aday olarak seçer. 440 Hz için 220 Hz, 110 Hz dönebilir.

```cpp
// Adaylar artan tau sırasında (kısa periyot önce)
for (int i = 0; i < candidates_.size(); ++i) {
    if (candidates_[i].prob == 0.0f) continue;
    for (int j = i + 1; j < candidates_.size(); ++j) {
        const float ratio   = float(candidates_[j].tau) / float(candidates_[i].tau);
        const float nearest = round(ratio);
        // tau_j, tau_i'nin tam katıysa (%2 toleransla) → harmonik alias, sil
        if (nearest >= 2.0f && fabs(ratio - nearest) / nearest < 0.02f) {
            candidates_[j].prob = 0.0f;
        }
    }
}
```

Bu loop şunu yapar: `τᵢ`'nin bilinen bir minimum olduğunu varsay. Eğer başka bir aday `τⱼ`, `τᵢ`'nin 2×, 3×, 4× ... katıysa, bu aday harmonik bir alias'tır — gerçek pitch değil. Olasılığı sıfırlanır.

Kazanan seçimi:

```cpp
const Candidate* winner = nullptr;
for (const auto& c : candidates_) {
    if (!winner || c.prob > winner->prob) winner = &c;
}
```

Eşit prob durumunda `>` (strict greater than) ilk bulunanı — yani en kısa periyodu, yani en yüksek frekansı — tutar. Bu kasıtlı bir seçimdir: sub-oktav hatası üst-oktav hatasından daha kötüdür.

**PYIN'in güçlü yönü**: Oktav hatalarına karşı çok daha dayanıklıdır. Harmonik içeriği zayıf veya karmaşık seslerde YIN'in başarısız olduğu yerde PYIN doğru pitch'i bulur.

**PYIN'in zayıf yönü**: Hesaplamak için tüm frame'i tarar (aynen YIN gibi O(N²)), ve ek olarak candidate management yapar. Ancak pratikte aday sayısı azdır (genelde 2-5 arası).

---

### 5c. Cepstrum Pitch Dedektörü

**Dosya**: `cpp/src/CepstrumPitchDetector.cpp`

Cepstrum tamamen farklı bir domende çalışır. YIN ve PYIN zaman domenindeyken cepstrum frekans domenine geçer, oradan başka bir transformasyon yapar.

#### Cepstrum Nedir?

Bir sinyalin "güç spektrumunun logaritmasının ters Fourier dönüşümü"dür. Adı da "spectrum" kelimesinin anagramından gelir.

Formülde:

```
Cepstrum(τ) = IFFT( log( |FFT(x)|² ) )
```

Neden bu işe yarar? Harmonik bir sesin spektrumu periyodik yapıdadır — temel frekans ve katları. Bu periyodik yapı logaritmik skala üzerinde de periyodik kalır. Periyodik bir sinyalin Fourier dönüşümü o sinyalin frekansında bir tepe noktası verir. Yani cepstrum'un x ekseninde (buna "quefrency" denir) bir tepe görürsek, bu tepe sinyalin temel periyodunu gösterir.

#### Implementasyon Adımları

**Adım 1: Hann Penceresi**

```cpp
for (int i = 0; i < len; ++i) {
    fftBuf_[i] = {frame[i] * hann_[i], 0.0f};
}
```

Hann penceresi spektral sızıntıyı (spectral leakage) azaltır. FFT sonsuz bir sinyalin sonlu bir parçasına bakıyor; pencereleme olmadan frame başı ve sonu arasındaki süreksizlik gürültü yaratır.

**Adım 2: FFT**

```cpp
tuner::fft(fftBuf_);
```

`Fft.hpp`'deki inline Cooley-Tukey implementasyonu kullanılır. Bu dependency'siz saf C++ kodudur, harici kütüphane gerektirmez.

**Adım 3: Log Güç Spektrumu**

```cpp
for (int k = 0; k <= frameSize_ / 2; ++k) {
    logPow_[k] = std::log(re*re + im*im + kEps);
}
// Simetrik (two-sided) kopyalama
for (int k = 1; k < frameSize_ / 2; ++k) {
    logPow_[frameSize_ - k] = logPow_[k];
}
```

Spektrumun negatif frekans tarafı pozitif tarafın aynasıdır. Two-sided yapılmazsa IFFT yanlış sonuç verir.

`kEps = 1e-10f` sıfır logaritması almayı önler.

**Adım 4: IFFT → Cepstrum**

```cpp
for (int k = 0; k < frameSize_; ++k) {
    fftBuf_[k] = {logPow_[k], 0.0f};
}
tuner::ifft(fftBuf_);
```

Sonuç dizisinin gerçek (real) kısmı cepstrum'dur.

**Adım 5: Quefrency Peak Arama**

```cpp
const int tauMin = int(sr / maxHz_);
const int tauMax = int(sr / minHz_);

float maxVal = -1e30f;
int   bestTau = -1;
for (int q = tauMin; q <= tauMax; ++q) {
    const float v = fftBuf_[q].real();
    if (v > maxVal) { maxVal = v; bestTau = q; }
}
```

Quefrency domain, zaman domeninin "periyot" eksenine karşılık gelir. `q = sr / f` formülüyle hangi quefrency değerinin hangi frekansa karşılık geldiği bulunur.

**Adım 6: Güven Skoru (SNR Tabanlı)**

```cpp
const float snr = (peak - mean) / rms;
return max(0.0f, min(1.0f, snr / 5.0f));
```

Burada önemli bir tasarım kararı var: güven skoru peak-prominence'a değil, SNR'a dayalı. Neden? Saf sinüs dalgalarında (440 Hz'lik pur bir ton) cepstrum zirvesi çok belirgin görünür ama bu noktada harmonik yapı yoktur çünkü sinüs tek bileşenlidir. Peak-prominence kullansaydık saf sinüste `1.0` güven skoru çıkardı — ama cepstrum saf sinüsü doğru tespit edemez. SNR tabanlı formül, cepstrum'un gerçekten güçlü olduğu harmonik seslerde yüksek, saf sinüste makul bir skor üretir.

**Cepstrum'un güçlü yönü**: Çok harmonikli seslerde (davul, org, bas gitar) mükemmel çalışır. Hesaplaması da YIN/PYIN'den çok daha hızlıdır (benchmarkta 0.05 ms vs 0.6-0.7 ms).

**Cepstrum'un zayıf yönü**: Harmonik yapısı az olan seslerde (flüt, ıslık) güven skoru düşük çıkar. Bu yüzden ensemble'da tek başına karar vermez.

---

## 6. Radix-2 FFT

**Dosya**: `cpp/include/Fft.hpp`

`CepstrumPitchDetector`'ün kullandığı FFT implementasyonu Cooley-Tukey algoritmasının en temel formudur.

### Bit-Reversal Permutation

```cpp
for (int i = 1, j = 0; i < n; ++i) {
    int bit = n >> 1;
    for (; j & bit; bit >>= 1) j ^= bit;
    j ^= bit;
    if (i < j) std::swap(x[i], x[j]);
}
```

Cooley-Tukey algoritması veriyi ikili ağaç şeklinde böler (yarıya, sonra çeyreğe...). Bu bölme işleminin sonunda her eleman, orijinal indeksinin bit-tersine çevrilmiş konumunda olur. Örneğin N=8'de eleman 3 (011₂), konum 6'ya (110₂) gider. Bu yeniden sıralama önden yapılır.

### Butterfly İşlemi

```cpp
for (int len = 2; len <= n; len <<= 1) {
    const float angle = -2π / len;
    const complex<float> wlen(cos(angle), sin(angle));
    for (int i = 0; i < n; i += len) {
        complex<float> w(1, 0);
        for (int j = 0; j < len/2; ++j) {
            const complex<float> u = x[i+j];
            const complex<float> v = x[i+j+len/2] * w;
            x[i+j]       = u + v;    // butterfly üst kol
            x[i+j+len/2] = u - v;    // butterfly alt kol
            w *= wlen;
        }
    }
}
```

Her iterasyonda çift uzunluktaki bloklara bakılır. `wlen` "twiddle factor" — kompleks düzlemde döndürme faktörü. Her adımda iki eleman bir "butterfly" işlemine tabi tutulur: toplandı ve çıkarıldı. Bu O(N²)'yi O(N log N)'e indiren temel hiledir.

### IFFT

```cpp
for (auto& c : x) c = conj(c);   // konjuge al
fft(x);                            // aynı FFT'yi çalıştır
const float inv = 1.0f / x.size();
for (auto& c : x) c = conj(c) * inv;  // tekrar konjuge, normalize et
```

IFFT'nin konjuge-FFT-konjuge trick'i: forward FFT'yi değiştirmeden kullanır, sadece giriş ve çıkışı konjuge alır. Bu matematiksel olarak doğrudur çünkü `IFFT(X) = conj(FFT(conj(X))) / N`.

---

## 7. EnsembleSelector — Oylama Mekanizması

**Dosya**: `cpp/src/EnsembleSelector.cpp`

Üç dedektörün sonuçları EnsembleSelector'a gelir. Her biri bir `DetectorResult` döndürür:

```cpp
struct DetectorResult {
    bool  voiced;       // pitch tespit edildi mi?
    float frequency;    // Hz
    float confidence;   // 0-1 arası güven skoru
};
```

### Adım 1: Sesli Sonuçları Filtrele

```cpp
Entry voiced[8];  // stack allocation — heap yok
int voicedCount = 0;

for (int i = 0; i < resultsBuf_.size(); ++i) {
    const auto& r = resultsBuf_[i];
    if (r.voiced && r.confidence > 0.0f) {
        voiced[voicedCount++] = {i, r.frequency, r.confidence, 0};
    }
}
```

`voiced[8]` stack'te tutulur. Heap allocation yapmak istemiyoruz çünkü bu worker thread'inde çalışıyor; her ne kadar audio callback'i olmasa da allocation latency'den kaçınmak iyi pratiktir.

### Adım 2: Oy Sayımı

```cpp
bool withinSemitones(float f1, float f2, float tolerance = 1.0f) {
    return fabs(12.0f * log2(f1 / f2)) <= tolerance;
}

for (int i = 0; i < voicedCount; ++i) {
    for (int j = i+1; j < voicedCount; ++j) {
        if (withinSemitones(voiced[i].freq, voiced[j].freq)) {
            ++voiced[i].votes;
            ++voiced[j].votes;
        }
    }
}
```

İki frekansın birbirinden ne kadar uzak olduğu müzikal yarım ton (semitone) cinsinden ölçülür: `|12 * log₂(f1/f2)|`. Bu logaritmik ölçek, frekans oranlarını eşit kulağa eşit mesafe gibi yorumlar. 1 semitone tolerans oldukça dardır (~5.9% frekans farkı). Dedektörler gerçekten aynı notayı görüyorlarsa bu tolerans içinde kalırlar.

### Adım 3: Kazananı Seç ve Ortalama Al

```cpp
// En çok oyu olan, eşitlikte en yüksek güvenli kazanır
const Entry* best = &voiced[0];
for (int i = 1; i < voicedCount; ++i) {
    if (voiced[i].votes > best->votes ||
        (voiced[i].votes == best->votes && voiced[i].conf > best->conf)) {
        best = &voiced[i];
    }
}

// Kazananla aynı fikirde olan dedektörlerin frekansını ortala
float freqSum = best->freq, confSum = best->conf;
int   agreeing = 1;
for (int i = 0; i < voicedCount; ++i) {
    if (&voiced[i] != best && withinSemitones(voiced[i].freq, best->freq)) {
        freqSum += voiced[i].freq;
        confSum += voiced[i].conf;
        ++agreeing;
    }
}
const float avgFreq = freqSum / agreeing;
```

Frekans ortalaması alınır çünkü anlaşan dedektörler birbirinden 1 semitone içinde ama tam olarak aynı değil olabilir. YIN 440.1, PYIN 439.8 bulabilir; ortalaması 439.95 gerçek değere daha yakındır.

### Adım 4: Güven Düzeltmesi

```cpp
avgConf = (agreeing > 1)
          ? min(1.0f, avgConf * 1.1f)   // anlaşma bonusu
          : avgConf * 0.85f;             // yalnız kalma cezası
```

İki veya daha fazla dedektör anlaşıyorsa `×1.1` bonus. Kimse anlaşmıyorsa `×0.85` ceza. Bu, güven skorunu dedektörlerin mutabakatına göre kalibre eder.

---

## 8. SnrEstimator — Ortam Gürültüsüne Adaptasyon

**Dosya**: `cpp/src/SnrEstimator.cpp`

Dedektörlerin verdiği ham güven skoru ses seviyesinden bağımsızdır. Ama pratikte düşük SNR ortamında (gürültülü oda) aynı güven skoru çok daha az güvenilirdir. `SnrEstimator` bunu düzeltir.

### Gürültü Tabanı Takibi (EMA)

```cpp
if (rmsLinear < noiseFloorLinear_) {
    // Ses gürültü tabanının altına düştü — tabanı hızlıca indir
    noiseFloorLinear_ = kAttackAlpha * rmsLinear + (1-kAttackAlpha) * noiseFloorLinear_;
} else {
    // Ses tabanın üzerinde — tabanı yavaşça yukarı çek (uzun vadeli sessizliği takip et)
    noiseFloorLinear_ = kDecayAlpha * noiseFloorLinear_ + (1-kDecayAlpha) * rmsLinear;
}
```

İki farklı EMA (Exponential Moving Average) hızı kullanılır:
- **Attack (hızlı)**: Ses düşünce gürültü tabanı hızla iner. Mikrofondan uzaklaşınca hemen adapte olur.
- **Decay (yavaş)**: Ses yükselince gürültü tabanı yavaşça çıkar. Bu kasıtlı — bir nota çalınırken gürültü tabanının o nota seviyesine çıkmasını istemeyiz.

SNR sonuç:
```
snrDb = 20 * log10(rmsLinear / noiseFloorLinear)
```

### SNR'dan Ağırlık

```cpp
float snrToWeight(float snrDb) {
    if (snrDb <= 0.0f)  return 0.0f;
    if (snrDb >= 30.0f) return 1.0f;
    return snrDb / 30.0f;
}
```

0 dB SNR → ağırlık 0 (sinyal gürültüyle eşit güçte, güvenilmez).
30 dB SNR → ağırlık 1 (sinyal gürültüden 1000× daha güçlü, tam güven).

Pipeline'da: `weightedConf = det.confidence * snrWeight`. Bu, gürültülü ortamda sistematik olarak düşük güven skoru üretir ve eşik altında kalarak yanlış nota basılmasını önler.

---

## 9. PostProcessor — Kararlı Nota Gösterimi

**Dosya**: `cpp/src/PostProcessor.cpp`

Dedektörler frame frame çalışır ve çıktıları gürültülüdür. Gerçek bir akustik enstrümanda bile aynı nota için 439.7, 440.2, 440.0, 439.9... gibi değerler gelir. Kullanıcıya bu titreşimi göstermek yerine kararlı bir değer göstermek gerekir. PostProcessor üç mekanizma kullanır.

### Mekanizma 1: Median-5 Filtresi

```cpp
// Dairesel buffer'a yaz
medianBuf_[medianIdx_] = frequency;
medianIdx_ = (medianIdx_ + 1) % kMedianLen;  // kMedianLen = 5

// Sırala ve ortayı al
array<float, 5> sorted;
copy(medianBuf_, medianBuf_+5, sorted.begin());
sort(sorted.begin(), sorted.end());
return sorted[2];  // orta eleman
```

Son 5 frame'in ortanca değeri alınır. Ortanca (median) anlık outlier'lara karşı ortalamadan daha dayanıklıdır. Eğer bir frame yanlış 300 Hz gösterip arkasından 440 Hz'e dönerse, ortanca 440 Hz'de kalır; ortalama bu geçici sapmayı taşır.

### Mekanizma 2: EMA (Exponential Moving Average)

```cpp
smoothedFreq_ = cfg_.emaAlpha * med + (1.0f - cfg_.emaAlpha) * smoothedFreq_;
```

Median filtreden geçen değer EMA ile yumuşatılır. EMA üstel ağırlıklı bir ortalamadır — en son değer en çok ağırlık taşır, eskiler giderek azalan ağırlıkla dahil edilir. `emaAlpha` ne kadar büyük olursa yanıt o kadar hızlı ama o kadar gürültülü olur.

EMA yalnızca aynı nota içinde çalışır. Nota değiştiğinde EMA sıfırlanır, yeni nota frekansına "snap" edilir. Aksi hâlde geçiş sırasında eski notadan yeni notaya doğru yavaş bir kayma görünür.

### Mekanizma 3: Histerezis

Histerezis, nota değişiminin çok sık olmasını önler. "A4'teyim, biraz kayarsam A#4'e geçeyim, sonra tekrar A4'e döneyim" döngüsü olmaz.

```cpp
const int newMidi = freqToMidi(med);  // frekansı en yakın MIDI notasına çevir

if (newMidi == lockedMidi_) {
    // Aynı notayı görüyoruz — değişim sayacını sıfırla
    candidateMidi_ = -1;
    candidateCount_ = 0;
} else {
    if (newMidi == candidateMidi_) {
        ++candidateCount_;
    } else {
        candidateMidi_  = newMidi;
        candidateCount_ = 1;
    }
    // Yeterince frame boyunca aynı yeni notayı görürsek değiş
    if (candidateCount_ >= cfg_.hysteresisFrames) {
        lockedMidi_ = candidateMidi_;
        smoothedFreq_ = med;  // EMA'yı snap et
    }
}
```

Varsayılan `hysteresisFrames = 3` — yeni nota 3 ardışık frame boyunca stabil görünürse geçiş yapılır. Bu ~128ms gecikme demektir (2048/48000 ≈ 43ms/frame × 3). Tuner için kabul edilebilir bir gecikme: yeterince hızlı ama kararlı.

---

## 10. NoteMapper — Hz'den Nota İsmine

**Dosya**: `cpp/src/NoteMapper.cpp`

```cpp
const int midi = int(round(69.0f + 12.0f * log2(frequency / a4_)));
```

MIDI nota numarası hesabı: A4 = 69 tanımından hareketle, her oktav 12 MIDI adımına karşılık gelir, her yarım ton log₂ skalasında eşit mesafededir.

```cpp
static const array<const char*, 12> names = {
    "C", "C#", "D", "D#", "E", "F",
    "F#", "G", "G#", "A", "A#", "B"
};
result.noteName = names[midi % 12];
result.octave   = midi / 12 - 1;
```

MIDI modulo 12 nota adını verir. MIDI / 12 - 1 oktavı verir. Neden -1? MIDI oktav sözleşmesi: C4 = MIDI 60. 60/12 = 5, 5-1 = 4. Doğru.

Cent sapması:
```cpp
const float target = a4_ * pow(2.0f, (midi - 69) / 12.0f);
const float cents  = 1200.0f * log2(frequency / target);
```

`target` o MIDI notasının teorik frekansıdır. Gerçek frekansın teorik frekansa oranının logaritması, 1200 ile çarpılınca cent cinsinden sapma verir. 1200 = bir oktavdaki cent sayısı. +50 cent = yarım ton yukarı, -50 cent = yarım ton aşağı.

---

## 11. HannWindow'un Rolü

`Window.hpp`'deki Hann penceresi şu formülle precomputed edilir:

```
w[i] = 0.5 * (1 - cos(2π * i / (N-1)))
```

Bu 0'dan başlayıp 0'da biten, ortada 1'e çıkan bir çan eğrisidir.

Neden önemli? FFT sonsuz uzunluktaki bir sinyal varsayar. Gerçekte sinyali keserek alıyoruz; kesilen uçlar ani bir atlama yaratır ve bu atlama spektrumda her frekansa yayılan gürültüye (spectral leakage) yol açar. Hann penceresiyle çarparak sinyalin uçlarını sıfıra indiririz, atlama ortadan kalkar.

**Önemli not**: Pipeline'da Hann penceresi cepstrum dedektörünün kendi içinde uygulanır. YIN ve PYIN için ayrıca `HannWindow` apply edilmez. Bu kasıtlı bir karardır — YIN zaman domenindeki fark fonksiyonunu kullanır; pencereli veriye bu fonksiyonu uygulamak periyodikliği bozarak yanlış sonuç üretir.

---

## 12. Özetle: Verinin Yolculuğu

```
Mikrofon sinyali
  → float32 PCM örnekler (@48kHz, 2048 sample/frame ≈ 42ms)
  → SPSC kuyruk (audio thread'i yazar, hiç allocate etmez)
  → Worker thread
      → RMS hesapla → eşiğin altındaysa atla
      → Working buffer'a kopyala
      → BiquadHpf: 70 Hz altını kes
      → EnsembleSelector:
          → YIN: CMND min bul → parabolic interp → Hz
          → PYIN: tüm CMND min topla → alias buda → kazananı seç → Hz
          → Cepstrum: FFT → log-güç → IFFT → quefrency peak → Hz
          → Oylama: semitone mutabakat → frekans ortala → güven ayarla
      → SnrEstimator: gürültü tabanı EMA → SNR dB → ağırlık
      → weightedConf = ensemble.confidence × snrWeight
      → eşiğin altındaysa atla
      → PostProcessor:
          → median-5 buffer
          → MIDI histerezis (nota kilitle/değiştir)
          → EMA yumuşatma
      → NoteMapper: Hz → MIDI → isim + oktav + cent
      → PitchResult { hasPitch, frequency, noteName, octave, cents, confidence, rmsDb }
  → jsInvoker->invokeAsync (JS thread'ine geç)
  → DeviceEventEmitter "onPitch" event
  → useTuner hook → setLatest → React re-render
```

Toplam gecikme: audio buffer süresi (42ms @ 2048/48k) + işlem süresi (≈1.25ms) + histerezis (≈128ms ilk nota için) = kullanıcı nota çaldıktan ~170ms sonra ekranda görür. Bu standart tuner uygulamaları için kabul edilebilir bir değerdir.
