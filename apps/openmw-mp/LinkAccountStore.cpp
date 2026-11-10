#include "LinkAccountStore.hpp"
#include <components/openmw-mp/arenalinkauth.hpp>
#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <algorithm>
#include <cctype>
#include <memory>
#include <set>
#include <mutex>
#include <chrono>
#include <ctime>
#ifdef _WIN32
#include <codecvt>
#include <locale>
#endif

namespace
{
    using Tree = boost::property_tree::ptree;
    namespace fs = boost::filesystem;
    fs::path utf8Path(const std::string& text)
    {
#ifdef _WIN32
        return fs::path(std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>>().from_bytes(text));
#else
        return fs::path(text);
#endif
    }
    std::string utf8Name(const fs::path& path)
    {
#ifdef _WIN32
        return std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>>().to_bytes(path.filename().wstring());
#else
        return path.filename().string();
#endif
    }
    std::string lower(std::string text)
    {
        for (char& c : text) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
        return text;
    }
    std::string accountFilename(std::string name)
    {
        // Same substitutions as CoreScripts fileHelper.fixFilename.
        for (char& c : name)
        {
            if (c == ':') c = ';';
            else if (c == '.') c = ',';
            else if (std::string("<>\"/\\|*?\r\n").find(c) != std::string::npos) c = '_';
        }
        static const std::set<std::string> reserved = {"CON","PRN","AUX","NUL",
            "COM1","COM2","COM3","COM4","COM5","COM6","COM7","COM8","COM9",
            "LPT1","LPT2","LPT3","LPT4","LPT5","LPT6","LPT7","LPT8","LPT9"};
        if (reserved.count(name)) name = "_" + name;
        return name + ".json";
    }
    bool readJson(const fs::path& path, Tree& tree)
    {
        try
        {
            if (!fs::is_regular_file(path) || fs::is_symlink(path) || fs::file_size(path) > 16 * 1024 * 1024)
                return false;
            fs::ifstream input(path, std::ios::binary);
            if (!input) return false;
            boost::property_tree::read_json(input, tree);
            return true;
        }
        catch (const std::exception&) { return false; }
    }
    std::function<void(const std::string&)> diagnostics(const fs::path& root)
    {
        auto lock = std::make_shared<std::mutex>();
        return [root, lock](const std::string& message) {
            std::lock_guard<std::mutex> guard(*lock);
            try
            {
                std::string clean = message.substr(0, 1024);
                std::replace(clean.begin(), clean.end(), '\n', ' ');
                std::replace(clean.begin(), clean.end(), '\r', ' ');
                const auto now = std::chrono::system_clock::now();
                const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
                for (const auto& path : { root / "ChatServer.log", fs::current_path() / "ChatServer.log" })
                {
                    boost::system::error_code ec;
                    if (fs::exists(path, ec) && fs::file_size(path, ec) >= 1024 * 1024)
                    {
                        fs::path old = path; old += ".1";
                        fs::remove(old, ec); ec.clear();
                        fs::rename(path, old, ec);
                        if (ec) continue;
                    }
                    fs::ofstream file(path, std::ios::app | std::ios::binary);
                    if (!file) continue;
                    file << "utc_unix_ms=" << ms << ' ' << clean << '\n';
                    file.flush();
                    if (file) return;
                }
            }
            catch (...) { /* Best-effort diagnostics, no account data. */ }
        };
    }

