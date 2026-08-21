# Proje: Dual-Layer Smooth Cursor Overlay (Windows, öncelik) / (Linux, ikincil)

## Konu

Windows masaüstünde (sistem geneli — tüm uygulamalar, dosya gezgini, oyun menüleri dahil, exclusive fullscreen hariç) çalışan, iki katmanlı bir custom cursor overlay sistemi. Amaç: web sitelerinde görülen "gerçek imleç + arkasından smooth/lag'li ikinci imleç" efektini tüm OS seviyesinde replicate etmek, ayrıca motion blur eklemek.

## Gereksinimler

### Katman 1 — Raw Point Cursor (gerçek input göstergesi)
- Gerçek mouse pozisyonunu 1:1, sıfır ek gecikmeyle gösterir.
- Sabit bir nokta/cross şekli. Context'e göre (link, text, resize vs.) şekil DEĞİŞMEZ.
- Her zaman en önde (diğer katmanın üstünde) görünür.
- Implementasyon: `SPI_SETCURSORS` ile Windows'un tüm sistem cursor scheme'i (arrow, hand, ibeam, resize-all, vs.) tek bir minimal cross/dot bitmap'e override edilir. Bu katman OS'in native hardware cursor rendering'ini kullanır — ayrı bir render loop'a ihtiyaç yok, native zero-latency.

### Katman 2 — Smooth Ghost Cursor (asıl efekt)
- Gerçek pozisyonu physics-based bir gecikmeyle takip eder (spring/damping, basit lerp değil).
- Hareket yönüne göre rotate olabilir (flick hissi — velocity vektörünün açısına göre dönüş).
- Kendi üzerinde motion blur (hız arttıkça blur/stretch artan, yönlü blur).
- Arkasında ayrıca fade-out olan bir trail/iz bırakır (self-blur'dan ayrı, daha uzun soluklu bir katman).
- Gerçek Windows cursor'ının o anki şeklini (arrow/hand/ibeam/resize/custom app cursor'ları dahil) takip eder ve kendi skin'ini ona göre değiştirir — yani bu katman da context-aware, sadece "gecikmeli ve fizikli" gelir.

### Genel
- Sistem geneli çalışmalı (belirli bir tarayıcı/uygulamaya bağlı değil).
- Multi-monitor, mixed-DPI desteklenmeli.
- Öncelik Windows. Olmazsa/zaman kalırsa Linux (X11) için de aynı mimari (Wayland hedef değil — compositor kısıtlamaları nedeniyle güvenilir global cursor tracking mümkün değil).

## Teknik Yaklaşım (özet)

- Full-screen (virtual desktop boyutunda), `WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST` overlay window.
- Input: `WM_INPUT` (Raw Input API), polling değil event-driven.
- Physics: critically-damped spring (`velocity += (target - pos) * k - velocity * damping; pos += velocity * dt`), lag hedefi ~60-100ms bandı.
- Shape sync: `GetCursorInfo()` → `hCursor` → `GetIconInfo` + `GetObject` ile bitmap çıkar; sistem cursor ID'leriyle (`IDC_ARROW`, `IDC_HAND`, `IDC_IBEAM`...) eşleştirip kendi skin'ini seç, custom app cursor'larında gerçek bitmap'i direkt texture olarak kullan.
- Self motion blur: frame arası sub-stepping / accumulation buffer (ara-pozisyonları azalan alpha ile üst üste composite et), blur uzunluğu hıza bağlı.
- Trail: son N smooth-pozisyonun ring buffer'ı, azalan boyut/opacity ile render.
- Render backend: Direct2D veya D3D11 + swap chain, vsync'e kilitli. GDI kullanma (blur/alpha için çok yavaş).
- Stack: C++ + Win32 + Direct2D.

## Riskler / Plan B

- Exclusive fullscreen oyunlarda overlay görünmez (borderless-windowed'de sorun yok) — kabul edilen limitasyon.
- Global hook + system-wide overlay bazı anti-cheat sistemlerini tetikleyebilir → oyun process'leri için otomatik disable eden bir exclude-list olmalı.
- `SPI_SETCURSORS` sistem geneli cursor scheme'i değiştirir; crash/unclean shutdown durumunda kullanıcının orijinal cursor scheme'i geri yüklenmezse güven kırılır → önceki scheme backup + crash-safe restore (ör. registry'den önceki değerleri okuyup crash handler'da da restore) zorunlu.

---

## Claude Yorumu / Önerileri

- **MVP sırası:** Önce Katman 1 (SPI_SETCURSORS override) + Katman 2'yi shape-sync ve blur olmadan, sade lerp ile çalışır hale getir. Physics/rotation/blur/trail üstüne katmanlı eklenmeli — hepsini tek seferde yazmaya çalışmak debug'ı işkenceye çevirir, özellikle Raw Input + layered window kombinasyonu ilk denemede nadiren sorunsuz çalışır.
- **Crash-safe restore, opsiyonel değil, gün 1 gereksinimi olmalı.** Cursor scheme'i override eden bir process crash olursa kullanıcı sistem cursor'unu manuel resetlemek zorunda kalır — bu tarz bir tool için en hızlı güven kaybı sebebi budur. Watchdog process ya da en azından `SetUnhandledExceptionFilter` + registry restore-on-exit şart.
- **Shape-sync katmanı en kırılgan parça.** `GetIconInfo`/`GetObject` her cursor tipinde tutarlı çalışmayabilir (özellikle bazı oyun/app'lerin custom animated cursor'ları, ör. `.ani` dosyaları). Bunun için fallback şart: tanınamayan bir cursor geldiğinde smooth katman default arrow skin'ine düşsün, crash/görsel bozulma olmasın.
- **Blur'u başta abartma.** Accumulation-buffer motion blur GPU maliyeti düşük FPS'lerde (60Hz monitör) tam tersi etki yaratıp "bulanık, gecikmeli, rahatsız edici" bir his verebilir — özellikle senin hedef kitlen (simracing izleyicisi) yüksek refresh rate alışkanlığında olabilir ama herkes 144Hz+ monitör kullanmıyor. Blur intensity + trail length'i config'e aç, agresif default seçme.
- **Anti-cheat exclude-list'i baştan tasarla, sonradan yama gibi eklemeye çalışma.** Process-based auto-disable (foreground exclusive-fullscreen process'i algılayıp overlay'i otomatik suspend eden bir watcher) mimarinin bir parçası olmalı, yoksa BeamNG/diğer sim oyunlarıyla kendi tool'un çakışır — ki bu tool'u muhtemelen kendi içerik prodüksiyonunda da kullanacaksın, bu riski göz ardı etme.
- **C++ + Win32 + Direct2D doğru seçim**, ama build/dağıtım tarafını unutma: bu bir sistem-seviye tray app, code signing olmadan Windows Defender/SmartScreen ilk çalıştırmada engelleyebilir — eğer bunu n0sther içerik/tool tarafında paylaşmayı düşünüyorsan (indirilebilir tool olarak), signing'i roadmap'e ekle, geliştirme bitince sürpriz olmasın.
