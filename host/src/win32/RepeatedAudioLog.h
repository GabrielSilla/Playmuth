#pragma once
#include <atomic>
#include <cstdint>
#include <string_view>

namespace azimuth::win32 {
// Only this exact diagnostic is coalesced. Other audio errors and stdout
// remain intact; the first occurrence after each report is still logged.
class RepeatedAudioLog {
    std::atomic<uint64_t> occurrences{0};
    static bool matches(std::string_view line){
        constexpr std::string_view prefix="[xma2 @ ";
        constexpr std::string_view suffix="] Could not update timestamps for skipped samples.";
        if(line.size()<=prefix.size()+suffix.size()||line.substr(0,prefix.size())!=prefix||
           line.substr(line.size()-suffix.size())!=suffix)return false;
        auto address=line.substr(prefix.size(),line.size()-prefix.size()-suffix.size());
        if(address.size()>32)return false;
        for(char c:address)if(!((c>='0'&&c<='9')||(c>='a'&&c<='f')||(c>='A'&&c<='F')))return false;
        return true;
    }
public:
    struct Counts {uint64_t occurrences,suppressed;};
    bool accept(std::string_view line){
        return !matches(line)||occurrences.fetch_add(1,std::memory_order_relaxed)==0;
    }
    Counts take(){
        auto count=occurrences.exchange(0,std::memory_order_relaxed);
        return {count,count?count-1:0};
    }
};
}
