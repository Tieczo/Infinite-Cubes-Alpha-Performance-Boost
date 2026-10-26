// The part of XAudio2 2.7 the alpha uses (platform/XAudio2_7.h on Windows), on FAudio for the native
// Linux build. FAudio is a reimplementation of XAudio2 whose structures match XAudio2's, so the
// interfaces here are thin classes that forward to its C functions.
#pragma once
#include <windows.h>
#include <FAudio.h>

#define XAUDIO2_COMMIT_NOW FAUDIO_COMMIT_NOW
#define XAUDIO2_DEFAULT_CHANNELS FAUDIO_DEFAULT_CHANNELS
#define XAUDIO2_DEFAULT_SAMPLERATE FAUDIO_DEFAULT_SAMPLERATE
#define XAUDIO2_DEFAULT_FREQ_RATIO FAUDIO_DEFAULT_FREQ_RATIO
#define XAUDIO2_DEFAULT_PROCESSOR FAUDIO_DEFAULT_PROCESSOR
#define XAUDIO2_END_OF_STREAM FAUDIO_END_OF_STREAM
#define WAVE_FORMAT_PCM FAUDIO_FORMAT_PCM

typedef FAudioWaveFormatEx WAVEFORMATEX;
typedef FAudioWaveFormatExtensible WAVEFORMATEXTENSIBLE;
typedef FAudioBuffer XAUDIO2_BUFFER;
typedef FAudioVoiceState XAUDIO2_VOICE_STATE;

#define COINIT_MULTITHREADED 0
inline HRESULT CoInitializeEx(void *, DWORD) { return S_OK; }

struct IXAudio2MasteringVoice {
    FAudioMasteringVoice *voice;
    void DestroyVoice()
    {
        FAudioVoice_DestroyVoice(voice);
        delete this;
    }
};

struct IXAudio2SourceVoice {
    FAudioSourceVoice *voice;
    HRESULT Start(UINT32 flags = 0, UINT32 operationSet = XAUDIO2_COMMIT_NOW)
    {
        return FAudioSourceVoice_Start(voice, flags, operationSet);
    }
    HRESULT Stop(UINT32 flags = 0, UINT32 operationSet = XAUDIO2_COMMIT_NOW)
    {
        return FAudioSourceVoice_Stop(voice, flags, operationSet);
    }
    HRESULT SubmitSourceBuffer(const XAUDIO2_BUFFER *buffer)
    {
        return FAudioSourceVoice_SubmitSourceBuffer(voice, buffer, nullptr);
    }
    HRESULT SetVolume(float volume, UINT32 operationSet = XAUDIO2_COMMIT_NOW)
    {
        return FAudioVoice_SetVolume(voice, volume, operationSet);
    }
    HRESULT SetFrequencyRatio(float ratio, UINT32 operationSet = XAUDIO2_COMMIT_NOW)
    {
        return FAudioSourceVoice_SetFrequencyRatio(voice, ratio, operationSet);
    }
    void GetState(XAUDIO2_VOICE_STATE *state) { FAudioSourceVoice_GetState(voice, state, 0); }
    void DestroyVoice()
    {
        FAudioVoice_DestroyVoice(voice);
        delete this;
    }
};

struct IXAudio2 {
    FAudio *audio;
    HRESULT CreateMasteringVoice(IXAudio2MasteringVoice **result, UINT32 channels = XAUDIO2_DEFAULT_CHANNELS,
                                 UINT32 sampleRate = XAUDIO2_DEFAULT_SAMPLERATE)
    {
        FAudioMasteringVoice *voice;
        HRESULT status = FAudio_CreateMasteringVoice(audio, &voice, channels, sampleRate, 0, 0, nullptr);
        if (FAILED(status))
            return status;
        *result = new IXAudio2MasteringVoice{voice};
        return S_OK;
    }
    HRESULT CreateSourceVoice(IXAudio2SourceVoice **result, const WAVEFORMATEX *format, UINT32 flags = 0,
                              float maxFrequencyRatio = XAUDIO2_DEFAULT_FREQ_RATIO)
    {
        FAudioSourceVoice *voice;
        HRESULT status =
            FAudio_CreateSourceVoice(audio, &voice, format, flags, maxFrequencyRatio, nullptr, nullptr, nullptr);
        if (FAILED(status)) {
            *result = nullptr;
            return status;
        }
        *result = new IXAudio2SourceVoice{voice};
        return S_OK;
    }
    ULONG Release()
    {
        ULONG count = FAudio_Release(audio);
        delete this;
        return count;
    }
};

inline HRESULT XAudio2Create(IXAudio2 **result, UINT32 flags = 0, UINT32 processor = XAUDIO2_DEFAULT_PROCESSOR)
{
    FAudio *audio;
    HRESULT status = FAudioCreate(&audio, flags, processor);
    if (FAILED(status))
        return status;
    *result = new IXAudio2{audio};
    return S_OK;
}
