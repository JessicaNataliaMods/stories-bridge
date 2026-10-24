#include "sa_user_files.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <exception>
#include <filesystem>
#include <limits>
#include <string>
#include <windows.h>
#include <shlobj.h>

namespace lcs {
namespace {

// San Andreas 1.0 US interfaces. The call-site hook is installed early, while
// GetUserFilesDir itself is redirected only after the private directory layout
// has been created and checked from the game-thread event callback.
constexpr uintptr_t get_user_files_dir = 0x744FB0;
constexpr uintptr_t event_handler_call = 0x748739;
constexpr uintptr_t tracks_directory_buffer = 0xC92168;
constexpr uintptr_t gallery_directory_buffer = 0xC92268;
constexpr size_t directory_buffer_size = 256;
constexpr size_t get_user_files_prefix_size = 8;

constexpr std::array<uint8_t, get_user_files_prefix_size> get_user_files_prefix{{
    0x83, 0xEC, 0x0C, 0x8D, 0x44, 0x24, 0x00, 0x50
}};

using EventHandler = BOOL(__cdecl*)(int32_t event, void* parameter);
UserFilesOptions configured_options{};
bool install_complete = false;
bool install_failed_uncertainly = false;
uintptr_t original_event_handler = 0;
volatile LONG layout_state = 0; // 0 pending, 1 preparing, 2 ready, -1 failed.
std::array<char, directory_buffer_size> root_directory_ansi{};
std::array<char, directory_buffer_size> tracks_directory_ansi{};
std::array<char, directory_buffer_size> gallery_directory_ansi{};

void assign_error(std::string& out, const char* message) noexcept {
    try {
        out = message ? message : "Unknown user-files installation error";
    } catch (...) {
        out.clear();
    }
}

bool set_error(std::string& out, const char* message) noexcept {
    assign_error(out, message);
    return false;
}

bool set_error(std::string& out, const std::string& message) noexcept {
    try {
        out = message;
    } catch (...) {
        out.clear();
    }
    return false;
}

bool executable_page(uintptr_t address, HMODULE expected_module) noexcept {
    MEMORY_BASIC_INFORMATION info{};
    if (!VirtualQuery(reinterpret_cast<const void*>(address), &info, sizeof(info)) ||
        info.State != MEM_COMMIT || info.AllocationBase != expected_module ||
        (info.Protect & (PAGE_GUARD | PAGE_NOACCESS))) return false;
    const DWORD protection = info.Protect & 0xFF;
    return protection == PAGE_EXECUTE || protection == PAGE_EXECUTE_READ ||
           protection == PAGE_EXECUTE_READWRITE || protection == PAGE_EXECUTE_WRITECOPY;
}

bool read_relative_call(uintptr_t site, uintptr_t& target, std::string& error) noexcept {
    std::array<uint8_t, 5> bytes{};
    SIZE_T received = 0;
    if (!ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(site),
                           bytes.data(), bytes.size(), &received) || received != bytes.size())
        return set_error(error, "The early RsEventHandler CALL site is unreadable");
    if (bytes[0] != 0xE8)
        return set_error(error, "The early RsEventHandler site is not a CALL; another hook may own it");
    int32_t displacement = 0;
    std::memcpy(&displacement, bytes.data() + 1, sizeof(displacement));
    const int64_t resolved = int64_t(site) + 5 + displacement;
    if (resolved <= 0 || uint64_t(resolved) > std::numeric_limits<uintptr_t>::max())
        return set_error(error, "The early RsEventHandler CALL target is invalid");
    target = static_cast<uintptr_t>(resolved);
    return true;
}

bool patch_relative_call(uintptr_t site, uintptr_t replacement, uintptr_t original,
                         std::string& error) noexcept {
    auto* code = reinterpret_cast<uint8_t*>(site);
    int64_t relative = int64_t(replacement) - int64_t(site + 5);
    if (relative < std::numeric_limits<int32_t>::min() || relative > std::numeric_limits<int32_t>::max())
        return set_error(error, "The user-files event wrapper is outside the CALL displacement range");

    DWORD previous_protection = 0;
    if (!VirtualProtect(code, 5, PAGE_EXECUTE_READWRITE, &previous_protection))
        return set_error(error, "Cannot make the early RsEventHandler CALL writable");

    int32_t displacement = static_cast<int32_t>(relative);
    std::memcpy(code + 1, &displacement, sizeof(displacement));
    const bool flushed = FlushInstructionCache(GetCurrentProcess(), code, 5) != 0;
    DWORD ignored = 0;
    const bool restored = VirtualProtect(code, 5, previous_protection, &ignored) != 0;
    if (flushed && restored) return true;

    // A failed protection restore is treated as an uncertain write. Restore the
    // previous CALL target before returning, and poison installation if that
    // rollback cannot be verified.
    DWORD writable = 0;
    const bool made_writable = VirtualProtect(code, 5, PAGE_EXECUTE_READWRITE, &writable) != 0;
    if (made_writable) {
        const int64_t original_relative = int64_t(original) - int64_t(site + 5);
        if (original_relative >= std::numeric_limits<int32_t>::min() &&
            original_relative <= std::numeric_limits<int32_t>::max()) {
            const int32_t original_displacement = static_cast<int32_t>(original_relative);
            std::memcpy(code + 1, &original_displacement, sizeof(original_displacement));
            FlushInstructionCache(GetCurrentProcess(), code, 5);
        }
        DWORD ignored_restore = 0;
        if (!VirtualProtect(code, 5, previous_protection, &ignored_restore))
            install_failed_uncertainly = true;
    } else {
        install_failed_uncertainly = true;
    }
    if (install_failed_uncertainly)
        return set_error(error, "The event-hook write could not be safely rolled back; do not retry this process");
    return set_error(error, flushed ? "Cannot restore protection on the early RsEventHandler CALL" :
                                      "Cannot flush the early RsEventHandler CALL write");
}

bool get_executable_path(std::filesystem::path& result, std::wstring& error) {
    std::wstring buffer(260, L'\0');
    for (;;) {
        SetLastError(ERROR_SUCCESS);
        const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (!length) {
            error = L"Cannot determine the game executable path (GetModuleFileNameW failed).";
            return false;
        }
        if (length < buffer.size()) {
            buffer.resize(length);
            result = std::filesystem::path(buffer);
            if (result.is_absolute() && !result.filename().empty()) return true;
            error = L"The game executable path is not absolute or has no file name.";
            return false;
        }
        if (buffer.size() >= 32768) {
            error = L"The game executable path is too long to use for user files.";
            return false;
        }
        buffer.resize((std::min)(buffer.size() * 2, size_t(32768)), L'\0');
    }
}

bool get_documents_path(std::filesystem::path& result, std::wstring& error) {
    std::array<wchar_t, MAX_PATH> buffer{};
    const HRESULT status = SHGetFolderPathW(nullptr, CSIDL_PERSONAL, nullptr,
                                            SHGFP_TYPE_CURRENT, buffer.data());
    if (FAILED(status) || !buffer[0]) {
        error = L"Cannot resolve the Windows Documents directory (SHGetFolderPathW failed).";
        return false;
    }
    result = std::filesystem::path(buffer.data());
    if (!result.is_absolute()) {
        error = L"Windows returned a non-absolute Documents path.";
        return false;
    }
    return true;
}

bool path_to_ansi(const std::filesystem::path& path,
                  std::array<char, directory_buffer_size>& output,
                  std::wstring& error) {
    std::wstring value = path.native();
    if (value.empty()) {
        error = L"A user-files directory path is empty.";
        return false;
    }
    const UINT code_page = GetACP();
    const DWORD flags = code_page == CP_UTF8 ? WC_ERR_INVALID_CHARS : WC_NO_BEST_FIT_CHARS;
    BOOL used_default = FALSE;
    const int written = WideCharToMultiByte(code_page, flags, value.c_str(), -1,
                                             output.data(), static_cast<int>(output.size()),
                                             nullptr, code_page == CP_UTF8 ? nullptr : &used_default);
    if (written <= 0 || (code_page != CP_UTF8 && used_default)) {
        error = L"A user-files path contains characters that the game's ANSI file APIs cannot represent.";
        return false;
    }
    if (static_cast<size_t>(written) > output.size()) {
        error = L"A user-files path exceeds the game's 255-character directory buffer.";
        return false;
    }
    return true;
}

bool ensure_directory(const std::filesystem::path& path, bool& created, std::wstring& error) {
    created = false;
    const std::wstring native = path.native();
    if (CreateDirectoryW(native.c_str(), nullptr)) {
        created = true;
    } else if (GetLastError() != ERROR_ALREADY_EXISTS) {
        error = L"Cannot create user-files directory: " + native;
        return false;
    }
    const DWORD attributes = GetFileAttributesW(native.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES || !(attributes & FILE_ATTRIBUTE_DIRECTORY)) {
        error = L"The user-files path is not a directory: " + native;
        return false;
    }
    if (attributes & FILE_ATTRIBUTE_REPARSE_POINT) {
        error = L"The user-files directory is a junction or symbolic link; refusing to redirect saves: " + native;
        return false;
    }
    return true;
}

bool validate_writable(const std::filesystem::path& directory, unsigned ordinal,
                       std::wstring& error) {
    const std::wstring native = directory.native();
    const std::wstring file = native + L"\\.lcs-userfiles-check-" +
        std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount()) +
        L"-" + std::to_wstring(ordinal) + L".tmp";
    HANDLE handle = CreateFileW(file.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                                FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        error = L"Cannot create a temporary write-check in user-files directory: " + native;
        return false;
    }
    constexpr char marker[] = "LCS";
    DWORD written = 0;
    const bool wrote = WriteFile(handle, marker, sizeof(marker), &written, nullptr) != 0 &&
                       written == sizeof(marker);
    const bool flushed = wrote && FlushFileBuffers(handle) != 0;
    CloseHandle(handle);
    if (!flushed) {
        error = L"Cannot write and flush a temporary check in user-files directory: " + native;
        return false;
    }
    return true;
}

