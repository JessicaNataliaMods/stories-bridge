#include "sa_vehicle_colours.hpp"

#include "lcs/sa_executor.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#if defined(_WIN32) && defined(_MSC_VER) && defined(_M_IX86) && !defined(LCS_VEHICLE_COLOURS_METADATA_ONLY)
#include <windows.h>
#endif

namespace lcs {
namespace {

constexpr size_t kExtendedColourCount = 138;
constexpr size_t kLcsColourCount = 101;
constexpr size_t kMaximumVehicleCount = 87;

struct Rgb { uint8_t r=0,g=0,b=0; };
struct VehicleColours {
    int32_t model=0;
    uint8_t count=0;
    std::array<uint8_t,8> primary{};
    std::array<uint8_t,8> secondary{};
};
struct Metadata {
    std::array<Rgb,kExtendedColourCount> colours{};
    std::array<bool,kExtendedColourCount> has_colour{};
    std::array<int32_t,kLcsColourCount> lcs_to_colour{};
    std::array<bool,kLcsColourCount> has_map{};
    std::vector<VehicleColours> vehicles;
    std::string source;
};

void require(bool condition,const char* message) {
    if(!condition)throw std::runtime_error(message);
}

uint32_t parse_uint(const std::string& token,const char* field) {
    uint32_t value=0;
    const auto result=std::from_chars(token.data(),token.data()+token.size(),value);
    if(token.empty() || result.ec!=std::errc{} || result.ptr!=token.data()+token.size())
        throw std::runtime_error(std::string("Invalid vehicle-colour metadata ")+field);
    return value;
}

std::vector<std::string> split(const std::string& line) {
    std::istringstream input(line);
    std::vector<std::string> fields;
    for(std::string field;input>>field;)fields.push_back(std::move(field));
    return fields;
}

Metadata read_metadata(const std::filesystem::path& path) {
    std::ifstream input(path,std::ios::binary);
    if(!input)throw std::runtime_error("Cannot open vehicle-colour metadata");
    Metadata result;
    constexpr size_t kMaximumMetadataBytes=1024*1024;
    result.source.assign(std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>());
    if(input.bad() || result.source.size()>kMaximumMetadataBytes)
        throw std::runtime_error("Cannot read vehicle-colour metadata safely");

    std::istringstream lines(result.source);
    std::string line;
    size_t line_number=0;
    bool header=false;
    while(std::getline(lines,line)) {
        ++line_number;
        const auto fields=split(line);
        if(fields.empty())continue;
        if(!header) {
            require(fields.size()==2 && fields[0]=="LCS_VEHICLE_COLOURS" && fields[1]=="1",
                "Unsupported vehicle-colour metadata header");
            header=true;
            continue;
        }
        if(fields[0]=="color") {
            require(fields.size()==5,"Malformed vehicle-colour RGB row");
            const auto index=parse_uint(fields[1],"color index");
            require(index<kExtendedColourCount && !result.has_colour[index],"Duplicate or out-of-range vehicle-colour RGB index");
            const auto r=parse_uint(fields[2],"red channel"),g=parse_uint(fields[3],"green channel"),b=parse_uint(fields[4],"blue channel");
            require(r<=255 && g<=255 && b<=255,"Vehicle-colour RGB channel is outside 0..255");
            result.colours[index]={static_cast<uint8_t>(r),static_cast<uint8_t>(g),static_cast<uint8_t>(b)};
            result.has_colour[index]=true;
        } else if(fields[0]=="map") {
            require(fields.size()==3,"Malformed vehicle-colour map row");
            const auto lcs=parse_uint(fields[1],"LCS map index"),colour=parse_uint(fields[2],"mapped colour index");
            require(lcs<kLcsColourCount && colour<kExtendedColourCount && !result.has_map[lcs],
                "Duplicate or out-of-range vehicle-colour map row");
            result.lcs_to_colour[lcs]=static_cast<int32_t>(colour);
            result.has_map[lcs]=true;
        } else if(fields[0]=="car") {
            require(fields.size()>=5,"Malformed vehicle-colour model row");
            const auto model=parse_uint(fields[1],"vehicle model id");
            const auto count=parse_uint(fields[2],"vehicle variation count");
            require(model<20000 && count>=1 && count<=8 && fields.size()==3+2*count,
                "Invalid vehicle-colour model id or variation count");
            VehicleColours vehicle;
            vehicle.model=static_cast<int32_t>(model);
            vehicle.count=static_cast<uint8_t>(count);
            for(size_t i=0;i<count;++i) {
                const auto primary=parse_uint(fields[3+i*2],"primary colour index");
                const auto secondary=parse_uint(fields[4+i*2],"secondary colour index");
                require(primary<kExtendedColourCount && secondary<kExtendedColourCount,
                    "Vehicle model references an unknown colour index");
                vehicle.primary[i]=static_cast<uint8_t>(primary);
                vehicle.secondary[i]=static_cast<uint8_t>(secondary);
            }
            result.vehicles.push_back(vehicle);
        } else {
            (void)line_number;
            throw std::runtime_error("Unknown vehicle-colour metadata row");
        }
    }
    require(header,"Vehicle-colour metadata is empty");
    require(std::all_of(result.has_colour.begin(),result.has_colour.end(),[](bool v){return v;}),
        "Vehicle-colour metadata must define all 138 palette rows");
    require(std::all_of(result.has_map.begin(),result.has_map.end(),[](bool v){return v;}),
        "Vehicle-colour metadata must define all 101 LCS colour mappings");
    require(!result.vehicles.empty() && result.vehicles.size()<=kMaximumVehicleCount,
        "Vehicle-colour metadata has an empty or oversized campaign vehicle set");
    std::sort(result.vehicles.begin(),result.vehicles.end(),[](const auto& a,const auto& b){return a.model<b.model;});
    for(size_t i=1;i<result.vehicles.size();++i)
        require(result.vehicles[i-1].model!=result.vehicles[i].model,"Duplicate vehicle model in colour metadata");
    for(const int32_t default_model:{432,433}) {
        const auto found=std::find_if(result.vehicles.begin(),result.vehicles.end(),[&](const auto& vehicle){return vehicle.model==default_model;});
        require(found!=result.vehicles.end() && found->count==1 &&
            found->primary[0]==result.lcs_to_colour[0] && found->secondary[0]==result.lcs_to_colour[0],
            "Rhino/Barracks must use the explicit CCar default colour pair");
    }
    return result;
}

#if defined(_WIN32) && defined(_MSC_VER) && defined(_M_IX86) && !defined(LCS_VEHICLE_COLOURS_METADATA_ONLY)

constexpr size_t kSaColourCount = 127;
constexpr size_t kNativeColourSlots = 128;
constexpr size_t kPaletteCapacity = 256;

int32_t runtime_colour_index(uint32_t metadata_index) {
    return static_cast<int32_t>(metadata_index<kSaColourCount ? metadata_index : metadata_index+1);
}

std::vector<int32_t> public_map(const Metadata& metadata) {
    std::vector<int32_t> result(kLcsColourCount);
    for(size_t i=0;i<result.size();++i)result[i]=runtime_colour_index(static_cast<uint32_t>(metadata.lcs_to_colour[i]));
    return result;
}

constexpr uintptr_t kVehicleColourTable=0xB4E480;
constexpr uintptr_t kModelInfoTable=0xA9B0C8;
constexpr uintptr_t kPrimaryColoursOffset=0x2B0;
constexpr uintptr_t kSecondaryColoursOffset=0x2B8;
constexpr uintptr_t kVariationCountOffset=0x2D0;

struct Rgba { uint8_t r=0,g=0,b=0,a=0; };
static_assert(sizeof(Rgba)==4,"SA vehicle colour entries must remain four bytes");
__declspec(align(16)) Rgba g_runtime_palette[kPaletteCapacity]{};

struct ColourReference {
    uintptr_t instruction=0;
    uint8_t length=0;
    uint8_t immediate_offset=0;
    uint8_t palette_byte_offset=0;
    std::array<uint8_t,8> original{};
};

constexpr ColourReference kReferences[]={
    {0x447094,7,3,0,{0x8D,0x04,0x85,0x80,0xE4,0xB4,0x00}},
    {0x44B1BE,7,3,0,{0x8B,0x04,0x95,0x80,0xE4,0xB4,0x00}},
    {0x4C838D,7,3,0,{0x8A,0x0C,0xB5,0x80,0xE4,0xB4,0x00}},
    {0x4C8396,7,3,1,{0x8A,0x14,0xB5,0x81,0xE4,0xB4,0x00}},
    {0x4C83A0,7,3,2,{0x8A,0x0C,0xB5,0x82,0xE4,0xB4,0x00}},
    {0x5817C9,7,3,0,{0x8B,0x3C,0x95,0x80,0xE4,0xB4,0x00}},
    {0x582173,7,3,0,{0x8B,0x04,0x85,0x80,0xE4,0xB4,0x00}},
    {0x5B68D4,8,4,1,{0xC7,0x44,0x24,0x10,0x81,0xE4,0xB4,0x00}},
    {0x6A6FF7,7,3,0,{0x8B,0x0C,0x85,0x80,0xE4,0xB4,0x00}},
};

struct PageProtection { uintptr_t address=0; DWORD original=0; };
struct MemoryWrite {
    uintptr_t address=0;
    size_t size=0;
    std::array<uint8_t,16> before{};
    std::array<uint8_t,16> after{};
};
struct InstalledVehicle {
    int32_t model=0;
    uintptr_t address=0;
    uint8_t before_count=0,after_count=0;
    std::array<uint8_t,8> before_primary{},before_secondary{},after_primary{},after_secondary{};
};
struct RuntimeState {
    bool prepared=false;
    bool models_applied=false;
    bool faulted=false;
    std::string metadata_source;
    std::array<Rgb,kExtendedColourCount> expected_colours{};
    std::vector<InstalledVehicle> vehicles;
};
RuntimeState g_state;
std::mutex g_mutex;

bool read_memory(uintptr_t address,void* output,size_t size) {
    SIZE_T actual=0;
    return ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(address),output,size,&actual)!=FALSE && actual==size;
}
bool write_memory(uintptr_t address,const void* input,size_t size) {
    SIZE_T actual=0;
    return WriteProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(address),input,size,&actual)!=FALSE && actual==size;
}
bool bytes_equal(uintptr_t address,const void* expected,size_t size) {
    std::array<uint8_t,16> observed{};
    return size<=observed.size() && read_memory(address,observed.data(),size) && std::memcmp(observed.data(),expected,size)==0;
}
void store_u32(uint8_t* bytes,uint32_t value) {std::memcpy(bytes,&value,sizeof(value));}

