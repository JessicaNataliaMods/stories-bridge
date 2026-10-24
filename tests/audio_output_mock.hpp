#pragma once
#include "sa_audio.hpp"
namespace audio_mock {
inline size_t opens=0,writes=0,resets=0;
inline bool fail_open=false,fail_write=false;
inline bool fail_volume=false,fail_rate=false;
inline bool fail_reset=false,fail_unprepare=false,fail_close=false;
inline bool fail_pause=false,fail_restart=false;inline size_t pauses=0,restarts=0;
inline DWORD support=WAVECAPS_VOLUME|WAVECAPS_LRVOLUME|WAVECAPS_PLAYBACKRATE,last_rate=0;
inline size_t rates=0,volumes=0;
inline size_t writes_at_rate=0;
inline bool fail_position=false;inline DWORD position_type=TIME_SAMPLES,position_samples=0;
inline size_t position_queries=0;inline HWAVEOUT position_handle=nullptr;
inline DWORD last_volume=0;
inline std::vector<char> effects;
inline std::vector<WAVEHDR*> headers;
inline std::vector<std::vector<uint8_t>> pcm_writes;
inline WAVEHDR* last_prepared=nullptr;
inline DWORD_PTR last_callback=0;inline DWORD last_flags=0;inline HANDLE completion_event=nullptr;
inline MMRESULT WINAPI open(LPHWAVEOUT out,UINT,LPCWAVEFORMATEX,DWORD_PTR callback,DWORD_PTR,DWORD flags) {
    last_callback=callback;last_flags=flags;
    ++opens;if(fail_open)return MMSYSERR_ERROR;
    *out=reinterpret_cast<HWAVEOUT>(opens);return MMSYSERR_NOERROR;
}
inline MMRESULT WINAPI prepare(HWAVEOUT,WAVEHDR* header,UINT){last_prepared=header;header->dwFlags|=WHDR_PREPARED;return 0;}
inline MMRESULT WINAPI write(HWAVEOUT,WAVEHDR* header,UINT){effects.push_back('W');++writes;if(fail_write)return MMSYSERR_ERROR;headers.push_back(header);pcm_writes.emplace_back(reinterpret_cast<uint8_t*>(header->lpData),reinterpret_cast<uint8_t*>(header->lpData)+header->dwBufferLength);if(completion_event)SetEvent(completion_event);return 0;}
inline MMRESULT WINAPI reset(HWAVEOUT){++resets;return fail_reset?MMSYSERR_ERROR:0;}
inline MMRESULT WINAPI unprepare(HWAVEOUT,WAVEHDR* header,UINT){if(fail_unprepare)return MMSYSERR_ERROR;header->dwFlags&=~WHDR_PREPARED;return 0;}
inline MMRESULT WINAPI close(HWAVEOUT){return fail_close?MMSYSERR_ERROR:0;}
inline MMRESULT WINAPI pause(HWAVEOUT){++pauses;return fail_pause?MMSYSERR_ERROR:0;}
inline MMRESULT WINAPI restart(HWAVEOUT){++restarts;return fail_restart?MMSYSERR_ERROR:0;}
inline MMRESULT WINAPI volume(HWAVEOUT,DWORD value){effects.push_back('V');++volumes;last_volume=value;return fail_volume?MMSYSERR_ERROR:0;}
inline MMRESULT WINAPI rate(HWAVEOUT,DWORD value){++rates;writes_at_rate=writes;last_rate=value;return fail_rate?MMSYSERR_ERROR:0;}
inline MMRESULT WINAPI id(HWAVEOUT,UINT* value){*value=1;return 0;}
inline MMRESULT WINAPI caps(UINT_PTR,WAVEOUTCAPSA* value,UINT){value->dwSupport=support;return 0;}
inline MMRESULT WINAPI position(HWAVEOUT handle,MMTIME* value,UINT){
    ++position_queries;position_handle=handle;if(fail_position)return MMSYSERR_ERROR;
    value->wType=position_type;value->u.sample=position_samples;return MMSYSERR_NOERROR;
}
}
#define waveOutOpen audio_mock::open
#define waveOutPrepareHeader audio_mock::prepare
#define waveOutWrite audio_mock::write
#define waveOutReset audio_mock::reset
#define waveOutUnprepareHeader audio_mock::unprepare
#define waveOutClose audio_mock::close
#define waveOutPause audio_mock::pause
#define waveOutRestart audio_mock::restart
#define waveOutSetVolume audio_mock::volume
#define waveOutSetPlaybackRate audio_mock::rate
#define waveOutGetID audio_mock::id
#define waveOutGetDevCapsA audio_mock::caps
#define waveOutGetPosition audio_mock::position
