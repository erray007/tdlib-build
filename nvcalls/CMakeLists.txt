# NVGram'in cagri kutuphanesi: libnvcalls.so
#
# Bu dosya Telegram'in kendi jni/CMakeLists.txt'sinin SONUNA include
# ediliyor; oradaki butun hedefler ve yollar tanimli halde geliyor.

# --- opus ---
#
# Opus ayri bir statik hedef DEGIL: Telegram onu dogrudan ana .so'sunun
# kaynak listesine koyuyor. tgcalls sesi opus ile kodluyor, yani bu 153
# dosya olmadan koprumuz "undefined reference to opus_encoder_create" ile
# dusuyor.
#
# Kaynak listesi elle yazilmiyor, ana hedeften SUZULUYOR: Telegram surumu
# yukseltildiginde opus dosyalari degisse bile bu dosyaya dokunmak
# gerekmesin. Ayni sebeple derleme tanimlari ve include yollari da ana
# hedeften aliniyor - opus sabit noktali (FIXED_POINT) derleniyor ve o
# tanim olmadan yanlis kod uretiliyor.
get_target_property(NVCALLS_ANA_KAYNAKLAR ${NATIVE_LIB} SOURCES)
get_target_property(NVCALLS_ANA_TANIMLAR ${NATIVE_LIB} COMPILE_DEFINITIONS)
get_target_property(NVCALLS_ANA_YOLLAR ${NATIVE_LIB} INCLUDE_DIRECTORIES)

# opus SES kodlayicisi, libyuv ise GORUNTU bicimi cevirileri icin.
# Ikisi de ayri hedef degil, ana .so'nun kaynak listesinde duruyorlar.
# libyuv sesli cagrida bile gerekiyor: tgcalls'in ortak kodu ona atif
# uretiyor ve eksik oldugunda kutuphane cihazda hic yuklenmiyor
# ("cannot locate symbol I420Copy").
set(NVCALLS_EK_KAYNAKLAR ${NVCALLS_ANA_KAYNAKLAR})
list(FILTER NVCALLS_EK_KAYNAKLAR INCLUDE REGEX "opus/|libyuv/")

list(LENGTH NVCALLS_EK_KAYNAKLAR NVCALLS_EK_SAYISI)
message(STATUS "nvcalls: ${NVCALLS_EK_SAYISI} opus/libyuv kaynagi suzuldu")
if(NVCALLS_EK_SAYISI LESS 100)
    # Suzgec tutmadiysa sessizce eksik bir kutuphane uretmek yerine
    # burada duruluyor; eksigi cihazda kesfetmek cok pahali.
    message(FATAL_ERROR "nvcalls: opus/libyuv kaynaklari bulunamadi, suzgec guncellenmeli")
endif()

# --- eski VoIP protokolunu disarida birak ---
#
# tgcalls hedefi legacy/InstanceImplLegacy.cpp'yi de derliyor ve o dosya
# ESKI libtgvoip kutuphanesine bagli. Biz o kutuphaneyi derlemiyoruz;
# sonucu, kutuphanenin cihazda hic yuklenememesi oluyordu:
#   dlopen failed: cannot locate symbol "_ZTVN6tgvoip11IPv4AddressE"
#
# Legacy protokolu (surum 2.4.4) zaten kullanilmiyor - Kotlin tarafindaki
# surum listesinde yok ve koprude kaydi da yapilmiyor. Dosyayi hedeften
# cikarmak, libtgvoip'i butun Android JNI yuku ile birlikte derlemekten
# hem kucuk hem risksiz.
get_target_property(NVCALLS_TGCALLS_KAYNAKLARI tgcalls SOURCES)
list(FILTER NVCALLS_TGCALLS_KAYNAKLARI EXCLUDE REGEX "tgcalls/legacy/")

# --- ffmpeg tabanli H264 cozucusunu de disarida birak ---
#
# h264_decoder_impl.cc avcodec'e bagli ve o bagimlilik arm64'te linki
# tumden dusuruyor:
#   relocation ... cannot be used against symbol 'ff_cos_1024';
#   recompile with -fPIC
#
# Telegram'in arm64 ffmpeg arsivi PIC olmayan assembly tablolari
# tasiyor; paylasilan kutuphaneye konamiyorlar. Ilginc olan, ayni
# derlemenin x86, x86_64 ve armeabi-v7a'da SORUNSUZ gecmesi - sorun
# yalniz o mimarinin arsivinde.
#
# Cozucu sesli cagrida zaten kullanilmiyor: video yok. Cikarilmasi hem
# linki duzeltiyor hem de ffmpeg'i tumden gereksiz kiliyor.
# Cozucu ve fabrika birlikte cikiyor: cozucu tek basina cikarilinca
# h264.cc onu aramaya devam ediyor (H264Decoder::Create). Yerlerine
# nvcalls/h264_yok.cpp geciyor.
list(FILTER NVCALLS_TGCALLS_KAYNAKLARI EXCLUDE REGEX "codecs/h264/h264_decoder_impl")
list(FILTER NVCALLS_TGCALLS_KAYNAKLARI EXCLUDE REGEX "codecs/h264/h264\.cc")
set_target_properties(tgcalls PROPERTIES SOURCES "${NVCALLS_TGCALLS_KAYNAKLARI}")