bool install_get_user_files_hook(std::wstring& error);

bool write_native_directories(const std::array<char, directory_buffer_size>& tracks,
                              const std::array<char, directory_buffer_size>& gallery,
                              std::array<uint8_t, directory_buffer_size * 2>& original,
                              std::wstring& error) {
    auto* memory = reinterpret_cast<uint8_t*>(tracks_directory_buffer);
    SIZE_T received = 0;
    if (!ReadProcessMemory(GetCurrentProcess(), memory, original.data(), original.size(), &received) ||
        received != original.size()) {
        error = L"The game's User Tracks and Gallery path buffers are unreadable.";
        return false;
    }

    DWORD previous_protection = 0;
    if (!VirtualProtect(memory, original.size(), PAGE_READWRITE, &previous_protection)) {
        error = L"Cannot make the game's User Tracks and Gallery path buffers writable.";
        return false;
    }
    std::array<uint8_t, directory_buffer_size * 2> replacement{};
    std::memcpy(replacement.data(), tracks.data(), tracks.size());
    std::memcpy(replacement.data() + directory_buffer_size, gallery.data(), gallery.size());
    SIZE_T written = 0;
    if (!WriteProcessMemory(GetCurrentProcess(), memory, replacement.data(), replacement.size(), &written) ||
        written != replacement.size()) {
        SIZE_T ignored_bytes = 0;
        const bool rolled_back = WriteProcessMemory(GetCurrentProcess(), memory, original.data(),
                                                     original.size(), &ignored_bytes) != 0 &&
                                 ignored_bytes == original.size();
        DWORD ignored = 0;
        const bool protection_restored = VirtualProtect(memory, original.size(), previous_protection, &ignored) != 0;
        error = rolled_back && protection_restored ?
            L"The game's User Tracks and Gallery path buffers could not be updated safely; the write was rolled back." :
            L"The game's User Tracks and Gallery path-buffer write failed and its rollback could not be verified.";
        return false;
    }
    DWORD ignored = 0;
    if (VirtualProtect(memory, original.size(), previous_protection, &ignored)) return true;
    DWORD writable = 0;
    if (!VirtualProtect(memory, original.size(), PAGE_READWRITE, &writable)) {
        error = L"The path-buffer protection could not be restored or rolled back.";
        return false;
    }
    SIZE_T restored_bytes = 0;
    const bool rolled_back = WriteProcessMemory(GetCurrentProcess(), memory, original.data(),
                                                 original.size(), &restored_bytes) != 0 &&
                             restored_bytes == original.size();
    VirtualProtect(memory, original.size(), previous_protection, &ignored);
    error = rolled_back ? L"The path-buffer protection could not be restored; the write was rolled back." :
                           L"The path-buffer protection failed and its rollback could not be verified.";
    return false;
}

