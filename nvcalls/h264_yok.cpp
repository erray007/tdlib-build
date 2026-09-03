// H264 kodlayici/cozucu fabrikasinin bos karsiligi.
//
// NEDEN: WebRTC'nin ffmpeg tabanli H264 COZUCUSU (h264_decoder_impl.cc)
// arm64'te linki dusuruyor - Telegram'in o mimari icin verdigi ffmpeg
// arsivi PIC olmayan assembly tablolari tasiyor:
//
//   relocation R_AARCH64_ADR_PREL_PG_HI21 cannot be used against
//   symbol 'ff_cos_1024'; recompile with -fPIC
//
// Ilginc olan, ayni derlemenin x86, x86_64 ve armeabi-v7a'da sorunsuz
// gecmesi; sorun yalniz arm64 arsivinde.
//
// Cozucu cikarilinca h264.cc onu aramaya devam etti
// (H264Decoder::Create). Zinciri kovalamak yerine H264 fabrikasinin
// tamami bu dosyayla degistirildi.
//
// SESLI CAGRIDA HICBIRI KULLANILMIYOR: video yok, dolayisiyla ne
// kodlayici ne cozucu isteniyor. Fabrikalarin bos donmesi WebRTC icin
// gecerli bir durum - "bu cihazda H264 desteklenmiyor" demekle ayni.
//
// VIDEO EKLENDIGINDE: bu dosya kaldirilip h264.cc ile
// h264_decoder_impl.cc geri alinmali. O zaman arm64 icin ffmpeg'i PIC
// ile yeniden derlemek ya da cozucuyu Android'in donanim kodegine
// (MediaCodec / AndroidVideoDecoder) birakmak gerekiyor - ikincisi
// zaten Java tarafinda hazir duruyor.

#include "modules/video_coding/codecs/h264/include/h264.h"

namespace webrtc {

std::unique_ptr<H264Encoder> H264Encoder::Create() {
    return nullptr;
}

std::unique_ptr<H264Encoder> H264Encoder::Create(const cricket::VideoCodec & /*codec*/) {
    return nullptr;
}

bool H264Encoder::IsSupported() {
    return false;
}

bool H264Encoder::SupportsScalabilityMode(ScalabilityMode /*scalability_mode*/) {
    return false;
}

std::unique_ptr<H264Decoder> H264Decoder::Create() {
    return nullptr;
}

bool H264Decoder::IsSupported() {
    return false;
}

std::vector<SdpVideoFormat> SupportedH264Codecs(bool /*add_scalability_modes*/) {
    return {};
}

std::vector<SdpVideoFormat> SupportedH264DecoderCodecs() {
    return {};
}

void DisableRtcUseH264() {}

} // namespace webrtc
