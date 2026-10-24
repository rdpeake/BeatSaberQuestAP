#include "MediaScanner.hpp"

#include <dlfcn.h>
#include <jni.h>

#include <thread>

#include "Logger.hpp"

namespace BeatSaberQuestAP::MediaScanner {

    namespace {
        using GetCreatedJavaVMs = jint (*)(JavaVM**, jsize, jsize*);

        JavaVM* FindJavaVM() {
            for (char const* library : {"libnativehelper.so", "libart.so"}) {
                void* handle = dlopen(library, RTLD_NOW | RTLD_NOLOAD);
                if (!handle)
                    continue;
                auto getCreatedJavaVMs = reinterpret_cast<GetCreatedJavaVMs>(dlsym(handle, "JNI_GetCreatedJavaVMs"));
                JavaVM* javaVM = nullptr;
                jsize vmCount = 0;
                if (getCreatedJavaVMs && getCreatedJavaVMs(&javaVM, 1, &vmCount) == JNI_OK && vmCount > 0)
                    return javaVM;
            }
            return nullptr;
        }

        bool ClearException(JNIEnv* env, char const* step) {
            if (!env->ExceptionCheck())
                return false;
            env->ExceptionClear();
            logger.warn("Media scan failed at {}", step);
            return true;
        }

        void Scan(JNIEnv* env, std::string const& path) {
            jclass activityThread = env->FindClass("android/app/ActivityThread");
            if (ClearException(env, "ActivityThread") || !activityThread)
                return;
            jmethodID currentApplication = env->GetStaticMethodID(activityThread, "currentApplication", "()Landroid/app/Application;");
            if (ClearException(env, "currentApplication") || !currentApplication)
                return;
            jobject context = env->CallStaticObjectMethod(activityThread, currentApplication);
            if (ClearException(env, "context") || !context)
                return;

            jclass scannerConnection = env->FindClass("android/media/MediaScannerConnection");
            if (ClearException(env, "MediaScannerConnection") || !scannerConnection)
                return;
            jmethodID scanFile = env->GetStaticMethodID(
                scannerConnection,
                "scanFile",
                "(Landroid/content/Context;[Ljava/lang/String;[Ljava/lang/String;Landroid/media/MediaScannerConnection$OnScanCompletedListener;)V"
            );
            if (ClearException(env, "scanFile") || !scanFile)
                return;

            jclass stringClass = env->FindClass("java/lang/String");
            jobjectArray paths = env->NewObjectArray(1, stringClass, env->NewStringUTF(path.c_str()));
            env->CallStaticVoidMethod(scannerConnection, scanFile, context, paths, nullptr, nullptr);
            if (!ClearException(env, "scan request"))
                logger.info("Requested media scan of {}", path);
        }
    }

    void ScanFile(std::string path) {
        std::thread([path = std::move(path)] {
            JavaVM* javaVM = FindJavaVM();
            if (!javaVM) {
                logger.warn("No Java VM found for media scan");
                return;
            }
            JNIEnv* env = nullptr;
            if (javaVM->AttachCurrentThread(&env, nullptr) != JNI_OK || !env)
                return;
            Scan(env, path);
            javaVM->DetachCurrentThread();
        }).detach();
    }
}