char* __cdecl sa_user_files_get_directory();

bool install_get_user_files_hook(std::wstring& error) {
    auto* code = reinterpret_cast<uint8_t*>(get_user_files_dir);
    std::array<uint8_t, get_user_files_prefix_size> observed{};
    SIZE_T received = 0;
    if (!ReadProcessMemory(GetCurrentProcess(), code, observed.data(), observed.size(), &received) ||
        received != observed.size()) {
        error = L"GetUserFilesDir is unreadable; refusing to change the game's storage path.";
        return false;
    }
    // SilentPatch already replaces GetUserFilesDir with a relative JMP. The hook
    // below returns the complete result, so another mod's 5-byte JMP can be
    // taken over; any other change means an unknown executable or patch.
    if (observed != get_user_files_prefix && observed[0] != 0xE9) {
        static const wchar_t digits[] = L"0123456789ABCDEF";
        std::wstring bytes;
        for (const uint8_t value : observed) {
            if (!bytes.empty()) bytes += L' ';
            bytes += digits[value >> 4];
            bytes += digits[value & 15];
        }
        error = L"GetUserFilesDir has been changed by another mod or executable version; refusing to overwrite it. Bytes: " + bytes;
        return false;
    }

    DWORD previous_protection = 0;
    if (!VirtualProtect(code, 5, PAGE_EXECUTE_READWRITE,
                        &previous_protection)) {
        error = L"Cannot make GetUserFilesDir writable.";
        return false;
    }
    const int64_t relative = int64_t(reinterpret_cast<uintptr_t>(&sa_user_files_get_directory)) -
                             int64_t(get_user_files_dir + 5);
    if (relative < std::numeric_limits<int32_t>::min() ||
        relative > std::numeric_limits<int32_t>::max()) {
        DWORD ignored = 0;
        VirtualProtect(code, 5, previous_protection, &ignored);
        error = L"The user-files directory hook is outside the supported jump range.";
        return false;
    }
    code[0] = 0xE9;
    const int32_t displacement = static_cast<int32_t>(relative);
    std::memcpy(code + 1, &displacement, sizeof(displacement));
    const bool flushed = FlushInstructionCache(GetCurrentProcess(), code, 5) != 0;
    DWORD ignored = 0;
    const bool restored = VirtualProtect(code, 5,
                                         previous_protection, &ignored) != 0;
    if (flushed && restored) return true;

    DWORD writable = 0;
    if (!VirtualProtect(code, 5, PAGE_EXECUTE_READWRITE, &writable)) {
        error = L"GetUserFilesDir could not be safely rolled back after a write failure.";
        return false;
    }
    SIZE_T restored_bytes = 0;
    const bool rolled_back = WriteProcessMemory(GetCurrentProcess(), code,
                                                 observed.data(), 5,
                                                 &restored_bytes) != 0 &&
                             restored_bytes == 5;
    FlushInstructionCache(GetCurrentProcess(), code, 5);
    VirtualProtect(code, 5, previous_protection, &ignored);
    error = !rolled_back ? L"GetUserFilesDir failed and its rollback could not be verified." :
        (flushed ? L"Cannot restore protection on GetUserFilesDir; the hook was rolled back." :
                   L"Cannot flush the GetUserFilesDir hook; the hook was rolled back.");
    return false;
}

