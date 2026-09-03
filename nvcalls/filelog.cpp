// Telegram'in gunluk sinifinin yerine gecen kucuk uygulama.
//
// NEDEN GEREKLI: Telegram, WebRTC'nin kirpilmis kopyasina KENDI gunluk
// cagrilarini eklemis. Iki dosya bunu yapiyor:
//
//   webrtc/video/video_source_sink_controller.cc
//   webrtc/modules/utility/source/jvm_android.cc
//
// Yani sesli cagri icin sart olan kod, Telegram'in tgnet kutuphanesindeki
// FileLog sinifina bagli hale gelmis. tgnet'i derlemek koca bir zincir
// aciyor (ConnectionsManager, ag katmani, veritabani); oysa gereken tek
// sey bu dort islevin var olmasi.
//
// Baslik Telegram'inki kullaniliyor (tgnet/FileLog.h) - isim bozulmasi
// birebir tutmali, yoksa linker yine bulamaz.

#include "tgnet/FileLog.h"

#include <android/log.h>
#include <cstdarg>
#include <cstdio>

// tgnet bu bayragi kendi kuruyor; burada kapali basliyor.
bool LOGS_ENABLED = false;

FileLog::FileLog() = default;

void FileLog::init(std::string path) {
    // Dosyaya yazma yok: gunluk cihazda logcat'e gidiyor, ayri bir dosya
    // tutmak uygulamanin isine yaramiyor.
}

FileLog &FileLog::getInstance() {
    static FileLog ornek;
    return ornek;
}

namespace {

/**
 * Bicimlendirip logcat'e yazar.
 *
 * LOGS_ENABLED kapaliyken hicbir sey yapmiyor: bu cagrilar ses yolunda,
 * yani saniyede onlarca kez gecilen kod. Surekli bicimlendirme yapmak
 * gereksiz yuk olurdu.
 */
void yaz(int oncelik, const char *bicim, va_list args) {
    if (!LOGS_ENABLED) return;
    __android_log_vprint(oncelik, "NVGramCagri", bicim, args);
}

} // namespace

void FileLog::fatal(const char *message, ...) {
    va_list args;
    va_start(args, message);
    yaz(ANDROID_LOG_FATAL, message, args);
    va_end(args);
}

void FileLog::e(const char *message, ...) {
    va_list args;
    va_start(args, message);
    yaz(ANDROID_LOG_ERROR, message, args);
    va_end(args);
}

void FileLog::w(const char *message, ...) {
    va_list args;
    va_start(args, message);
    yaz(ANDROID_LOG_WARN, message, args);
    va_end(args);
}

void FileLog::d(const char *message, ...) {
    va_list args;
    va_start(args, message);
    yaz(ANDROID_LOG_DEBUG, message, args);
    va_end(args);
}

// ref/delref: Telegram'in kendi basvuru sayaci izleri. Bizde karsiligi
// yok, cagrilari sessizce yutuluyor.
void FileLog::ref(const char *message, ...) {}

void FileLog::delref(const char *message, ...) {}
