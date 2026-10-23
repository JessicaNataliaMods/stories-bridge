#pragma once
#include <string>
namespace lcs {
// Replace only frontend text call sites. SA owns menu navigation, values,
// hitboxes and native dictionaries; LcsHud=0 leaves these calls untouched.
bool sa_ui_menu_install(std::string& diagnostic);
}