bool restore_native_directories(const std::array<uint8_t, directory_buffer_size * 2>& original,
                                std::wstring& error) {
    auto* memory = reinterpret_cast<uint8_t*>(tracks_directory_buffer);
    DWORD previous_protection = 0;
    if (!VirtualProtect(memory, original.size(), PAGE_READWRITE, &previous_protection)) {
        error += L" Native path buffers could not be reopened for rollback.";
        return false;
    }
    SIZE_T written = 0;
    const bool restored = WriteProcessMemory(GetCurrentProcess(), memory, original.data(),
                                              original.size(), &written) != 0 &&
                          written == original.size();
    DWORD ignored = 0;
    const bool protection_restored = VirtualProtect(memory, original.size(), previous_protection, &ignored) != 0;
    if (!restored || !protection_restored) {
        error += L" Native path-buffer rollback could not be verified.";
        return false;
    }
    return true;
}

void remove_if_created(const std::filesystem::path& path, bool created) noexcept {
    if (!created) return;
    try {
        const std::wstring native = path.native();
        RemoveDirectoryW(native.c_str()); // Only removes an empty directory created by this attempt.
    } catch (...) {}
}

bool append_utf8_error(const char* message, std::wstring& output) {
    if (!message) return false;
    const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, message, -1, nullptr, 0);
    if (length <= 0) return false;
    std::wstring converted(static_cast<size_t>(length), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, message, -1,
                            converted.data(), length) != length) return false;
    converted.resize(static_cast<size_t>(length - 1));
    output += converted;
    return true;
}