bool profile_matches() {
    return GetModuleHandleW(nullptr)==reinterpret_cast<HMODULE>(0x400000) && sa_executor_profile_matches();
}

std::array<uint8_t,8> patched_reference(const ColourReference& reference,uintptr_t palette) {
    auto result=reference.original;
    const auto address=palette+reference.palette_byte_offset;
    require(address<=UINT32_MAX,"Extended vehicle palette is outside the SA address space");
    store_u32(result.data()+reference.immediate_offset,static_cast<uint32_t>(address));
    return result;
}

bool references_are_original() {
    for(const auto& reference:kReferences)
        if(!bytes_equal(reference.instruction,reference.original.data(),reference.length))return false;
    return true;
}
bool references_are_patched() {
    const auto palette=reinterpret_cast<uintptr_t>(g_runtime_palette);
    for(const auto& reference:kReferences) {
        const auto expected=patched_reference(reference,palette);
        if(!bytes_equal(reference.instruction,expected.data(),reference.length))return false;
    }
    return true;
}

bool pin_module() {
    HMODULE module=nullptr;
    return GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,
        reinterpret_cast<LPCWSTR>(&g_runtime_palette[0]),&module)!=FALSE;
}

bool writable_region(uintptr_t address,size_t size) {
    MEMORY_BASIC_INFORMATION info{};
    if(!VirtualQuery(reinterpret_cast<const void*>(address),&info,sizeof(info)) || info.State!=MEM_COMMIT ||
       (info.Protect&PAGE_GUARD) || (info.Protect&PAGE_NOACCESS))return false;
    const DWORD protection=info.Protect&0xFF;
    const bool writable=protection==PAGE_READWRITE || protection==PAGE_WRITECOPY ||
        protection==PAGE_EXECUTE_READWRITE || protection==PAGE_EXECUTE_WRITECOPY;
    return writable && address+size>=address && address+size<=reinterpret_cast<uintptr_t>(info.BaseAddress)+info.RegionSize;
}