# --- Telegram'in kendi JNI koprusunu disarida birak ---
#
# voipandroid hedefi org_telegram_messenger_voip_Instance.cpp'yi de
# iceriyor: Telegram'in KENDI cagri koprusu. Bizim kendi koprumuz var
# (nvcalls.cpp) ve o dosya hem Telegram'in Java siniflarini hem de
# legacy protokolunu cagiriyor:
#   undefined symbol: tgcalls::Register<tgcalls::InstanceImplLegacy>()
#
# Cikarilmasi hem bu bagi kesiyor hem de ayni JNI islevlerinin iki kez
# tanimlanmasini engelliyor.
get_target_property(NVCALLS_ANDROID_KAYNAKLARI voipandroid SOURCES)
list(FILTER NVCALLS_ANDROID_KAYNAKLARI EXCLUDE REGEX "org_telegram_messenger_voip_Instance")
set_target_properties(voipandroid PROPERTIES SOURCES "${NVCALLS_ANDROID_KAYNAKLARI}")

# --- linker bayraklarini temizle ---
#
# Ana CMakeLists CMAKE_SHARED_LINKER_FLAGS icine uzun bir --exclude-libs
# listesi koyuyor: libtgnet.a, libflac.a, librlottie.a, libsqlite.a...
# Bunlar Telegram'in kendi hedefleri ve biz onlari DERLEMIYORUZ. Bayrak
# global oldugu icin bizim hedefimize de gecip linki dusuruyor
# ("cannot open libtgnet.a").
#
# Degisken hedef olusturulurken okundugu icin temizlik add_library'den
# ONCE yapilmali. Ana hedef coktan tanimlandigindan etkilenmiyor.
set(CMAKE_SHARED_LINKER_FLAGS "")

add_library(nvcalls SHARED
        ${CMAKE_CURRENT_LIST_DIR}/nvcalls.cpp
        # Telegram'in gunluk sinifinin yerine gecen kucuk uygulama;
        # neden gerektigi dosyanin basinda yaziyor.
        ${CMAKE_CURRENT_LIST_DIR}/filelog.cpp
        # H264 fabrikasinin bos karsiligi; sebebi dosyanin basinda.
        ${CMAKE_CURRENT_LIST_DIR}/h264_yok.cpp
        ${NVCALLS_EK_KAYNAKLAR})

target_compile_definitions(nvcalls PRIVATE ${NVCALLS_ANA_TANIMLAR})
target_include_directories(nvcalls PRIVATE ${NVCALLS_ANA_YOLLAR})
target_compile_options(nvcalls PRIVATE -O3 -fexceptions -frtti -Wno-unknown-pragmas)

# 16 KB sayfa boyutu: Android 15'ten beri cihazlar bu boyutu
# kullanabiliyor ve Play, targetSdk 35+ uygulamalarda uyumu zorunlu
# tutuyor. Bayrak olmadan kutuphane bu cihazlarda hic yuklenmiyor.
target_link_options(nvcalls PRIVATE "-Wl,-z,max-page-size=16384")
target_link_options(nvcalls PRIVATE "-Wl,-z,common-page-size=16384")

# --exclude-libs YALNIZ tgcalls arsivlerine uygulaniyor, ALL'a DEGIL.
#
# ALL denendi ve cagri sirasinda uygulamayi dusurdu:
#   UnsatisfiedLinkError: No implementation found for
#   org.webrtc.SoftwareVideoEncoderFactory.nativeCreateFactory()
#
# Sebep: voipandroid arsivi WebRTC'nin Java'dan cagrilan JNI islevlerini
# tasiyor (sdk/android/src/jni/*). ALL onlarin da disa acilmasini
# engelliyor ve Java tarafi karsiligini bulamiyor. Telegram'in kendi link
# satiri da voipandroid'i bu listeye KOYMUYOR.
#
# tgcalls arsivleri listede kaliyor: iclerinde disa acilmasi gereken JNI
# yok ve boylece WebRTC'nin sembolleri TDLib'inkilerle cakismiyor - ikisi
# de ayni surecte yuklu ve ikisinde de boringssl var.
target_link_options(nvcalls PRIVATE "-Wl,--exclude-libs,libtgcalls.a")
target_link_options(nvcalls PRIVATE "-Wl,--exclude-libs,libtgcalls_tp.a")

# Cozumlenmemis sembol varsa LINK DUSSUN.
#
# Varsayilan olarak paylasilan kutuphaneler eksik sembolle de
# linkleniyor ve hata ancak CIHAZDA, dlopen sirasinda ortaya cikiyor.
# Her eksik icin ayri bir derleme-kurulum-deneme dongusu gerekiyordu;
# bu bayrakla hepsi bir kerede burada goruluyor.
target_link_options(nvcalls PRIVATE "-Wl,--no-undefined")

# whole-archive olanlar: bu uc kutuphane isini STATIK KAYITLA yapiyor
# (ses aygiti kaydi, kodlayici kaydi). Linker "kimse cagirmiyor" deyip
# atarsa kayit hic olmuyor ve cagri sessizce sessiz kaliyor - Telegram'in
# kendi link satiri da tam olarak bu ucunu boyle veriyor.
target_link_libraries(nvcalls
        -Wl,--whole-archive rnnoise openh264 voipandroid -Wl,--no-whole-archive
        tgcalls
        tgcalls_tp
        ssl
        crypto
        libvpx
        libdav1d
        # ffmpeg BILEREK YOK: tek kullanicisi olan H264 cozucusu
        # yukarida hedeften cikarildi. Linklenseydi arm64'te PIC
        # hatasiyla dusuyordu.
        log
        OpenSLES
        android
        GLESv2
        EGL
        z)