bool prepare_user_files(std::wstring& error) {
    std::filesystem::path executable, documents;
    if (!get_executable_path(executable, error)) return false;
    if (!configured_options.portable && !get_documents_path(documents, error)) return false;

    UserFilesLayout layout;
    try {
        layout = user_files_layout(executable, documents, configured_options);
    } catch (const std::exception& exception) {
        error = L"Invalid user-files configuration: ";
        if (!append_utf8_error(exception.what(), error)) error += L"invalid folder name or executable path";
        return false;
    }

    auto make_directory_path = [](std::array<char, directory_buffer_size>& out,
                                  const std::filesystem::path& path, std::wstring& message) {
        if (!path.is_absolute()) {
            message = L"The configured user-files directory is not absolute.";
            return false;
        }
        return path_to_ansi(path, out, message);
    };
    if (!make_directory_path(root_directory_ansi, layout.root, error) ||
        !make_directory_path(tracks_directory_ansi, layout.tracks, error) ||
        !make_directory_path(gallery_directory_ansi, layout.gallery, error)) return false;

    bool root_created = false, tracks_created = false, gallery_created = false;
    if (!ensure_directory(layout.root, root_created, error) ||
        !ensure_directory(layout.tracks, tracks_created, error) ||
        !ensure_directory(layout.gallery, gallery_created, error)) {
        remove_if_created(layout.gallery, gallery_created);
        remove_if_created(layout.tracks, tracks_created);
        remove_if_created(layout.root, root_created);
        return false;
    }
    if (!validate_writable(layout.root, 0, error) ||
        !validate_writable(layout.tracks, 1, error) ||
        !validate_writable(layout.gallery, 2, error)) {
        remove_if_created(layout.gallery, gallery_created);
        remove_if_created(layout.tracks, tracks_created);
        remove_if_created(layout.root, root_created);
        return false;
    }

    std::array<uint8_t, directory_buffer_size * 2> previous_buffers{};
    if (!write_native_directories(tracks_directory_ansi, gallery_directory_ansi,
                                  previous_buffers, error)) {
        remove_if_created(layout.gallery, gallery_created);
        remove_if_created(layout.tracks, tracks_created);
        remove_if_created(layout.root, root_created);
        return false;
    }
    if (!install_get_user_files_hook(error)) {
        restore_native_directories(previous_buffers, error);
        remove_if_created(layout.gallery, gallery_created);
        remove_if_created(layout.tracks, tracks_created);
        remove_if_created(layout.root, root_created);
        return false;
    }
    return true;
}