std::vector<PageProtection> code_pages() {
    SYSTEM_INFO system{};GetSystemInfo(&system);
    require(system.dwPageSize!=0,"Cannot determine SA code page size");
    std::vector<PageProtection> pages;
    for(const auto& reference:kReferences) {
        MEMORY_BASIC_INFORMATION info{};
        require(VirtualQuery(reinterpret_cast<const void*>(reference.instruction),&info,sizeof(info)) &&
            info.State==MEM_COMMIT && !(info.Protect&PAGE_GUARD),"SA colour reference is not committed executable memory");
        const DWORD protection=info.Protect&0xFF;
        require(protection==PAGE_EXECUTE || protection==PAGE_EXECUTE_READ || protection==PAGE_EXECUTE_READWRITE ||
            protection==PAGE_EXECUTE_WRITECOPY,"SA colour reference is not executable memory");
        const uintptr_t page=reference.instruction&~(static_cast<uintptr_t>(system.dwPageSize)-1);
        if(std::none_of(pages.begin(),pages.end(),[&](const auto& item){return item.address==page;}))
            pages.push_back({page,0});
    }
    return pages;
}

bool make_code_writable(std::vector<PageProtection>& pages) {
    SYSTEM_INFO system{};GetSystemInfo(&system);
    size_t changed=0;
    for(;changed<pages.size();++changed) {
        DWORD prior=0;
        if(!VirtualProtect(reinterpret_cast<void*>(pages[changed].address),system.dwPageSize,PAGE_EXECUTE_READWRITE,&prior))break;
        pages[changed].original=prior;
    }
    if(changed==pages.size())return true;
    bool restored=true;DWORD ignored=0;
    while(changed>0) {
        --changed;
        if(!VirtualProtect(reinterpret_cast<void*>(pages[changed].address),system.dwPageSize,pages[changed].original,&ignored))restored=false;
    }
    if(!restored)g_state.faulted=true;
    return false;
}

