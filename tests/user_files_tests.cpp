#include "lcs/user_files.hpp"
#include <iostream>

int main() {try {
    const auto game=std::filesystem::absolute("synthetic-game/gta_sa.exe");
    const auto docs=std::filesystem::absolute("synthetic-documents");
    lcs::UserFilesOptions options;
    auto portable=lcs::user_files_layout(game,{},options);
    if(portable.root!=game.parent_path()/L"GTA Liberty City Stories User Files" ||
       portable.tracks!=portable.root/L"User Tracks" || portable.gallery!=portable.root/L"Gallery")
        throw std::runtime_error("Portable files must follow the game executable, including tracks/gallery");
    options.portable=false;
    auto documents=lcs::user_files_layout(game,docs,options);
    if(documents.root!=docs/L"GTA Liberty City Stories User Files" || documents.root==docs/L"GTA San Andreas User Files")
        throw std::runtime_error("Documents mode must use its own folder");
    options.folder_name=L"My LCS Saves";
    if(lcs::user_files_layout(game,docs,options).root!=docs/L"My LCS Saves")
        throw std::runtime_error("Custom folder name was ignored");
    for(const auto* invalid:{L"",L"..",L"../escape",L"child\\escape",L"C:\\elsewhere",L"name.",L"name ",L"NUL",L"COM1.txt",L"bad:name"}) {
        options.folder_name=invalid;
        bool rejected=false;
        try{lcs::user_files_layout(game,docs,options);}catch(const std::exception&){rejected=true;}
        if(!rejected)throw std::runtime_error("Unsafe folder name accepted");
    }
    options.folder_name=L"Valid";
    bool rejected=false;
    try{lcs::user_files_layout(game,{},options);}catch(const std::exception&){rejected=true;}
    if(!rejected)throw std::runtime_error("Missing Documents folder silently accepted");
    std::cout<<"PASS: portable/Documents roots, exclusive/custom names, tracks/gallery and invalid paths; no files created\n";
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