void show_deferred_error(const std::wstring& error) noexcept {
    try {
        std::wstring message = L"LCS user files could not be initialized. The game will stop before using its shared San Andreas user files.\n\n";
        message += error;
        OutputDebugStringW((message + L"\n").c_str());
        MessageBoxW(nullptr, message.c_str(), L"LCS user files", MB_OK | MB_ICONERROR | MB_SETFOREGROUND);
    } catch (...) {
        OutputDebugStringA("LCS user files initialization failed; startup was stopped to avoid using shared San Andreas user files.\n");
    }
}

BOOL __cdecl sa_user_files_event_handler(int32_t event, void* parameter) {
    LONG state = InterlockedCompareExchange(&layout_state, 2, 2);
    if (state == 0) {
        const LONG prior = InterlockedCompareExchange(&layout_state, 1, 0);
        if (prior == 0) {
            std::wstring error;
            bool ready = false;
            try {
                ready = prepare_user_files(error);
            } catch (const std::exception& exception) {
                try {
                    error = L"User-files initialization failed";
                    error += L": ";
                    if (!append_utf8_error(exception.what(), error)) error += L"unexpected exception";
                } catch (...) {}
            } catch (...) {
                error = L"Unexpected failure while preparing the user-files directories.";
            }
            if (ready) {
                InterlockedExchange(&layout_state, 2);
                state = 2;
            } else {
                InterlockedExchange(&layout_state, -1);
                show_deferred_error(error.empty() ? L"The user-files paths could not be created and verified." : error);
                return FALSE;
            }
        } else {
            state = prior;
        }
    }
    if (state == 1) {
        // The startup event is normally single-threaded. If another thread
        // entered concurrently, wait for the single preparation attempt.
        for (unsigned spin = 0; spin < 2000; ++spin) {
            Sleep(0);
            state = InterlockedCompareExchange(&layout_state, 2, 2);
            if (state != 1) break;
        }
    }
    if (state != 2) return FALSE;
    return reinterpret_cast<EventHandler>(original_event_handler)(event, parameter);
}

char* __cdecl sa_user_files_get_directory() {
    return root_directory_ansi.data();
}

} // namespace

bool sa_user_files_install(const UserFilesOptions& options, std::string& error) noexcept {
    error.clear();
    try {
        if (install_failed_uncertainly)
            return set_error(error, "A previous user-files hook write had an uncertain rollback; do not retry in this process");
        if (install_complete) {
            if (configured_options.portable != options.portable ||
                configured_options.folder_name != options.folder_name)
                return set_error(error, "User-files hooks were already installed with different settings");
            uintptr_t current = 0;
            if (!read_relative_call(event_handler_call, current, error)) return false;
            if (current != reinterpret_cast<uintptr_t>(&sa_user_files_event_handler))
                return set_error(error, "The installed RsEventHandler hook was changed by another mod");
            return true;
        }

        HMODULE executable = GetModuleHandleW(nullptr);
        if (!executable || reinterpret_cast<uintptr_t>(executable) != 0x400000)
            return set_error(error, "User files require the supported 32-bit San Andreas executable at its expected base address");
        uintptr_t original = 0;
        if (!read_relative_call(event_handler_call, original, error)) return false;
        if (!executable_page(event_handler_call, executable) || !executable_page(original, executable))
            return set_error(error, "The RsEventHandler CALL or its target is outside the game executable; refusing an unknown hook");

        configured_options = options;
        original_event_handler = original;
        if (!patch_relative_call(event_handler_call,
                                 reinterpret_cast<uintptr_t>(&sa_user_files_event_handler),
                                 original, error)) return false;
        install_complete = true;
        return true;
    } catch (const std::exception& exception) {
        return set_error(error, exception.what());
    } catch (...) {
        return set_error(error, "Unexpected failure while installing user-files hooks");
    }
}

} // namespace lcs
