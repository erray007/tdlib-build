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

set(NVCALLS_OPUS ${NVCALLS_ANA_KAYNAKLAR})
list(FILTER NVCALLS_OPUS INCLUDE REGEX "opus/")

list(LENGTH NVCALLS_OPUS NVCALLS_OPUS_SAYISI)
message(STATUS "nvcalls: ${NVCALLS_OPUS_SAYISI} opus kaynagi suzuldu")
if(NVCALLS_OPUS_SAYISI LESS 50)
    # Suzgec tutmadiysa (Telegram opus'u baska turlu derlemeye basladiysa)
    # sessizce eksik bir kutuphane uretmek yerine burada duruluyor.
    message(FATAL_ERROR "nvcalls: opus kaynaklari bulunamadi, suzgec guncellenmeli")
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
set_target_properties(tgcalls PROPERTIES SOURCES "${NVCALLS_TGCALLS_KAYNAKLARI}")

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
        ${NVCALLS_OPUS})

target_compile_definitions(nvcalls PRIVATE ${NVCALLS_ANA_TANIMLAR})
target_include_directories(nvcalls PRIVATE ${NVCALLS_ANA_YOLLAR})
target_compile_options(nvcalls PRIVATE -O3 -fexceptions -frtti -Wno-unknown-pragmas)

# 16 KB sayfa boyutu: Android 15'ten beri cihazlar bu boyutu
# kullanabiliyor ve Play, targetSdk 35+ uygulamalarda uyumu zorunlu
# tutuyor. Bayrak olmadan kutuphane bu cihazlarda hic yuklenmiyor.
target_link_options(nvcalls PRIVATE "-Wl,-z,max-page-size=16384")
target_link_options(nvcalls PRIVATE "-Wl,-z,common-page-size=16384")

# Statik kutuphanelerin sembolleri disa acilmiyor. Disariya yalniz kendi
# JNI islevlerimiz cikiyor - onlar bu hedefin kendi kaynagindan geldigi
# icin etkilenmiyorlar. Kazanci iki yonlu: kutuphane kuculuyor ve
# WebRTC'nin sembolleri TDLib'inkilerle cakismiyor (ikisi de ayni
# surecte yuklu ve ikisinde de boringssl var).
target_link_options(nvcalls PRIVATE "-Wl,--exclude-libs,ALL")

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
        log
        OpenSLES
        android
        GLESv2
        EGL
        z)