bool restore_code_protections(const std::vector<PageProtection>& pages) {
    SYSTEM_INFO system{};GetSystemInfo(&system);
    bool restored=true;DWORD ignored=0;
    for(const auto& page:pages)
        if(!VirtualProtect(reinterpret_cast<void*>(page.address),system.dwPageSize,page.original,&ignored))restored=false;
    return restored;
}

bool reopen_code_pages(const std::vector<PageProtection>& pages) {
    SYSTEM_INFO system{};GetSystemInfo(&system);
    bool opened=true;DWORD ignored=0;
    for(const auto& page:pages)
        if(!VirtualProtect(reinterpret_cast<void*>(page.address),system.dwPageSize,PAGE_EXECUTE_READWRITE,&ignored))opened=false;
    return opened;
}

bool rollback_writes(const std::vector<MemoryWrite>& writes,size_t attempted) {
    bool okay=true;
    while(attempted>0) {
        const auto& write=writes[--attempted];
        std::array<uint8_t,16> current{};
        if(!read_memory(write.address,current.data(),write.size)) {okay=false;continue;}
        if(std::memcmp(current.data(),write.before.data(),write.size)==0)continue;
        if(std::memcmp(current.data(),write.after.data(),write.size)!=0) {okay=false;continue;}
        if(!write_memory(write.address,write.before.data(),write.size))okay=false;
    }
    return okay;
}

bool apply_writes(const std::vector<MemoryWrite>& writes,bool include_code_pages,
                  std::vector<PageProtection>* pages) {
    for(const auto& write:writes) {
        std::array<uint8_t,16> current{};
        if(write.size==0 || write.size>current.size() || !read_memory(write.address,current.data(),write.size) ||
           std::memcmp(current.data(),write.before.data(),write.size)!=0)return false;
        if(!include_code_pages && !writable_region(write.address,write.size))return false;
    }
    if(include_code_pages && (!pages || !make_code_writable(*pages)))return false;
    size_t attempted=0;
    for(;attempted<writes.size();++attempted)
        if(!write_memory(writes[attempted].address,writes[attempted].after.data(),writes[attempted].size)) {
            ++attempted;
            const bool rolled_back=rollback_writes(writes,attempted);
            bool protections_ok=true;
            if(include_code_pages && pages)protections_ok=restore_code_protections(*pages);
            if(!rolled_back || !protections_ok)g_state.faulted=true;
            if(include_code_pages)FlushInstructionCache(GetCurrentProcess(),nullptr,0);
            return false;
        }
    if(include_code_pages && pages && !restore_code_protections(*pages)) {
        const bool reopened=reopen_code_pages(*pages);
        const bool rolled_back=reopened && rollback_writes(writes,writes.size());
        const bool protections_ok=restore_code_protections(*pages);
        if(!rolled_back || !protections_ok)g_state.faulted=true;
        FlushInstructionCache(GetCurrentProcess(),nullptr,0);
        return false;
    }
    if(include_code_pages)FlushInstructionCache(GetCurrentProcess(),nullptr,0);
    return true;
}

