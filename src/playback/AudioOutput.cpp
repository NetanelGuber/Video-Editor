#include "AudioOutput.h"
#include <Windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <wrl/client.h>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace editor::playback {
using Microsoft::WRL::ComPtr;
AudioOutput::AudioOutput(AudioMixer& decoder, qint64 startUs, qint64 endUs, double gain)
    : decoder_(decoder), startUs_(startUs), endUs_(endUs), gain_(gain), position_(startUs) {}
AudioOutput::~AudioOutput() { cancel(); wait(); }
void AudioOutput::cancel() { requestInterruption(); decoder_.cancel(); }
QJsonObject AudioOutput::stats() const {
    QMutexLocker lock(&mutex_);
    return {{"backend", "WASAPI shared / 48 kHz float stereo"}, {"available", available()}, {"error", error_},
        {"underruns", underruns_.load()}, {"positionUs", positionUs()}};
}
void AudioOutput::run() {
    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(com)) { QMutexLocker lock(&mutex_); error_ = "Cannot initialize audio COM"; ready_ = true; return; }
    {
        ComPtr<IMMDeviceEnumerator> devices; ComPtr<IMMDevice> device;
        ComPtr<IAudioClient> client; ComPtr<IAudioRenderClient> render;
        auto check = [&](HRESULT code, const char* operation) {
            if (SUCCEEDED(code)) return true;
            QMutexLocker lock(&mutex_); error_ = QString("%1 failed (0x%2); silent wall-clock fallback").arg(operation).arg(static_cast<quint32>(code), 8, 16, QLatin1Char('0'));
            return false;
        };
        auto play = [&]() {
            if (!check(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&devices)), "Find audio devices") ||
                !check(devices->GetDefaultAudioEndpoint(eRender, eConsole, &device), "Open default output") ||
                !check(device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(client.GetAddressOf())), "Create audio client")) return;
            WAVEFORMATEX format{}; format.wFormatTag = WAVE_FORMAT_IEEE_FLOAT; format.nChannels = 2;
            format.nSamplesPerSec = 48000; format.wBitsPerSample = 32; format.nBlockAlign = 8; format.nAvgBytesPerSec = 384000;
            if (!check(client->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,
                1000000, 0, &format, nullptr), "Initialize shared audio") ||
                !check(client->GetService(IID_PPV_ARGS(&render)), "Open audio render service")) return;
            UINT32 capacity = 0; if (!check(client->GetBufferSize(&capacity), "Get audio buffer size")) return;
            if (capacity > 48000) { QMutexLocker lock(&mutex_); error_ = "Audio device buffer exceeds one-second limit"; return; }
            qint64 submitted = 0; bool started = false, wasEmpty = false;
            const auto firstSample = project::scaleTime(startUs_, 48000, 1000000, project::Rounding::Nearest).value_or(0);
            const auto totalSamples = project::scaleTime(endUs_, 48000, 1000000, project::Rounding::Nearest).value_or(0) - firstSample;
            AudioBlock block; bool hasBlock = false; int offset = 0;
            available_ = true;
            while (!isInterruptionRequested()) {
                UINT32 padding = 0;
                if (!check(client->GetCurrentPadding(&padding), "Read audio clock")) break;
                position_ = padding == 0 && submitted >= totalSamples ? endUs_ :
                    std::min(endUs_, startUs_ + std::max<qint64>(0, submitted - padding) * 1000000 / 48000);
                if (playing_ && !started && ready_) { if (!check(client->Start(), "Start audio")) break; started = true; }
                if (!playing_ && started) { if (!check(client->Stop(), "Pause audio")) break; started = false; }
                if (started && padding == 0 && submitted > 0 && position_ < endUs_) {
                    if (!wasEmpty) ++underruns_; wasEmpty = true;
                } else wasEmpty = false;
                UINT32 free = capacity - std::min(capacity, padding);
                if (free == 0 || submitted >= totalSamples) { ready_ = true; msleep(3); continue; }
                if (!hasBlock) { hasBlock = decoder_.takeAudio(block); offset = 0; }
                if (!hasBlock && !decoder_.done()) { msleep(3); continue; }
                const qint64 cursor = startUs_ + submitted * 1000000 / 48000;
                bool silence = !hasBlock;
                qint64 length = std::min<qint64>(free, totalSamples - submitted);
                if (length <= 0) { ready_ = true; msleep(3); continue; }
                if (hasBlock) {
                    const qint64 blockCursor = block.ptsUs + offset * 1000000LL / 48000;
                    const auto delta = (blockCursor - cursor) * 48000 / 1000000;
                    if (delta > 1) { silence = true; length = std::min(length, delta); }
                    else {
                        if (delta < -1) offset += static_cast<int>(std::min<qint64>(-delta, block.samples.size() / 2 - offset));
                        length = std::min<qint64>(length, block.samples.size() / 2 - offset);
                        if (length == 0) { hasBlock = false; continue; }
                    }
                }
                BYTE* destination = nullptr;
                if (!check(render->GetBuffer(static_cast<UINT32>(length), &destination), "Write audio buffer")) break;
                if (!silence) {
                    auto* floats = reinterpret_cast<float*>(destination);
                    for (qint64 n = 0; n < length * 2; ++n) {
                        const auto value = block.samples[offset * 2 + n] * gain_;
                        floats[n] = std::isfinite(value) ? static_cast<float>(std::clamp(value, -1.0, 1.0)) : 0.0f;
                    }
                    offset += static_cast<int>(length); if (offset * 2 >= block.samples.size()) hasBlock = false;
                }
                if (!check(render->ReleaseBuffer(static_cast<UINT32>(length), silence ? AUDCLNT_BUFFERFLAGS_SILENT : 0), "Submit audio")) break;
                submitted += length;
                if (submitted >= capacity / 2 || decoder_.done()) ready_ = true;
                msleep(1);
            }
            client->Stop(); available_ = false;
        };
        play();
    }
    ready_ = true; CoUninitialize();
}
}
