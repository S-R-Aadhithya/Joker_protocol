#include <jni.h>
#include <string>
#include <android/log.h>
// Include joker headers as needed
// #include "joker/interface.hpp"

#define LOG_TAG "JokerProtocol"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

extern "C" JNIEXPORT jstring JNICALL
Java_com_example_jokerprotocol_MainActivity_stringFromJNI(
        JNIEnv* env,
        jobject /* this */) {
    std::string hello = "Hello from Joker Protocol C++ Core";
    LOGI("JNI initialized!");
    return env->NewStringUTF(hello.c_str());
}
