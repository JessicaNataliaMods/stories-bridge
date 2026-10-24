#pragma once
#include <algorithm>
#include <filesystem>
#include <stdexcept>
#include <string>

namespace lcs {
struct UserFilesOptions {
    bool portable = true;
    std::wstring folder_name = L"GTA Liberty City Stories User Files";
};
struct UserFilesLayout {
    std::filesystem::path root, tracks, gallery;
};

inline void validate_user_files_folder(const std::wstring& name) {
    if(name.empty() || name==L"." || name==L".." || name.back()==L'.' || name.back()==L' ' ||
       std::any_of(name.begin(),name.end(),[](wchar_t c){return c<32 || std::wstring(L"<>:\"/\\|?*").find(c)!=std::wstring::npos;}))
        throw std::runtime_error("UserFilesFolder must be one valid Windows folder name");
    auto stem=name.substr(0,name.find(L'.'));
    std::transform(stem.begin(),stem.end(),stem.begin(),[](wchar_t c){return c>=L'a' && c<=L'z'?c-32:c;});
    if(stem==L"CON" || stem==L"PRN" || stem==L"AUX" || stem==L"NUL" ||
       (stem.size()==4 && (stem.substr(0,3)==L"COM" || stem.substr(0,3)==L"LPT") && stem[3]>=L'1' && stem[3]<=L'9'))
        throw std::runtime_error("UserFilesFolder cannot use a reserved Windows device name");
}

inline UserFilesLayout user_files_layout(const std::filesystem::path& executable_file,
                                        const std::filesystem::path& documents,
                                        const UserFilesOptions& options={}) {
    validate_user_files_folder(options.folder_name);
    if(!executable_file.is_absolute() || executable_file.filename().empty())
        throw std::runtime_error("User files require the absolute game executable path");
    if(!options.portable && !documents.is_absolute())
        throw std::runtime_error("Documents user files require the Windows Documents directory");
    const auto root=((options.portable?executable_file.parent_path():documents)/options.folder_name).lexically_normal();
    return {root, root/L"User Tracks", root/L"Gallery"};
}
} // namespace lcs
