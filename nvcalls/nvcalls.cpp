// NVGram'in tgcalls koprusu.
//
// TDLib cagriyi kurar ve sifreleme anahtarini verir; ses tgcalls'in isi.
// Bu dosya ikisinin arasindaki tek gecit: Kotlin'den gelen cagri
// parametrelerini tgcalls'in Descriptor'ina cevirir, tgcalls'in urettigi
// sinyal paketlerini Kotlin'e geri verir.
//
// Telegram'in kendi koprusu (org_telegram_messenger_voip_Instance.cpp)
// ornek alindi ama bilerek cok daha kucuk: video, ekran paylasimi, grup
// cagrisi ve eski libtgvoip protokolu yok. NVGram'in ihtiyaci 1:1 sesli
// cagri.

#include <jni.h>
#include <atomic>
#include <memory>
#include <string>
#include <vector>

#include "Instance.h"
#include "InstanceImpl.h"
// Instance.h PlatformContext'i yalnizca ILERI BILDIRIYOR; ondan tureyen
// bir sinif yazmak icin tam tanim gerekiyor.
#include "PlatformContext.h"
#include "StaticThreads.h"
#include "v2/InstanceV2Impl.h"
#include "v2/InstanceV2ReferenceImpl.h"
#include "group/GroupInstanceCustomImpl.h"
#include "group/GroupInstanceImpl.h"

#include <rtc_base/ssl_adapter.h>
#include <sdk/android/native_api/base/init.h>
#include <sdk/android/native_api/jni/jvm.h>
// webrtc::JVM burada; ses aygiti katmani JVM'e bu sinif uzerinden
// ulasiyor ve baslatilmazsa mikrofon hic acilmiyor.
#include <modules/utility/include/jvm_android.h>

using namespace tgcalls;

