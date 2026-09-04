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

static JavaVM* g_vm = nullptr;
static jclass g_main_activity_class = nullptr;
static jmethodID g_on_native_log_method = nullptr;

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* reserved) {
    g_vm = vm;
    JNIEnv* env;
    if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) {
        return JNI_ERR;
    }
    jclass localClass = env->FindClass("com/example/jokerprotocol/MainActivity");
    if (localClass) {
        g_main_activity_class = reinterpret_cast<jclass>(env->NewGlobalRef(localClass));
        g_on_native_log_method = env->GetStaticMethodID(g_main_activity_class, "onNativeLog", "(Ljava/lang/String;)V");
    }
    return JNI_VERSION_1_6;
}

void dispatch_log_to_ui(const std::string& msg) {
    if (!g_vm || !g_main_activity_class || !g_on_native_log_method) return;
    JNIEnv* env;
    bool attached = false;
    int status = g_vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6);
    if (status == JNI_EDETACHED) {
        if (g_vm->AttachCurrentThread(&env, nullptr) != 0) return;
        attached = true;
    } else if (status == JNI_EVERSION) {
        return;
    }
    
    jstring jmsg = env->NewStringUTF(msg.c_str());
    env->CallStaticVoidMethod(g_main_activity_class, g_on_native_log_method, jmsg);
    env->DeleteLocalRef(jmsg);
    
    if (attached) {
        g_vm->DetachCurrentThread();
    }
}

void custom_log(int prio, const char* tag, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    __android_log_vprint(prio, tag, fmt, args);
    va_end(args);

    va_start(args, fmt);
    char buf[2048];
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    
    dispatch_log_to_ui(std::string(buf));
}

#define LOGI(...) custom_log(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) custom_log(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

using namespace joker;
using namespace joker::android;

struct JokerInstance {
    std::string name;
    std::unique_ptr<UdpNicAdapter> nic;
    NeighborTable neighbors;
    DedupCache dedup;
    SimpleTimerWheel timer_wheel;
    std::unique_ptr<TimerCoordinator> coordinator;
    SimpleMetrics metrics;
    Config config;

    JokerInstance(const std::string& node_name, const std::string& ip, uint16_t port) 
        : name(node_name), dedup(1000, std::chrono::milliseconds(5000)) {
        
        // Generate a random Virtual MAC based on time to ensure uniqueness
        uint8_t rand_byte = static_cast<uint8_t>(time(nullptr) % 255);
        MacAddress mac({0x02, 0x00, 0x00, 0x00, 0x00, rand_byte});

        nic = std::make_unique<UdpNicAdapter>(ip, port, mac);
        coordinator = std::make_unique<TimerCoordinator>(timer_wheel);
        
        nic->RegisterReceiveCallback([this](const std::vector<uint8_t>& frame, const MacAddress& mac, bool is_cand) {
            process_received_frame(frame, mac, is_cand, neighbors, dedup, *coordinator, *nic, metrics, config);
            
            // LOG TO UI and LOGCAT!
            // To extract the string, we need to skip the header.
            size_t consumed = 0;
            auto hdr = deserialize_header(frame, consumed);
            if (hdr && frame.size() > consumed) {
                std::string payload(frame.begin() + consumed, frame.end());
                LOGI("[Node %s] RECEIVED PACKET: %s", name.c_str(), payload.c_str());
            } else {
                LOGI("[Node %s] RECEIVED RAW/MALFORMED PACKET length=%zu", name.c_str(), frame.size());
            }
        });
        
        nic->Start();
        LOGI("JOKER Protocol [Node %s] created with MAC %s on port %d", name.c_str(), mac.ToString().c_str(), port);
    }

    ~JokerInstance() {
        if (nic) {
            nic->Stop();
        }
    }
};

static std::unique_ptr<JokerInstance> g_node = nullptr;
static std::mutex g_mutex;

extern "C" JNIEXPORT void JNICALL
Java_com_example_jokerprotocol_MainActivity_startJokerProtocol(
        JNIEnv* env, jobject /* this */, jstring bindIp, jboolean isGroupOwner) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_node) {
        LOGI("Joker is already running.");
        return;
    }
    
    const char* ip_chars = env->GetStringUTFChars(bindIp, nullptr);
    std::string ip(ip_chars);
    env->ReleaseStringUTFChars(bindIp, ip_chars);
    
    // We name the node "GO" or "CLI" for log clarity
    std::string name = isGroupOwner ? "GO" : "CLI";
    g_node = std::make_unique<JokerInstance>(name, ip, 5005);
    LOGI("Started Node %s bound to IP %s on port 5005", name.c_str(), ip.c_str());
}

extern "C" JNIEXPORT void JNICALL
Java_com_example_jokerprotocol_MainActivity_stopJokerProtocol(
        JNIEnv* env, jobject /* this */) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_node.reset();
    LOGI("JOKER Protocol Node stopped.");
}

extern "C" JNIEXPORT void JNICALL
Java_com_example_jokerprotocol_MainActivity_setPeerIp(
        JNIEnv* env, jobject /* this */, jstring peerIp) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_node || !g_node->nic) {
        LOGI("Joker node not running, cannot set peer IP.");
        return;
    }
    const char* ip_chars = env->GetStringUTFChars(peerIp, nullptr);
    std::string ip(ip_chars);
    env->ReleaseStringUTFChars(peerIp, ip_chars);
    
    g_node->nic->SetPeerIp(ip);
    LOGI("Successfully injected Client Unicast Peer IP from Kotlin: %s", ip.c_str());
}

void send_from_node(JokerInstance* node, const std::string& msg) {
    if (!node) return;
    
    JokerHeader hdr;
    hdr.type = PacketType::kUnicast;
    hdr.ttl = 32;
    hdr.packet_id = static_cast<uint32_t>(time(nullptr) ^ msg.length());
    hdr.final_destination = MacAddress({0x02, 0x00, 0x00, 0x00, 0x00, 0xFF}); // Broadcast/Dummy
    
    std::vector<uint8_t> wire;
    serialize_header(hdr, wire);
    wire.insert(wire.end(), msg.begin(), msg.end());
    
    node->nic->TransmitBroadcast(wire);
    LOGI("[Node %s] Sent payload: %s", node->name.c_str(), msg.c_str());
}

extern "C" JNIEXPORT void JNICALL
Java_com_example_jokerprotocol_MainActivity_sendChatMessage(
        JNIEnv* env, jobject /* this */, jstring message) {
    std::lock_guard<std::mutex> lock(g_mutex);
    const char* msg_chars = env->GetStringUTFChars(message, nullptr);
    std::string msg(msg_chars);
    env->ReleaseStringUTFChars(message, msg_chars);
    send_from_node(g_node.get(), msg);
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_example_jokerprotocol_MainActivity_stringFromJNI(
        JNIEnv* env, jobject /* this */) {
    return env->NewStringUTF("Legacy Method");
}