MemoryWrite reference_write(const ColourReference& reference,uintptr_t palette) {
    MemoryWrite write;write.address=reference.instruction+reference.immediate_offset;write.size=sizeof(uint32_t);
    std::memcpy(write.before.data(),reference.original.data()+reference.immediate_offset,sizeof(uint32_t));
    const auto address=palette+reference.palette_byte_offset;
    require(address<=UINT32_MAX,"Extended vehicle palette is outside the SA address space");
    store_u32(write.after.data(),static_cast<uint32_t>(address));
    return write;
}

bool same_rgb(const Rgba& rgba,const Rgb& rgb) {return rgba.r==rgb.r && rgba.g==rgb.g && rgba.b==rgb.b;}

std::vector<InstalledVehicle> stage_vehicle_models(const Metadata& metadata,bool first_application) {
    std::vector<InstalledVehicle> result;
    result.reserve(metadata.vehicles.size());
    for(const auto& source:metadata.vehicles) {
        const auto type=sa_model_class(source.model);
        require(type && *type==6,"Campaign colour metadata references a non-vehicle SA model");
        uint32_t model_info=0;
        require(read_memory(kModelInfoTable+static_cast<uintptr_t>(source.model)*sizeof(uint32_t),&model_info,sizeof(model_info)) && model_info,
            "SA vehicle model-info pointer is unavailable");
        const uintptr_t address=model_info;
        require(writable_region(address+kVariationCountOffset,1) && writable_region(address+kPrimaryColoursOffset,8) &&
            writable_region(address+kSecondaryColoursOffset,8),"SA vehicle colour fields are not writable");
        InstalledVehicle item;item.model=source.model;item.address=address;
        if(first_application) {
            require(read_memory(address+kVariationCountOffset,&item.before_count,1) &&
                read_memory(address+kPrimaryColoursOffset,item.before_primary.data(),8) &&
                read_memory(address+kSecondaryColoursOffset,item.before_secondary.data(),8),
                "Cannot read original SA vehicle colour fields");
        }
        item.after_count=source.count;
        if(first_application) {
            item.after_primary=item.before_primary;
            item.after_secondary=item.before_secondary;
        }
        for(size_t i=0;i<source.count;++i) {
            item.after_primary[i]=static_cast<uint8_t>(runtime_colour_index(source.primary[i]));
            item.after_secondary[i]=static_cast<uint8_t>(runtime_colour_index(source.secondary[i]));
        }
        result.push_back(item);
    }
    return result;
}

bool current_vehicle_matches(const InstalledVehicle& expected,bool converted) {
    const uint8_t count=converted?expected.after_count:expected.before_count;
    const auto& primary=converted?expected.after_primary:expected.before_primary;
    const auto& secondary=converted?expected.after_secondary:expected.before_secondary;
    uint8_t actual_count=0;std::array<uint8_t,8> actual_primary{},actual_secondary{};
    return read_memory(expected.address+kVariationCountOffset,&actual_count,1) &&
        read_memory(expected.address+kPrimaryColoursOffset,actual_primary.data(),8) &&
        read_memory(expected.address+kSecondaryColoursOffset,actual_secondary.data(),8) &&
        actual_count==count && actual_primary==primary && actual_secondary==secondary;
}

void append_model_writes(const InstalledVehicle& model,std::vector<MemoryWrite>& writes) {
    if(current_vehicle_matches(model,true))return;
    require(current_vehicle_matches(model,false),"SA vehicle colour fields changed outside the campaign consumer");
    MemoryWrite primary;primary.address=model.address+kPrimaryColoursOffset;primary.size=8;
    std::memcpy(primary.before.data(),model.before_primary.data(),8);std::memcpy(primary.after.data(),model.after_primary.data(),8);
    MemoryWrite secondary;secondary.address=model.address+kSecondaryColoursOffset;secondary.size=8;
    std::memcpy(secondary.before.data(),model.before_secondary.data(),8);std::memcpy(secondary.after.data(),model.after_secondary.data(),8);
    MemoryWrite count;count.address=model.address+kVariationCountOffset;count.size=1;
    count.before[0]=model.before_count;count.after[0]=model.after_count;
    writes.push_back(primary);writes.push_back(secondary);writes.push_back(count);
}

