#include "androidpath.hpp"

#if defined(__ANDROID__)

#include <jni.h>
#include <cstdlib>
#include <cstring>
#include <string>
#include <mutex>
#include <pwd.h>
#include <unistd.h>
#include <boost/filesystem/fstream.hpp>

static std::string g_path_global; //< Path to global directory root, e.g. /data/data/com.libopenmw.openmw
static std::string g_path_user; //< Path to user root, e.g. /sdcard/Android/data/com.libopenmw.openmw
static std::mutex g_path_mutex;

/**
 * \brief Called by java code to set up directory paths
 */
extern "C" JNIEXPORT void JNICALL Java_ui_activity_GameActivity_getPathToJni(JNIEnv *env, jobject obj, jstring global, jstring user)
{
    const char* globalChars = global != nullptr ? env->GetStringUTFChars(global, nullptr) : nullptr;
    const char* userChars = user != nullptr ? env->GetStringUTFChars(user, nullptr) : nullptr;
    {
        std::lock_guard<std::mutex> lock(g_path_mutex);
        g_path_global = globalChars != nullptr ? globalChars : "";
        g_path_user = userChars != nullptr ? userChars : "";
    }
    if (globalChars != nullptr)
        env->ReleaseStringUTFChars(global, globalChars);
    if (userChars != nullptr)
        env->ReleaseStringUTFChars(user, userChars);
}

namespace Files
{

AndroidPath::AndroidPath(const std::string& application_name)
{
}

// /sdcard/Android/data/com.libopenmw.openmw/config
boost::filesystem::path AndroidPath::getUserConfigPath() const
{
    std::lock_guard<std::mutex> lock(g_path_mutex);
    return boost::filesystem::path(g_path_user.empty() ? "." : g_path_user) / "config";
}

// /sdcard/Android/data/com.libopenmw.openmw/
// (so that saves are placed at /sdcard/Android/data/com.libopenmw.openmw/saves)
boost::filesystem::path AndroidPath::getUserDataPath() const
{
    std::lock_guard<std::mutex> lock(g_path_mutex);
    return boost::filesystem::path(g_path_user.empty() ? "." : g_path_user);
}

// /data/data/com.libopenmw.openmw/cache
// (supposed to be "official" android cache location)
boost::filesystem::path AndroidPath::getCachePath() const
{
    std::lock_guard<std::mutex> lock(g_path_mutex);
    return boost::filesystem::path(g_path_global.empty() ? "." : g_path_global) / "cache";
}

// /data/data/com.libopenmw.openmw/files/config
// (note the addition of "files")
boost::filesystem::path AndroidPath::getGlobalConfigPath() const
{
    std::lock_guard<std::mutex> lock(g_path_mutex);
    return boost::filesystem::path(g_path_global.empty() ? "." : g_path_global) / "files" / "config";
}

boost::filesystem::path AndroidPath::getLocalPath() const
{
    return boost::filesystem::path("./");
}

// /sdcard/Android/data/com.libopenmw.openmw
// (so that the data is at /sdcard/Android/data/com.libopenmw.openmw/data)
boost::filesystem::path AndroidPath::getGlobalDataPath() const
{
    std::lock_guard<std::mutex> lock(g_path_mutex);
    return boost::filesystem::path(g_path_user.empty() ? "." : g_path_user);
}

boost::filesystem::path AndroidPath::getInstallPath() const
{
    return boost::filesystem::path();
}


} /* namespace Files */

#endif /* defined(__Android__) */
