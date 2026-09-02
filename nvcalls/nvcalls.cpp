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
#include <memory>
#include <string>
#include <vector>

#include "Instance.h"
#include "InstanceImpl.h"
#include "StaticThreads.h"
#include "v2/InstanceV2Impl.h"
#include "v2/InstanceV2ReferenceImpl.h"

#include <rtc_base/ssl_adapter.h>
#include <sdk/android/native_api/base/init.h>
#include <sdk/android/native_api/jni/jvm.h>

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

} // namespace

extern "C" {

/**
 * Kutuphaneyi hazirlar.
 *
 * InitAndroid ve InitializeSSL bir kez cagrilmali; ikisi de WebRTC'nin
 * kendi ic durumunu kuruyor.
 */
JNIEXPORT jint JNI_OnLoad(JavaVM *vm, void * /*ayrilmis*/) {
    webrtc::InitAndroid(vm);
    webrtc::JVM::Initialize(vm);
    rtc::InitializeSSL();
    return JNI_VERSION_1_6;
}

JNIEXPORT void JNICALL
Java_com_eray_1bolat_nvgram_cagri_TgcallsMotoru_nativeKur(JNIEnv *env, jclass sinif) {
    motorSinifi = static_cast<jclass>(env->NewGlobalRef(sinif));
    durumMetodu = env->GetMethodID(sinif, "durumDegisti", "(I)V");
    sinyalMetodu = env->GetMethodID(sinif, "sinyalUretildi", "([B)V");
    sesSeviyesiMetodu = env->GetMethodID(sinif, "sesSeviyeleri", "(FF)V");
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

} // extern "C"
