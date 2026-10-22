#include "lcs/lcs_radio.hpp"
#include "lcs/cutscene_audio.hpp"
#include <fstream>
#include <iostream>
#include <set>
#include <stdexcept>

namespace {
void check(bool value,const std::string& message){if(!value)throw std::runtime_error(message);}
}
// argv[1]: the original PS2 AUDIO folder (MUSIC/*.VB are read, never played).
int main(int argc,char** argv){try {
    check(argc==2,"Expected the original PS2 AUDIO folder");
    // Every LCS station is reachable from exactly the SA slot SET_RADIO_CHANNEL
    // tunes, and that slot reports the same station back.
    std::set<uint32_t> reached;
    for(int32_t raw=0;raw<=13;++raw)if(const auto station=lcs::lcs_station_for_sa(raw))reached.insert(*station);
    check(reached.size()==lcs::lcs_radio_station_count,"Every LCS station has an SA slot");
    for(int32_t station=0;station<int32_t(lcs::lcs_radio_station_count);++station) {
        const auto raw=lcs::sa_station_for_lcs(station);
        check(lcs::lcs_station_for_sa(raw)==uint32_t(station) && lcs::lcs_radio_channel_for_sa(raw)==station,"SET/GET_RADIO_CHANNEL round trip");
    }
    check(lcs::sa_station_for_lcs(lcs::lcs_radio_off)==13 && lcs::lcs_radio_channel_for_sa(13)==lcs::lcs_radio_off &&
          lcs::lcs_radio_channel_for_sa(12)==lcs::lcs_radio_off && lcs::lcs_radio_channel_for_sa(0)==-1 &&
          !lcs::lcs_station_for_sa(12) && !lcs::lcs_station_for_sa(13),"User tracks, scanner and off stay native");
    // GetTrackStartPos: elapsed time capped at 270 s, wrapped to the length.
    lcs::LcsRadioTrack track{600000,590000,1000};
    // Retail wraps only past the length; the block seek maps the boundary to 0.
    check(lcs::lcs_radio_start_position(track,11000)==600000,"Exact length stays at the boundary as in retail");
    track={600000,10000,1000};
    check(lcs::lcs_radio_start_position(track,1000000)==280000,"Elapsed listening gap is capped at 270 s");
    track={600000,10000,5000};
    check(lcs::lcs_radio_start_position(track,1000)==10000 && track.last_check_ms==1000,"A timer behind the check only resets it");
    check(lcs::lcs_radio_length_ms(0x4000)==448 && lcs::lcs_radio_block_offset(0x40000,0)==0 &&
          lcs::lcs_radio_block_offset(0x40000,1000)==0x8000 && lcs::lcs_radio_block_offset(0x40000,999999)==0,"Retail duration and block seek");
    const std::filesystem::path audio(argv[1]);
    for(const auto* name:lcs::lcs_radio_files) {
        const auto size=std::filesystem::file_size(audio/"MUSIC"/(std::string(name)+".VB"));
        check(size>=0x4000 && size%0x4000==0,std::string("Station stream size: ")+name);
    }
    // The disk stream decodes exactly like the in-memory CITY stream.
    const auto city=audio/"MUSIC"/"CITY.VB";
    std::ifstream in(city,std::ios::binary|std::ios::ate);std::vector<uint8_t> bytes(size_t(in.tellg()));
    in.seekg(0);in.read(reinterpret_cast<char*>(bytes.data()),std::streamsize(bytes.size()));
    for(const uint64_t offset:{uint64_t(0),uint64_t(0x4000)*37}) {
        lcs::Ps2MusicStream memory(bytes,size_t(offset));lcs::Ps2FileMusicStream file(city,offset);
        for(int block=0;block<3;++block)check(memory.next_block().pcm==file.next_block().pcm,"Disk stream decode differs");
    }
    std::cout<<"PASS: 10 LCS stations mapped to SA slots, retail position rule, disk stream decode; gameExecuted=false\n";
    return 0;
} catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