    struct Store
    {
        fs::path root;
        std::function<void(const std::string&)> log;
        std::map<std::string, std::uint32_t> ids;
        std::uint32_t nextId = 1;
        std::mutex idMutex;
        bool banned(const char* list, const std::string& value) const
        {
            Tree bans;
            // Missing or temporarily unreadable banlist fails closed.
            if (!readJson(root / "banlist.json", bans))
            {
                log("BANLIST_UNREADABLE access_denied=1");
                return true;
            }
            auto entries = bans.get_child_optional(list);
            if (!entries) return false;
            for (const auto& item : *entries)
                if (lower(item.second.get_value<std::string>()) == lower(value)) return true;
            return false;
        }
        bool find(const std::string& name, mwmp::LinkAccount& account)
        {
            try
            {
                if (name.empty() || name.size() > ArenaLink::sMaxNick || name.find('\0') != std::string::npos)
                    return false;
                const std::string wanted = lower(accountFilename(name));
                fs::path selected;
                const fs::path folder = root / "player";
                if (!fs::is_directory(folder)) { log("ACCOUNT_DIRECTORY_MISSING"); return false; }
                for (const fs::directory_entry& item : fs::directory_iterator(folder))
                {
                    if (lower(utf8Name(item.path())) != wanted) continue;
                    // Never guess between case-colliding profiles on Linux.
                    if (!selected.empty()) { log("ACCOUNT_CASE_COLLISION"); return false; }
                    selected = item.path();
                }
                if (selected.empty()) { log("ACCOUNT_NOT_FOUND"); return false; }
                Tree data;
                if (!readJson(selected, data)) { log("ACCOUNT_JSON_UNREADABLE"); return false; }
                account.name = data.get<std::string>("login.name", name);
                account.passwordSalt = data.get<std::string>("login.passwordSalt", "");
                account.passwordSha256 = data.get<std::string>("login.passwordHash", "");
                if (account.passwordSalt.empty() || account.passwordSalt.size() > 128 || account.passwordSha256.size() != 64)
                {
                    log("ACCOUNT_VERIFIER_INVALID");
                    return false;
                }
                {
                    // U034: findAccount is normally used by the ArenaLink TCP
                    // thread, but game -> launcher mirroring also resolves the
                    // same account on the main server thread. Keep transient
                    // user-id allocation race-free without serializing JSON IO.
                    std::lock_guard<std::mutex> guard(idMutex);
                    auto& id = ids[wanted];
                    if (id == 0) id = nextId++;
                    account.userId = id;
                }
                account.level = static_cast<std::uint16_t>(std::clamp(data.get<int>("stats.level", 1), 0, 65535));
                account.className = data.get<std::string>("character.class", "");
                account.banned = banned("playerNames", name) || banned("playerNames", account.name);
                const std::string color = data.get<std::string>("customVariables.chatColor", "");
                if (color.size() == 7 && color[0] == '#'
                    && std::all_of(color.begin()+1, color.end(), [](unsigned char c) { return std::isxdigit(c); }))
                    account.color = static_cast<std::uint32_t>(std::stoul(color.substr(1), nullptr, 16));
                return true;
            }
            catch (const std::exception&) { log("ACCOUNT_LOOKUP_EXCEPTION"); return false; }
        }
    };
}
namespace mwmp
{
    LinkCallbacks jsonLinkCallbacks(const std::string& dataDirectory)
    {
        auto store = std::make_shared<Store>();
        store->root = utf8Path(dataDirectory);
        try { store->root = fs::absolute(store->root); }
        catch (...) { /* Keep the original data root if cwd cannot be resolved. */ }
        store->log = diagnostics(store->root);
        try
        {
            store->log("DATA_ROOT path=" + store->root.generic_string());
            boost::system::error_code ec;
            const bool exists = fs::is_directory(store->root / "player", ec);
            store->log("ACCOUNT_DIRECTORY exists=" + std::to_string(exists)
                + " error=" + std::to_string(ec.value()));
        }
        catch (...) { store->log("DATA_ROOT_DIAGNOSTIC_UNAVAILABLE"); }
        LinkCallbacks callbacks;
        callbacks.diagnostic = store->log;
        callbacks.findAccount = [store](const std::string& name, LinkAccount& account) { return store->find(name, account); };
        callbacks.isAddressBanned = [store](const std::string& address) { return store->banned("ipAddresses", address); };
        callbacks.verifyProof = [](const LinkAccount& account, const std::string& nonce, const std::string& proof) {
            return ArenaLink::verifyStoredProof(account.passwordSha256, nonce, proof);
        };
        return callbacks;
    }
}
