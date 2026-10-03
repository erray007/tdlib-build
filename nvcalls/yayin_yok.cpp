// Grup YAYIN AKISI parcalarinin bos karsiligi.
//
// NEDEN: grup sesli sohbetin yayin akisi dosyalari ffmpeg'e bagli
// (AudioStreamingPartInternal, AudioStreamingPartPersistentDecoder,
// VideoStreamingPart) ve ffmpeg'i libnvcalls.so'ya linklemek arm64'te
// mumkun degil:
//
//   relocation R_AARCH64_ADR_PREL_PG_HI21 cannot be used against
//   symbol 'ff_cos_1024'; recompile with -fPIC
//
// Telegram'in arm64 ffmpeg arsivi PIC olmayan assembly tablolari
// tasiyor; paylasilan kutuphaneye konamiyorlar. Ayni sorun H264
// cozucusunde de cikmisti ve orada da ayni cozum uygulandi
// (h264_yok.cpp).
//
// ONEMLI AYRIM: bu dosya ffmpeg BASLIKLARINI kullaniyor (AVCodecParameters
// gibi turler imzalarda geciyor) ama ffmpeg KUTUPHANESINE linklenmiyor.
// Sorun olan linkleme, derleme degil.
//
// YAYIN AKISI SESLI SOHBETTE KULLANILMIYOR: bu yol kanal CANLI
// YAYINLARINI dinlemek icin (alt proje B). Gruplarda ve kanallarda sesli
// sohbete katilma (alt proje A) yayin parcalarina hic ugramiyor; onlar
// yalnizca GroupInstanceCustomImpl -> StreamingMediaContext zincirinden
// atif aldiklari icin LINKLENMELERI gerekiyor, CALISMALARI gerekmiyor.
//
// KANAL CANLI YAYINI EKLENDIGINDE: bu dosya kaldirilip gercek dosyalar
// geri alinmali. O zaman arm64 icin ffmpeg'i -fPIC ile yeniden derlemek
// gerekiyor - ayni is H264 cozucusunu de geri getirir.

#include "group/AudioStreamingPart.h"
#include "group/AudioStreamingPartPersistentDecoder.h"
#include "group/VideoStreamingPart.h"

namespace tgcalls {

// --------------------------------------------- AudioStreamingPart

AudioStreamingPart::AudioStreamingPart(std::vector<uint8_t> && /*data*/,
                                       std::string const & /*container*/,
                                       bool /*isSingleChannel*/) {
    _state = nullptr;
}

AudioStreamingPart::~AudioStreamingPart() = default;

std::map<std::string, int32_t> AudioStreamingPart::getEndpointMapping() const {
    return {};
}

int AudioStreamingPart::getRemainingMilliseconds() const {
    // Sifir: "bu parcada calacak ses kalmadi". Cagiran bunu gecerli bir
    // durum olarak ele aliyor.
    return 0;
}

std::vector<AudioStreamingPart::StreamingPartChannel>
AudioStreamingPart::get10msPerChannel(
    AudioStreamingPartPersistentDecoder & /*persistentDecoder*/) {
    return {};
}

// ------------------------------------- AudioStreamingPartPersistentDecoder

WrappedCodecParameters::WrappedCodecParameters(
    AVCodecParameters const * /*codecParameters*/) {
    _value = nullptr;
}

WrappedCodecParameters::~WrappedCodecParameters() = default;

bool WrappedCodecParameters::isEqual(AVCodecParameters const * /*other*/) {
    return false;
}

AudioStreamingPartPersistentDecoder::AudioStreamingPartPersistentDecoder() {
    _state = nullptr;
}

AudioStreamingPartPersistentDecoder::~AudioStreamingPartPersistentDecoder() =
    default;

int AudioStreamingPartPersistentDecoder::decode(
    AVCodecParameters const * /*codecParameters*/, AVRational /*timeBase*/,
    AVPacket & /*packet*/, AVFrame * /*frame*/) {
    // Negatif: ffmpeg'in "cozulemedi" sozlesmesi.
    return -1;
}

void AudioStreamingPartPersistentDecoder::maybeReset(
    AVCodecParameters const * /*codecParameters*/, AVRational /*timeBase*/) {}

// --------------------------------------------- VideoStreamingPart

VideoStreamingSharedState::VideoStreamingSharedState() { _impl = nullptr; }

VideoStreamingSharedState::~VideoStreamingSharedState() = default;

VideoStreamingPart::VideoStreamingPart(
    std::vector<uint8_t> && /*data*/,
    VideoStreamingPart::ContentType /*contentType*/) {
    _state = nullptr;
}

VideoStreamingPart::~VideoStreamingPart() = default;

absl::optional<VideoStreamingPartFrame>
VideoStreamingPart::getFrameAtRelativeTimestamp(
    VideoStreamingSharedState const * /*sharedState*/, double /*timestamp*/) {
    return absl::nullopt;
}

absl::optional<std::string> VideoStreamingPart::getActiveEndpointId() const {
    return absl::nullopt;
}

bool VideoStreamingPart::hasRemainingFrames() const { return false; }

int VideoStreamingPart::getAudioRemainingMilliseconds() { return 0; }

std::vector<AudioStreamingPart::StreamingPartChannel>
VideoStreamingPart::getAudio10msPerChannel(
    AudioStreamingPartPersistentDecoder & /*persistentDecoder*/) {
    return {};
}

} // namespace tgcalls
