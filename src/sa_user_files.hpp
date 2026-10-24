#pragma once

#include <string>

#include "lcs/user_files.hpp"

namespace lcs {

// Install only the early RenderWare event-call hook. Directory creation and
// all other file-system work are deferred until that event is dispatched.
bool sa_user_files_install(const UserFilesOptions& options, std::string& error) noexcept;

} // namespace lcs