void initialize_palette(const Metadata& metadata) {
    std::array<Rgba,kNativeColourSlots> previous{};
    require(read_memory(kVehicleColourTable,previous.data(),sizeof(previous)),"SA vehicle colour table is unreadable");
    const bool already_loaded=std::any_of(previous.begin(),previous.begin()+kSaColourCount,
        [](const auto& colour){return colour.r || colour.g || colour.b || colour.a;});
    if(already_loaded)
        for(size_t i=0;i<kSaColourCount;++i)
            require(same_rgb(previous[i],metadata.colours[i]),
                "Late palette preparation would overwrite a foreign SA vehicle colour table");
    std::array<Rgba,kPaletteCapacity> staged{};
    std::copy(previous.begin(),previous.end(),staged.begin());
    for(size_t i=0;i<kSaColourCount;++i)
        staged[i]={metadata.colours[i].r,metadata.colours[i].g,metadata.colours[i].b,255};
    for(size_t i=kSaColourCount;i<kExtendedColourCount;++i) {
        for(size_t j=0;j<kSaColourCount;++j)
            require(metadata.colours[i].r!=metadata.colours[j].r || metadata.colours[i].g!=metadata.colours[j].g ||
                metadata.colours[i].b!=metadata.colours[j].b,"LCS-only colour duplicates the SA palette and cannot use an appended slot");
        for(size_t j=kSaColourCount;j<i;++j)
            require(metadata.colours[i].r!=metadata.colours[j].r || metadata.colours[i].g!=metadata.colours[j].g ||
                metadata.colours[i].b!=metadata.colours[j].b,"Duplicate LCS-only colour in extended palette");
        staged[i+1]={metadata.colours[i].r,metadata.colours[i].g,metadata.colours[i].b,255};
    }
    std::copy(staged.begin(),staged.end(),g_runtime_palette);
    g_state.expected_colours=metadata.colours;
}

void validate_runtime_palette() {
    for(size_t i=0;i<kSaColourCount;++i)
        require(same_rgb(g_runtime_palette[i],g_state.expected_colours[i]),
            "Loaded SA vehicle palette differs from the campaign's verified 127-colour prefix");
    for(size_t i=kSaColourCount;i<kExtendedColourCount;++i)
        require(same_rgb(g_runtime_palette[i+1],g_state.expected_colours[i]),
            "Extended SA vehicle palette does not contain the exact campaign RGB rows");
}

#endif // supported 32-bit retail SA backend
}

void sa_prepare_vehicle_colours(const std::filesystem::path& metadata_path) {
    const Metadata metadata=read_metadata(metadata_path);
#if defined(_WIN32) && defined(_MSC_VER) && defined(_M_IX86) && !defined(LCS_VEHICLE_COLOURS_METADATA_ONLY)
    std::lock_guard<std::mutex> lock(g_mutex);
    require(!g_state.faulted,"Vehicle-colour consumer is faulted after an incomplete memory transaction");
    require(profile_matches(),"Vehicle-colour consumer requires the verified retail SA executable profile");
    if(g_state.prepared) {
        require(g_state.metadata_source==metadata.source,"Vehicle-colour metadata changed after palette publication");
        require(references_are_patched(),"A foreign modification changed a published SA vehicle-colour reference");
        return;
    }
    require(references_are_original(),"SA vehicle-colour references do not match the exact retail instructions");
    initialize_palette(metadata);
    require(pin_module(),"Cannot pin the vehicle-colour palette module before publishing its address");
    const auto palette=reinterpret_cast<uintptr_t>(g_runtime_palette);
    std::vector<MemoryWrite> writes;
    writes.reserve(std::size(kReferences));
    for(const auto& reference:kReferences)writes.push_back(reference_write(reference,palette));
    auto pages=code_pages();
    if(!apply_writes(writes,true,&pages))
        throw std::runtime_error(g_state.faulted?"SA vehicle-colour reference transaction could not be rolled back":"Cannot relocate SA vehicle-colour references safely");
    if(!references_are_patched()) {
        g_state.faulted=true;
        throw std::runtime_error("SA vehicle-colour references failed post-write verification");
    }
    g_state.metadata_source=metadata.source;
    g_state.prepared=true;
#else
    (void)metadata;
    throw std::runtime_error("SA vehicle-colour consumer requires 32-bit retail Windows SA");
#endif
}

