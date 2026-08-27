#include <jni.h>
#include <string>
#include <android/log.h>
#include <memory>
#include <thread>
#include <mutex>
#include "udp_nic_adapter.hpp"
#include "joker/neighbor.hpp"
#include "joker/dedup_cache.hpp"
#include "joker/timer_wheel.hpp"
#include "joker/coordinator.hpp"
#include "joker/metrics.hpp"
#include "joker/forwarding.hpp"

#define LOG_TAG "JokerProtocol"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

using namespace joker;
using namespace joker::android;

struct JokerInstance {
    std::unique_ptr<UdpNicAdapter> nic;
    NeighborTable neighbors;
    DedupCache dedup;
    SimpleTimerWheel timer_wheel;
    std::unique_ptr<TimerCoordinator> coordinator;
    SimpleMetrics metrics;
    Config config;

    JokerInstance(const std::string& ip, bool is_go) : dedup(1000, std::chrono::milliseconds(5000)) {
        // Generate a random Virtual MAC based on time to ensure uniqueness
        uint8_t rand_byte = static_cast<uint8_t>(time(nullptr) % 255);
        MacAddress mac({0x02, 0x00, 0x00, 0x00, 0x00, rand_byte});

        nic = std::make_unique<UdpNicAdapter>(ip, 5005, mac);
        coordinator = std::make_unique<TimerCoordinator>(timer_wheel);
        
        nic->RegisterReceiveCallback([this](const std::vector<uint8_t>& frame, const MacAddress& mac, bool is_cand) {
            process_received_frame(frame, mac, is_cand, neighbors, dedup, *coordinator, *nic, metrics, config);
        });
        
        nic->Start();
        LOGI("JOKER Protocol instance created with MAC %s", mac.ToString().c_str());
    }

    ~JokerInstance() {
        if (nic) {
            nic->Stop();
        }
    }
};

static std::unique_ptr<JokerInstance> g_joker_instance = nullptr;
static std::mutex g_mutex;

extern "C" JNIEXPORT void JNICALL
Java_com_example_jokerprotocol_MainActivity_startJokerProtocol(
        JNIEnv* env, jobject /* this */, jstring bindIp, jboolean isGroupOwner) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_joker_instance) {
        LOGI("Joker is already running.");
        return;
    }
    
    const char* ip_chars = env->GetStringUTFChars(bindIp, nullptr);
    std::string ip(ip_chars);
    env->ReleaseStringUTFChars(bindIp, ip_chars);
    
    g_joker_instance = std::make_unique<JokerInstance>(ip, isGroupOwner);
}

extern "C" JNIEXPORT void JNICALL
Java_com_example_jokerprotocol_MainActivity_stopJokerProtocol(
        JNIEnv* env, jobject /* this */) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_joker_instance.reset();
    LOGI("JOKER Protocol stopped.");
}

extern "C" JNIEXPORT void JNICALL
Java_com_example_jokerprotocol_MainActivity_sendChatMessage(
        JNIEnv* env, jobject /* this */, jstring message) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_joker_instance) return;
    
    const char* msg_chars = env->GetStringUTFChars(message, nullptr);
    std::string msg(msg_chars);
    env->ReleaseStringUTFChars(message, msg_chars);
    
    // Broadcast a dummy message for now as a proof of concept.
    // In a full implementation we would encode this in a DATA packet.
    std::vector<uint8_t> payload(msg.begin(), msg.end());
    g_joker_instance->nic->TransmitBroadcast(payload);
    LOGI("Sent payload over UdpNicAdapter: %s", msg.c_str());
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_example_jokerprotocol_MainActivity_stringFromJNI(
        JNIEnv* env, jobject /* this */) {
    return env->NewStringUTF("Legacy Method");
}