namespace {

// tgcalls uygulamalarinin kaydi.
//
// BU SATIRLAR OLMADAN CAGRI SESSIZCE CALISMAZ: Meta::Create surume gore
// uygulama ariyor, kayit yapilmamissa nullptr donuyor ve cagri kurulmus
// gorunurken ses hic baslamiyor. Kayit statik baslatma ile oluyor, yani
// bu degiskenlerin varligi sart.
//
// InstanceImplLegacy (surum 2.4.4) BILEREK YOK: o eski libtgvoip'e bagli
// ve o kutuphane derlenmiyor. Kotlin tarafindaki surum listesi de bu
// yuzden 2.4.4 icermiyor - ikisi birlikte degismeli.
const auto kayitV1 = Register<InstanceImpl>();               // 2.7.7, 5.0.0
const auto kayitV2 = Register<InstanceV2Impl>();             // 7,8,9,12,13
const auto kayitV2Ref = Register<InstanceV2ReferenceImpl>(); // 10.0.0, 11.0.0

/**
 * tgcalls'in istedigi platform baglami.
 *
 * PlatformContext bos bir arayuz (yalniz sanal yikici). Telegram'in
 * AndroidContext'i kullanilmiyor cunku onun kurucusu KOSULSUZ olarak
 * org/telegram/messenger/voip/VideoCapturerDevice sinifini ariyor - video
 * kullanilmasa bile. Olculdu: AndroidContext'e cast eden tek yer
 * VideoCameraCapturer, yani yalniz kamera yolu. Sesli cagrida hic
 * ugranmiyor.
 */
class NvgramBaglami : public PlatformContext {
public:
    explicit NvgramBaglami(jobject motor) : javaMotor(motor) {}
    // Kotlin tarafindaki SesMotoru nesnesine kuresel basvuru.
    jobject javaMotor = nullptr;
};

/** Bir cagrinin yasadigi sure boyunca tutulan her sey. */
struct Cagri {
    std::unique_ptr<Instance> ornek;
    jobject javaMotor = nullptr;
    std::shared_ptr<NvgramBaglami> baglam;
};

jclass motorSinifi = nullptr;
jmethodID durumMetodu = nullptr;
jmethodID sinyalMetodu = nullptr;
jmethodID sesSeviyesiMetodu = nullptr;

/**
 * Bir GRUP sesli sohbetinin yasadigi sure boyunca tutulan her sey.
 *
 * 1:1 cagrinin [Cagri] yapisindan AYRI: grup motorunun protokolu farkli
 * (tek seferlik katilma yuku + SSRC, mesaj mesaj sinyallesme degil) ve
 * ayni yapiya sigdirmak ikisini de bozardi.
 */
struct GrupCagri {
    std::unique_ptr<GroupInstanceInterface> ornek;
    jobject javaMotor = nullptr;
};

jclass grupMotorSinifi = nullptr;
jmethodID grupYukMetodu = nullptr;
jmethodID grupSeviyeMetodu = nullptr;
jmethodID grupAgMetodu = nullptr;

/** Java dizisini std::vector'e kopyalar. */
std::vector<uint8_t> baytlariAl(JNIEnv *env, jbyteArray dizi) {
    if (dizi == nullptr) return {};
    const auto boy = env->GetArrayLength(dizi);
    std::vector<uint8_t> sonuc(static_cast<size_t>(boy));
    env->GetByteArrayRegion(dizi, 0, boy, reinterpret_cast<jbyte *>(sonuc.data()));
    return sonuc;
}

jbyteArray baytlariVer(JNIEnv *env, const std::vector<uint8_t> &veri) {
    jbyteArray dizi = env->NewByteArray(static_cast<jsize>(veri.size()));
    env->SetByteArrayRegion(
        dizi, 0, static_cast<jsize>(veri.size()),
        reinterpret_cast<const jbyte *>(veri.data()));
    return dizi;
}

std::string metniAl(JNIEnv *env, jstring metin) {
    if (metin == nullptr) return {};
    const char *ham = env->GetStringUTFChars(metin, nullptr);
    std::string sonuc(ham ? ham : "");
    if (ham) env->ReleaseStringUTFChars(metin, ham);
    return sonuc;
}

/**
 * Geri cagrilar tgcalls'in KENDI is parcaciklarindan geliyor.
 *
 * O parcaciklar JVM'e bagli degil; once baglanmadan JNI cagrisi yapmak
 * sureci dusuruyor. AttachCurrentThreadIfNeeded bunu hallediyor ve
 * WebRTC'nin kendi cozumu oldugu icin ayrica ayirma gerekmiyor.
 */
JNIEnv *ortam() {
    return webrtc::AttachCurrentThreadIfNeeded();
}

Cagri *cagriyiAl(jlong isaretci) {
    return reinterpret_cast<Cagri *>(isaretci);
}

bool webrtcHazir = false;

/**
 * WebRTC'yi ve geri cagri kimliklerini ILK CAGRIDA hazirlar.
 *
 * Kutuphane yuklenirken degil, gercekten cagri kurulurken calisiyor: o
 * anda JVM tamamen ayakta ve uygulama baglami hazir. Bayrakla korunuyor,
 * ikinci cagride bir sey yapmiyor.
 *
 * Metot kimlikleri de burada aliniyor - Kotlin tarafindaki geri cagri
 * metotlari companion'da degil NESNENIN KENDISINDE oldugu icin sinif
 * dogrudan nesneden okunuyor.
 */
void hazirla(JNIEnv *env, jobject motor) {
    if (!webrtcHazir) {
        JavaVM *vm = nullptr;
        env->GetJavaVM(&vm);
        webrtc::InitAndroid(vm);
        webrtc::JVM::Initialize(vm);
        rtc::InitializeSSL();
        webrtcHazir = true;
    }
    if (durumMetodu != nullptr) return;

    jclass sinif = env->GetObjectClass(motor);
    motorSinifi = static_cast<jclass>(env->NewGlobalRef(sinif));
    durumMetodu = env->GetMethodID(sinif, "durumDegisti", "(I)V");
    sinyalMetodu = env->GetMethodID(sinif, "sinyalUretildi", "([B)V");
    sesSeviyesiMetodu = env->GetMethodID(sinif, "sesSeviyeleri", "(FF)V");
}

/**
 * GRUP tarafinin karsiligi: WebRTC'yi ve grup geri cagri kimliklerini
 * ilk cagrida hazirlar.
 *
 * Sinif NESNEDEN okunuyor (GetObjectClass), FindClass ile DEGIL:
 * FindClass sinif yukleyiciye bagli ve uygulama is parcacigi disindan
 * cagrilinca sinifi bulamiyor. 1:1 tarafinda da ayni karar alinmis.
 *
 * Metot adlari ve imzalari GrupMotoru.kt ile BIREBIR ayni olmali;
 * uyusmazlik derleme zamaninda degil CAGRI SIRASINDA cokuyor.
 */
void grupHazirla(JNIEnv *env, jobject motor) {
    if (!webrtcHazir) {
        JavaVM *vm = nullptr;
        env->GetJavaVM(&vm);
        webrtc::InitAndroid(vm);
        webrtc::JVM::Initialize(vm);
        rtc::InitializeSSL();
        webrtcHazir = true;
    }
    if (grupYukMetodu != nullptr) return;

    jclass sinif = env->GetObjectClass(motor);
    grupMotorSinifi = static_cast<jclass>(env->NewGlobalRef(sinif));
    grupYukMetodu =
        env->GetMethodID(sinif, "katilmaYuku", "(ILjava/lang/String;)V");
    grupSeviyeMetodu = env->GetMethodID(sinif, "sesSeviyeleri", "([I[Z)V");
    grupAgMetodu = env->GetMethodID(sinif, "agDurumu", "(Z)V");
}

} // namespace