std::vector<int32_t> sa_load_vehicle_colours(const std::filesystem::path& metadata_path) {
    const Metadata metadata=read_metadata(metadata_path);
#if defined(_WIN32) && defined(_MSC_VER) && defined(_M_IX86) && !defined(LCS_VEHICLE_COLOURS_METADATA_ONLY)
    std::lock_guard<std::mutex> lock(g_mutex);
    require(!g_state.faulted,"Vehicle-colour consumer is faulted after an incomplete memory transaction");
    require(g_state.prepared && g_state.metadata_source==metadata.source,
        "Vehicle-colour palette must be prepared before SA initialization with the same metadata");
    require(profile_matches() && references_are_patched(),"SA vehicle-colour references are foreign or the executable profile changed");
    validate_runtime_palette();

    const bool first_application=!g_state.models_applied;
    auto staged=stage_vehicle_models(metadata,first_application);
    std::vector<MemoryWrite> writes;
    writes.reserve(staged.size()*3);
    if(first_application) {
        for(const auto& model:staged) {
            MemoryWrite primary;primary.address=model.address+kPrimaryColoursOffset;primary.size=8;
            std::memcpy(primary.before.data(),model.before_primary.data(),8);std::memcpy(primary.after.data(),model.after_primary.data(),8);
            MemoryWrite secondary;secondary.address=model.address+kSecondaryColoursOffset;secondary.size=8;
            std::memcpy(secondary.before.data(),model.before_secondary.data(),8);std::memcpy(secondary.after.data(),model.after_secondary.data(),8);
            MemoryWrite count;count.address=model.address+kVariationCountOffset;count.size=1;
            count.before[0]=model.before_count;count.after[0]=model.after_count;
            writes.push_back(primary);writes.push_back(secondary);writes.push_back(count);
        }
    } else {
        require(staged.size()==g_state.vehicles.size(),"Prepared vehicle-colour model set changed");
        for(size_t i=0;i<staged.size();++i) {
            require(staged[i].model==g_state.vehicles[i].model && staged[i].address==g_state.vehicles[i].address,
                "SA vehicle model-info identity changed after campaign colours were prepared");
            const auto& source=metadata.vehicles[i];
            require(g_state.vehicles[i].after_count==source.count,
                "Prepared vehicle colour variation count differs from campaign metadata");
            for(size_t colour=0;colour<source.count;++colour)
                require(g_state.vehicles[i].after_primary[colour]==runtime_colour_index(source.primary[colour]) &&
                    g_state.vehicles[i].after_secondary[colour]==runtime_colour_index(source.secondary[colour]),
                    "Prepared vehicle colour pair differs from campaign metadata");
            staged[i].before_count=g_state.vehicles[i].before_count;
            staged[i].before_primary=g_state.vehicles[i].before_primary;
            staged[i].before_secondary=g_state.vehicles[i].before_secondary;
            staged[i].after_primary=g_state.vehicles[i].after_primary;
            staged[i].after_secondary=g_state.vehicles[i].after_secondary;
            append_model_writes(staged[i],writes);
        }
    }
    if(!writes.empty() && !apply_writes(writes,false,nullptr))
        throw std::runtime_error(g_state.faulted?"Vehicle colour model transaction could not be rolled back":"SA vehicle model colour fields are unreadable, protected, or foreign");
    if(first_application)g_state.vehicles=std::move(staged);
    g_state.models_applied=true;
    for(const auto& model:g_state.vehicles) {
        if(!current_vehicle_matches(model,true)) {
            g_state.faulted=true;
            throw std::runtime_error("SA vehicle colour model failed post-write verification");
        }
    }
    return public_map(metadata);
#else
    (void)metadata;
    throw std::runtime_error("SA vehicle-colour consumer requires 32-bit retail Windows SA");
#endif
}

}
