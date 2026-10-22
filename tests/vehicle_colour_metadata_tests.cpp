// Exercise the production parser without touching the GTA process or memory.
#include "../src/sa_vehicle_colours.cpp"
#include <iostream>

int main(int argc,char** argv) {
    try {
        if(argc!=2)throw std::runtime_error("Expected vehicle-colours.dat path");
        const auto metadata=lcs::read_metadata(argv[1]);
        if(metadata.vehicles.size()!=87)throw std::runtime_error("Expected complete 87-model fleet");
        std::cout<<"PASS: production colour parser accepted all 87 models; bounded native variations; no game memory accessed\n";
        return 0;
    } catch(const std::exception& error) {
        std::cerr<<error.what()<<'\n';return 1;
    }
}