// ---------- ses kalitesi ----------
//
// tgcalls bu iki degeri KAYNAKTA SABIT tutuyor (InstanceV2Impl.cpp:
// setMaxBitrate(32 * 1024) ve kCodecParamPTime 60) ve disaridan
// ayarlamanin yolu yok - customParameters yalnizca iki ag anahtari
// okuyor. Bulut derlemesinde o iki sabit, asagidaki fonksiyonlarin
// cagrisiyla degistiriliyor; boylece deger kullanicinin ayarindan
// geliyor ve her degisiklikte kutuphaneyi yeniden derlemek gerekmiyor.
//
// atomic: tgcalls bu fonksiyonlari KENDI is parcaciklarindan cagiriyor,
// ayar ise arayuz parcaciginda yaziliyor.
//
// Varsayilanlar tgcalls'in kendi degerleri: ayar hic dokunulmazsa
// davranis Telegram'inkiyle ayni kaliyor.
namespace {
std::atomic<int> sesBitrate{32 * 1024};
std::atomic<int> sesPtime{60};
} // namespace

extern "C" int nvgramSesBitrate() {
    return sesBitrate.load(std::memory_order_relaxed);
}

extern "C" int nvgramSesPtime() {
    return sesPtime.load(std::memory_order_relaxed);
}

