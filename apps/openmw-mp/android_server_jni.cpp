#if defined(__ANDROID__)

#include <jni.h>
#include <unistd.h>
#include <cstdlib>
#include <string>
#include <vector>

#include "Networking.hpp"

int tes3mpServerMain(int argc, char *argv[]);

// Reuse the Android path initialization compiled into the server library's
// components target. This gives ConfigurationManager the same Android roots
// as the client while the dedicated server itself runs from its private
// portable runtime directory.
extern "C" JNIEXPORT void JNICALL
Java_ui_activity_GameActivity_getPathToJni(JNIEnv*, jobject, jstring, jstring);

namespace
{
    std::string fromJString(JNIEnv* env, jstring value)
    {
        if (!value)
            return std::string();
        const char* chars = env->GetStringUTFChars(value, nullptr);
        std::string out = chars ? chars : "";
        if (chars)
            env->ReleaseStringUTFChars(value, chars);
        return out;
    }
}

extern "C" JNIEXPORT jint JNICALL
Java_server_ArenaServerService_nativeRun(JNIEnv* env, jobject instance,
    jstring globalPath, jstring userPath, jstring runtimePath)
{
    Java_ui_activity_GameActivity_getPathToJni(env, instance, globalPath, userPath);

    const std::string runtime = fromJString(env, runtimePath);
    const std::string userRoot = fromJString(env, userPath);
    if (runtime.empty() || ::chdir(runtime.c_str()) != 0)
        return 126;

    // Dedicated-server-only path override. The client process never sets it.
    if (!userRoot.empty())
        ::setenv("ARENAMP_ANDROID_SERVER_ROOT", userRoot.c_str(), 1);

    std::vector<std::string> args;
    args.emplace_back("tes3mp-server");
    args.emplace_back("--resources=resources");

    std::vector<char*> argv;
    argv.reserve(args.size() + 1);
    for (std::string& arg : args)
        argv.push_back(&arg[0]);
    argv.push_back(nullptr);

    try
    {
        return tes3mpServerMain(static_cast<int>(args.size()), argv.data());
    }
    catch (...)
    {
        // Keep native exceptions from crossing the JNI boundary. The Android
        // service reports the non-zero code and may apply its restart policy.
        return 125;
    }
}

extern "C" JNIEXPORT void JNICALL
Java_server_ArenaServerService_nativeStop(JNIEnv*, jobject)
{
    if (mwmp::Networking* networking = mwmp::Networking::getPtr())
        networking->stopServer(0);
}

#endif
