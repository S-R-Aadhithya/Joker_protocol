#include <jni.h>
#include <string>
#include <android/log.h>
#include <memory>
#include <thread>
#include <atomic>
#include <mutex>
#include "udp_nic_adapter.hpp"
#include "joker/neighbor.hpp"
#include "joker/dedup_cache.hpp"
#include "joker/timer_wheel.hpp"
#include "joker/coordinator.hpp"
#include "joker/metrics.hpp"
#include "joker/forwarding.hpp"
#include "joker/crc32.hpp"

#define LOG_TAG "JokerProtocol"

static JavaVM* g_vm = nullptr;
static jclass g_main_activity_class = nullptr;
static jmethodID g_on_native_log_method = nullptr;
static jmethodID g_on_packet_received_method = nullptr;

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
        g_on_packet_received_method = env->GetStaticMethodID(g_main_activity_class, "onPacketReceived", "(Ljava/lang/String;)V");
    }
    return JNI_VERSION_1_6;
}

static JNIEnv* get_jni_env() {
    if (!g_vm) return nullptr;
    JNIEnv* env = nullptr;
    int status = g_vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6);
    if (status == JNI_OK) {
        return env;
    }
    if (status == JNI_EDETACHED) {
        if (g_vm->AttachCurrentThread(&env, nullptr) == 0) {
            thread_local struct ThreadDetacher {
                ~ThreadDetacher() {
                    if (g_vm) g_vm->DetachCurrentThread();
                }
            } detacher;
            return env;
        }
    }
    return nullptr;
}

void dispatch_log_to_ui(const std::string& msg) {
    if (!g_main_activity_class || !g_on_native_log_method) return;
    JNIEnv* env = get_jni_env();
    if (!env) return;
    
    jstring jmsg = env->NewStringUTF(msg.c_str());
    env->CallStaticVoidMethod(g_main_activity_class, g_on_native_log_method, jmsg);
    env->DeleteLocalRef(jmsg);
}

void dispatch_packet_to_ui(const std::string& payload) {
    if (!g_main_activity_class || !g_on_packet_received_method) return;
    JNIEnv* env = get_jni_env();
    if (!env) return;

    jstring jpayload = env->NewStringUTF(payload.c_str());
    env->CallStaticVoidMethod(g_main_activity_class, g_on_packet_received_method, jpayload);
    env->DeleteLocalRef(jpayload);
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
    
    std::atomic<bool> ticker_running_{false};
    std::thread ticker_thread_;

    JokerInstance(const std::string& node_name, const std::string& ip, uint16_t port, bool is_go) 
        : name(node_name), dedup(1000, std::chrono::milliseconds(5000)) {
        
        // Group Owner gets 0x01, Client gets 0x02
        uint8_t mac_id = is_go ? 0x01 : 0x02;
        MacAddress mac({0x02, 0x00, 0x00, 0x00, 0x00, mac_id});

        nic = std::make_unique<UdpNicAdapter>(ip, port, mac);
        coordinator = std::make_unique<TimerCoordinator>(timer_wheel);
        
        nic->RegisterReceiveCallback([this](const std::vector<uint8_t>& frame, const MacAddress& mac, bool is_cand) {
            size_t consumed = 0;
            auto hdr = deserialize_header(frame, consumed);
            if (!hdr) {
                process_received_frame(frame, mac, is_cand, neighbors, dedup, *coordinator, *nic, metrics, config);
                return;
            }

            // Deduplication: drop already processed frames or our own transmissions
            if (dedup.Contains(hdr->packet_id)) {
                process_received_frame(frame, mac, is_cand, neighbors, dedup, *coordinator, *nic, metrics, config);
                return;
            }

            // Fresh frame: process through JOKER core
            process_received_frame(frame, mac, is_cand, neighbors, dedup, *coordinator, *nic, metrics, config);

            // Deliver to UI only if targeted at this node or broadcast
            if (hdr->type == PacketType::kUnicast && frame.size() > consumed &&
                (hdr->final_destination == mac || hdr->final_destination.IsBroadcast())) {
                std::string payload(frame.begin() + consumed, frame.end());
                if (payload.rfind("FT_CHUNK:", 0) == 0) {
                    LOGI("[Node %s] RECEIVED FILE CHUNK (%zu bytes)", name.c_str(), frame.size());
                } else if (payload.rfind("FT_META:", 0) == 0) {
                    LOGI("[Node %s] RECEIVED FILE METADATA: %s", name.c_str(), payload.c_str());
                } else if (payload.rfind("FT_DONE:", 0) == 0) {
                    LOGI("[Node %s] RECEIVED FILE COMPLETE: %s", name.c_str(), payload.c_str());
                } else {
                    LOGI("[Node %s] RECEIVED PACKET: %s", name.c_str(), payload.c_str());
                }
                dispatch_packet_to_ui(payload);
            }
        });
        
        nic->Start();

        ticker_running_ = true;
        ticker_thread_ = std::thread([this]() {
            while (ticker_running_) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                timer_wheel.Tick();
            }
        });

        LOGI("JOKER Protocol [Node %s] created with MAC %s on port %d", name.c_str(), mac.ToString().c_str(), port);
    }

    ~JokerInstance() {
        ticker_running_ = false;
        if (ticker_thread_.joinable()) {
            ticker_thread_.join();
        }
        if (nic) {
            nic->Stop();
        }
    }
};

static std::unique_ptr<JokerInstance> g_node = nullptr;
static std::mutex g_mutex;
static std::atomic<uint32_t> g_seq_counter{1};

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
    g_node = std::make_unique<JokerInstance>(name, ip, 5005, isGroupOwner);
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
    if (!node || !node->nic) return;
    
    JokerHeader hdr;
    hdr.type = PacketType::kUnicast;
    hdr.ttl = 32;
    
    // Compute CRC32 packet ID over payload + monotonic counter to guarantee uniqueness
    std::vector<uint8_t> id_bytes(msg.begin(), msg.end());
    uint32_t seq = g_seq_counter.fetch_add(1);
    id_bytes.push_back(static_cast<uint8_t>((seq >> 24) & 0xFF));
    id_bytes.push_back(static_cast<uint8_t>((seq >> 16) & 0xFF));
    id_bytes.push_back(static_cast<uint8_t>((seq >> 8) & 0xFF));
    id_bytes.push_back(static_cast<uint8_t>(seq & 0xFF));
    hdr.packet_id = joker_packet_id(id_bytes.data(), id_bytes.size());

    // In 2-node direct P2P topology:
    // If we are GO, send to CLI (0x02). If we are CLI, send to GO (0x01).
    hdr.final_destination = (node->name == "GO")
        ? MacAddress({0x02, 0x00, 0x00, 0x00, 0x00, 0x02})
        : MacAddress({0x02, 0x00, 0x00, 0x00, 0x00, 0x01});
    
    // Mark packet as already seen locally so if echoed or relayed, we never re-process it
    node->dedup.Insert(hdr.packet_id);

    std::vector<uint8_t> wire;
    serialize_header(hdr, wire);
    wire.insert(wire.end(), msg.begin(), msg.end());
    
    node->nic->TransmitBroadcast(wire);
    LOGI("[Node %s] Sent payload (id=%u): %s", node->name.c_str(), hdr.packet_id, msg.c_str());
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