extern "C" {

/**
 * JNI_OnLoad YALNIZ surum bildiriyor.
 *
 * WebRTC'yi burada baslatmak UYGULAMAYI DUSURUYOR: kutuphane
 * yuklenirken JVM'in sinif yukleyicisi ve uygulama baglami henuz
 * WebRTC'nin bekledigi halde degil. Telegram da ayni sebeple baslatmayi
 * JNI_OnLoad'a degil, ilk gercek cagriya birakiyor
 * (org_telegram_messenger_voip_Instance.cpp icindeki initWebRTC).
 */
JNIEXPORT jint JNI_OnLoad(JavaVM * /*vm*/, void * /*ayrilmis*/) {
    return JNI_VERSION_1_6;
}

/**
 * Cagriyi baslatir ve tgcalls ornegini dondurur.
 *
 * Donen deger bir isaretci; sonraki her islem onunla yapiliyor. Sifir
 * donmesi, istenen surum icin kayitli bir uygulama bulunamadigi anlamina
 * geliyor.
 */
JNIEXPORT jlong JNICALL
Java_com_eray_1bolat_nvgram_cagri_TgcallsMotoru_nativeBaslat(
    JNIEnv *env,
    jobject motor,
    jstring jSurum,
    jbyteArray jAnahtar,
    jboolean jGiden,
    jboolean jP2p,
    jstring jOzelParametreler,
    jobjectArray jSunucular,
    jstring jGunlukYolu) {

    hazirla(env, motor);

    const auto surum = metniAl(env, jSurum);

    // Sifreleme anahtari tam 256 bayt olmali; kisa gelirse tgcalls
    // tanimsiz bellek okur.
    auto anahtar = std::make_shared<std::array<uint8_t, 256>>();
    const auto anahtarBaytlari = baytlariAl(env, jAnahtar);
    if (anahtarBaytlari.size() != anahtar->size()) {
        return 0;
    }
    std::copy(anahtarBaytlari.begin(), anahtarBaytlari.end(), anahtar->begin());

    auto cagri = new Cagri();
    cagri->javaMotor = env->NewGlobalRef(motor);
    cagri->baglam = std::make_shared<NvgramBaglami>(cagri->javaMotor);

    const auto javaMotor = cagri->javaMotor;

    Descriptor tanim = {
        .config = Config{
            .initializationTimeout = 30.0,
            .receiveTimeout = 20.0,
            .dataSaving = DataSaving::Never,
            .enableP2P = jP2p == JNI_TRUE,
            .allowTCP = true,
            .enableStunMarking = true,
            // Yanki bastirma, gurultu bastirma ve otomatik kazanc:
            // ucu de acik. Ekran okuyucu kullanicilari cogu zaman
            // hoparlorden konusuyor ve yanki bastirma olmadan karsi
            // taraf kendi sesini duyuyor.
            .enableAEC = true,
            .enableNS = true,
            .enableAGC = true,
            .enableVolumeControl = true,
            .logPath = {metniAl(env, jGunlukYolu)},
            .maxApiLayer = 92,
            .customParameters = metniAl(env, jOzelParametreler),
        },
        .encryptionKey = EncryptionKey(std::move(anahtar), jGiden == JNI_TRUE),
        .stateUpdated = [javaMotor](State durum) {
            JNIEnv *e = ortam();
            e->CallVoidMethod(javaMotor, durumMetodu, static_cast<jint>(durum));
        },
        .audioLevelsUpdated = [javaMotor](float benim, float karsi) {
            JNIEnv *e = ortam();
            e->CallVoidMethod(javaMotor, sesSeviyesiMetodu, benim, karsi);
        },
        .signalingDataEmitted = [javaMotor](const std::vector<uint8_t> &veri) {
            JNIEnv *e = ortam();
            jbyteArray dizi = baytlariVer(e, veri);
            e->CallVoidMethod(javaMotor, sinyalMetodu, dizi);
            e->DeleteLocalRef(dizi);
        },
        .platformContext = cagri->baglam,
    };
    tanim.version = surum;

    // Sunucular: her biri {adres, adresV6, port, yansiticiMi, etiket,
    // kullanici, parola, turn, stun, tcp} alanlarini tasiyan bir dizi
    // olarak geliyor. Kotlin tarafinda ayri bir sinif tutmak yerine duz
    // dizi kullaniliyor - JNI'de alan alan okumak hem yavas hem kirilgan.
    const auto sunucuSayisi = jSunucular ? env->GetArrayLength(jSunucular) : 0;
    for (jsize i = 0; i < sunucuSayisi; ++i) {
        auto satir = static_cast<jobjectArray>(env->GetObjectArrayElement(jSunucular, i));
        if (satir == nullptr) continue;

        const auto alan = [&](jsize n) {
            return metniAl(env, static_cast<jstring>(env->GetObjectArrayElement(satir, n)));
        };

        const auto adres = alan(0);
        const auto adresV6 = alan(1);
        const auto port = static_cast<uint16_t>(std::stoi(alan(2)));
        const auto yansiticiMi = alan(3) == "1";

        if (yansiticiMi) {
            // Telegram yansiticisi: etiket onaltilik metin olarak geliyor.
            const auto etiketMetni = alan(4);
            Endpoint uc;
            uc.endpointId = static_cast<int64_t>(i + 1);
            uc.host = EndpointHost{adres, adresV6};
            uc.port = port;
            uc.type = EndpointType::UdpRelay;
            for (size_t b = 0; b + 1 < etiketMetni.size() && b / 2 < 16; b += 2) {
                uc.peerTag[b / 2] = static_cast<uint8_t>(
                    std::stoi(etiketMetni.substr(b, 2), nullptr, 16));
            }
            tanim.endpoints.push_back(uc);
        } else {
            RtcServer sunucu;
            sunucu.id = static_cast<uint8_t>(i + 1);
            sunucu.host = adres;
            sunucu.port = port;
            sunucu.login = alan(5);
            sunucu.password = alan(6);
            sunucu.isTurn = alan(7) == "1";
            sunucu.isTcp = alan(9) == "1";
            tanim.rtcServers.push_back(std::move(sunucu));
        }
        env->DeleteLocalRef(satir);
    }

    cagri->ornek = Meta::Create(surum, std::move(tanim));
    if (!cagri->ornek) {
        // Kayitli uygulama yok: Kotlin tarafindaki surum listesi ile
        // buradaki Register satirlari ayrismis demektir.
        env->DeleteGlobalRef(cagri->javaMotor);
        delete cagri;
        return 0;
    }
    return reinterpret_cast<jlong>(cagri);
}

JNIEXPORT void JNICALL
Java_com_eray_1bolat_nvgram_cagri_TgcallsMotoru_nativeSinyalAl(
    JNIEnv *env, jobject /*motor*/, jlong isaretci, jbyteArray jVeri) {
    auto cagri = cagriyiAl(isaretci);
    if (!cagri || !cagri->ornek) return;
    cagri->ornek->receiveSignalingData(baytlariAl(env, jVeri));
}

JNIEXPORT void JNICALL
Java_com_eray_1bolat_nvgram_cagri_TgcallsMotoru_nativeSesiKes(
    JNIEnv * /*env*/, jobject /*motor*/, jlong isaretci, jboolean kesik) {
    auto cagri = cagriyiAl(isaretci);
    if (!cagri || !cagri->ornek) return;
    cagri->ornek->setMuteMicrophone(kesik == JNI_TRUE);
}

JNIEXPORT void JNICALL
Java_com_eray_1bolat_nvgram_cagri_TgcallsMotoru_nativeAgDegisti(
    JNIEnv * /*env*/, jobject /*motor*/, jlong isaretci, jint tur) {
    auto cagri = cagriyiAl(isaretci);
    if (!cagri || !cagri->ornek) return;
    cagri->ornek->setNetworkType(static_cast<NetworkType>(tur));
}

/**
 * Ses kalitesini ayarlar.
 *
 * CAGRI BASLAMADAN ONCE cagrilmali: ptime kodek kurulurken bir kez
 * okunuyor, sonradan degistirmek suren cagriyi etkilemiyor.
 *
 * bitrateBps yalnizca TAVAN; setMaxBitrate min_bitrate_bps'e dokunmadigi
 * icin kotu agda Opus kendi asagi iniyor.
 */
JNIEXPORT void JNICALL
Java_com_eray_1bolat_nvgram_cagri_TgcallsMotoru_nativeSesKalitesi(
    JNIEnv * /*env*/, jobject /*motor*/, jint bitrateBps, jint ptimeMs) {
    sesBitrate.store(bitrateBps, std::memory_order_relaxed);
    sesPtime.store(ptimeMs, std::memory_order_relaxed);
}

/** Kullanilan yansiticinin kimligi; TDLib cagri kapatilirken istiyor. */
JNIEXPORT jlong JNICALL
Java_com_eray_1bolat_nvgram_cagri_TgcallsMotoru_nativeBaglantiId(
    JNIEnv * /*env*/, jobject /*motor*/, jlong isaretci) {
    auto cagri = cagriyiAl(isaretci);
    if (!cagri || !cagri->ornek) return 0;
    return static_cast<jlong>(cagri->ornek->getPreferredRelayId());
}

/**
 * Cagriyi durdurur.
 *
 * stop() geri cagrili: tgcalls kapanisi kendi is parcaciginda yapiyor ve
 * bitmeden nesne silinemez. Silme islemi bu yuzden geri cagrinin icinde.
 */
JNIEXPORT void JNICALL
Java_com_eray_1bolat_nvgram_cagri_TgcallsMotoru_nativeDurdur(
    JNIEnv *env, jobject /*motor*/, jlong isaretci) {
    auto cagri = cagriyiAl(isaretci);
    if (!cagri) return;
    if (!cagri->ornek) {
        if (cagri->javaMotor) env->DeleteGlobalRef(cagri->javaMotor);
        delete cagri;
        return;
    }
    auto ornek = cagri->ornek.release();
    ornek->stop([cagri, ornek](FinalState) {
        JNIEnv *e = ortam();
        if (cagri->javaMotor) e->DeleteGlobalRef(cagri->javaMotor);
        delete ornek;
        delete cagri;
    });
}

// =====================================================================
// GRUP SESLI SOHBET
//
// Grup motoru libtgcalls.a icinde ZATEN derli (Telegram'in jni/voip/
// CMakeLists.txt'si group/ dosyalarini acikca listeliyor ve bizim
// CMakeLists yalniz legacy/'yi cikariyor). Buradaki atiflar olmadan
// baglayici o nesneleri ATIYORDU - libnvcalls.so'da sembolun
// bulunmamasinin sebebi buydu, derlenmemis olmasi degil.
// =====================================================================

JNIEXPORT jlong JNICALL
Java_com_eray_1bolat_nvgram_cagri_GrupMotoru_nativeGrupBaslat(
    JNIEnv *env, jobject motor) {
    grupHazirla(env, motor);

    auto *cagri = new GrupCagri();
    cagri->javaMotor = env->NewGlobalRef(motor);
    auto *javaMotor = cagri->javaMotor;

    GroupInstanceDescriptor tanim;
    tanim.threads = StaticThreads::getThreads();
    tanim.config.need_log = false;

    tanim.networkStateUpdated = [javaMotor](GroupNetworkState durum) {
        JNIEnv *e = ortam();
        e->CallVoidMethod(javaMotor, grupAgMetodu,
                          static_cast<jboolean>(durum.isConnected));
    };

    // KENDI SEVIYEMIZ SSRC 0 ILE GELIYOR, gercek SSRC'mizle DEGIL.
    // Kotlin tarafi sifiri arayacak; gercek SSRC aranirsa kendimizi hic
    // bulamaz ve konustugumuz sunucuya hic bildirilmez.
    tanim.audioLevelsUpdated = [javaMotor](GroupLevelsUpdate const &g) {
        JNIEnv *e = ortam();
        const auto n = static_cast<jsize>(g.updates.size());
        std::vector<jint> s(static_cast<std::size_t>(n));
        std::vector<jboolean> k(static_cast<std::size_t>(n));
        for (jsize i = 0; i < n; ++i) {
            s[static_cast<std::size_t>(i)] =
                static_cast<jint>(g.updates[static_cast<std::size_t>(i)].ssrc);
            k[static_cast<std::size_t>(i)] =
                g.updates[static_cast<std::size_t>(i)].value.voice ? JNI_TRUE
                                                                   : JNI_FALSE;
        }
        jintArray ssrcler = e->NewIntArray(n);
        jbooleanArray konusanlar = e->NewBooleanArray(n);
        if (n > 0) {
            e->SetIntArrayRegion(ssrcler, 0, n, s.data());
            e->SetBooleanArrayRegion(konusanlar, 0, n, k.data());
        }
        e->CallVoidMethod(javaMotor, grupSeviyeMetodu, ssrcler, konusanlar);
        // YEREL BASVURULAR SILINIYOR: bu geri cagri saniyede onlarca kez
        // geliyor ve yerel basvuru tablosu dolarsa JVM sureci dusuruyor.
        e->DeleteLocalRef(ssrcler);
        e->DeleteLocalRef(konusanlar);
    };

    // DOGRUDAN YAPICI: bu sinifta Meta::Create gibi bir fabrika yok ve
    // 1:1'deki surum secimi grupta gecerli degil - tek uygulama var.
    cagri->ornek =
        std::make_unique<GroupInstanceCustomImpl>(std::move(tanim));
    return reinterpret_cast<jlong>(cagri);
}

JNIEXPORT void JNICALL
Java_com_eray_1bolat_nvgram_cagri_GrupMotoru_nativeGrupKatilmaYukuIste(
    JNIEnv * /*env*/, jobject /*motor*/, jlong isaretci) {
    auto *c = reinterpret_cast<GrupCagri *>(isaretci);
    if (c == nullptr || !c->ornek) return;
    auto *javaMotor = c->javaMotor;
    c->ornek->emitJoinPayload([javaMotor](GroupJoinPayload const &yuk) {
        JNIEnv *e = ortam();
        jstring json = e->NewStringUTF(yuk.json.c_str());
        e->CallVoidMethod(javaMotor, grupYukMetodu,
                          static_cast<jint>(yuk.audioSsrc), json);
        e->DeleteLocalRef(json);
    });
}

JNIEXPORT void JNICALL
Java_com_eray_1bolat_nvgram_cagri_GrupMotoru_nativeGrupKatilmaCevabi(
    JNIEnv *env, jobject /*motor*/, jlong isaretci, jstring jJson) {
    auto *c = reinterpret_cast<GrupCagri *>(isaretci);
    if (c == nullptr || !c->ornek) return;
    c->ornek->setJoinResponsePayload(metniAl(env, jJson));
}

JNIEXPORT void JNICALL
Java_com_eray_1bolat_nvgram_cagri_GrupMotoru_nativeGrupSesiKes(
    JNIEnv * /*env*/, jobject /*motor*/, jlong isaretci, jboolean kesik) {
    auto *c = reinterpret_cast<GrupCagri *>(isaretci);
    if (c != nullptr && c->ornek) c->ornek->setIsMuted(kesik == JNI_TRUE);
}

JNIEXPORT void JNICALL
Java_com_eray_1bolat_nvgram_cagri_GrupMotoru_nativeGrupSesSeviyesi(
    JNIEnv * /*env*/, jobject /*motor*/, jlong isaretci, jint ssrc,
    jdouble carpan) {
    auto *c = reinterpret_cast<GrupCagri *>(isaretci);
    if (c != nullptr && c->ornek)
        c->ornek->setVolume(static_cast<uint32_t>(ssrc), carpan);
}

JNIEXPORT void JNICALL
Java_com_eray_1bolat_nvgram_cagri_GrupMotoru_nativeGrupDurdur(
    JNIEnv *env, jobject /*motor*/, jlong isaretci) {
    auto *c = reinterpret_cast<GrupCagri *>(isaretci);
    if (c == nullptr) return;
    if (c->ornek) {
        // stop() tamamlamasi MEDYA is parcaciginda calisiyor, yani orada
        // silmek calismakta olan nesneyi silmek olurdu. Yikim burada
        // yapiliyor ve ~GroupInstanceCustomImpl medya is parcaciginda
        // BlockingCall ile bekliyor - yani BU CAGRI BLOKLUYOR. Kotlin
        // tarafi bunu arayuz is parcaciginda cagirmamali.
        c->ornek->stop([] {});
        c->ornek.reset();
    }
    if (c->javaMotor != nullptr) env->DeleteGlobalRef(c->javaMotor);
    delete c;
}

} // extern "C"
