#include <open62541/client_config_default.h>
#include <open62541/client_subscriptions.h>
#include <open62541/server_config_default.h>

#include <atomic>
#include <charconv>
#include <chrono>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>

namespace {
using namespace std::chrono_literals;
using Client = std::unique_ptr<UA_Client, decltype(&UA_Client_delete)>;
using Server = std::unique_ptr<UA_Server, decltype(&UA_Server_delete)>;
constexpr UA_Int32 sample = 42;
constexpr UA_DateTime source_time = UA_DATETIME_UNIX_EPOCH + UA_DATETIME_SEC;

UA_StatusCode read_sample(UA_Server*, const UA_NodeId*, void*, const UA_NodeId*, void* context,
                         UA_Boolean include_time, const UA_NumericRange*, UA_DataValue* value) {
    const auto status = UA_Variant_setScalarCopy(&value->value, context, &UA_TYPES[UA_TYPES_INT32]);
    value->hasValue = status == UA_STATUSCODE_GOOD;
    value->hasSourceTimestamp = include_time;
    value->sourceTimestamp = source_time;
    return status;
}

bool read_fixture(const std::string& url, UA_UInt16 ns) {
    const Client client(UA_Client_new(), UA_Client_delete);
    if (!client) return false;
    auto* config = UA_Client_getConfig(client.get());
    config->timeout = 2000;
    config->securityMode = UA_MESSAGESECURITYMODE_NONE;
    if (UA_Client_connect(client.get(), url.c_str()) != UA_STATUSCODE_GOOD)
        return false;

    UA_ReadValueId nodes[2]{};
    nodes[0].nodeId = UA_NODEID_NUMERIC(ns, 1);
    nodes[1].nodeId = UA_NODEID_NUMERIC(ns, 2);
    nodes[0].attributeId = nodes[1].attributeId = UA_ATTRIBUTEID_VALUE;
    UA_ReadRequest request{};
    request.timestampsToReturn = UA_TIMESTAMPSTORETURN_BOTH;
    request.nodesToRead = nodes;
    request.nodesToReadSize = 2;
    auto reply = UA_Client_Service_read(client.get(), request);
    bool ok = false;
    if (reply.responseHeader.serviceResult == UA_STATUSCODE_GOOD && reply.resultsSize == 2) {
        const auto& good = reply.results[0];
        const auto& missing = reply.results[1];
        ok = good.status == UA_STATUSCODE_GOOD && good.hasValue
             && UA_Variant_hasScalarType(&good.value, &UA_TYPES[UA_TYPES_INT32])
             && *static_cast<const UA_Int32*>(good.value.data) == sample
             && good.hasSourceTimestamp && good.sourceTimestamp == source_time
             && good.hasServerTimestamp && good.serverTimestamp > source_time
             && missing.hasStatus && missing.status == UA_STATUSCODE_BADNODEIDUNKNOWN
             && !missing.hasValue;
    }
    UA_ReadResponse_clear(&reply);
    UA_Client_disconnect(client.get());
    return ok;
}

struct Certificate {
    std::string cert, key;
    UA_ByteString cert_bytes() { return bytes(cert); }
    UA_ByteString key_bytes() { return bytes(key); }
private:
    static UA_ByteString bytes(std::string& data) {
        return {data.size(), reinterpret_cast<UA_Byte*>(data.data())};
    }
};

std::string read_file(const char* path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("fixture certificate absent");
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

#ifdef UA_ENABLE_ENCRYPTION
void change_uri(UA_String& uri, const char* value) {
    UA_String_clear(&uri);
    uri = UA_String_fromChars(value);
}

bool subscribe(const std::string& url, UA_UInt16 ns, int expected,
               Certificate& identity, Certificate& trusted_server, bool should_connect) {
    const Client client(UA_Client_new(), UA_Client_delete);
    if (!client) return false;
    auto* config = UA_Client_getConfig(client.get());
    const auto trusted = trusted_server.cert_bytes();
    if (UA_ClientConfig_setDefaultEncryption(config, identity.cert_bytes(), identity.key_bytes(),
                                             &trusted, 1, nullptr, 0) != UA_STATUSCODE_GOOD)
        return false;
    change_uri(config->clientDescription.applicationUri, "urn:synthetic:client");
    config->timeout = 1200;
    config->securityMode = UA_MESSAGESECURITYMODE_SIGNANDENCRYPT;
    change_uri(config->securityPolicyUri, "http://opcfoundation.org/UA/SecurityPolicy#Basic256Sha256");
    const auto connected = UA_Client_connect(client.get(), url.c_str());
    if (!should_connect) return connected != UA_STATUSCODE_GOOD;
    if (connected != UA_STATUSCODE_GOOD) return false;

    auto request = UA_CreateSubscriptionRequest_default();
    request.requestedPublishingInterval = 100;
    auto subscription = UA_Client_Subscriptions_create(client.get(), request, nullptr, nullptr, nullptr);
    const auto id = subscription.subscriptionId;
    const bool available = subscription.responseHeader.serviceResult == UA_STATUSCODE_GOOD;
    UA_CreateSubscriptionResponse_clear(&subscription);
    if (!available) return false;

    int observation = -1;
    auto monitor = UA_MonitoredItemCreateRequest_default(UA_NODEID_NUMERIC(ns, 1));
    monitor.requestedParameters.samplingInterval = 50;
    auto result = UA_Client_MonitoredItems_createDataChange(client.get(), id, UA_TIMESTAMPSTORETURN_BOTH,
        monitor, &observation, [](UA_Client*, UA_UInt32, void*, UA_UInt32, void* state, UA_DataValue* value) {
            if (value->hasValue && value->status == UA_STATUSCODE_GOOD && value->hasSourceTimestamp
                && value->hasServerTimestamp && UA_Variant_hasScalarType(&value->value, &UA_TYPES[UA_TYPES_INT32]))
                *static_cast<int*>(state) = *static_cast<const UA_Int32*>(value->value.data);
        }, nullptr);
    const bool monitored = result.statusCode == UA_STATUSCODE_GOOD;
    UA_MonitoredItemCreateResult_clear(&result);
    if (!monitored) return false;
    const auto deadline = std::chrono::steady_clock::now() + 2s;
    while (observation == -1 && std::chrono::steady_clock::now() < deadline)
        UA_Client_run_iterate(client.get(), 100);
    UA_Client_disconnect(client.get());
    return observation == expected;
}
#endif

bool serve(UA_UInt16 port, const std::string& url, UA_Int32 value, Certificate* identity,
           Certificate* trusted_client, const std::function<bool(UA_UInt16)>& exercise) {
    UA_ServerConfig config{};
    UA_StatusCode status;
    if (!identity) {
        status = UA_ServerConfig_setMinimal(&config, port, nullptr);
    } else {
#ifdef UA_ENABLE_ENCRYPTION
        const auto cert = identity->cert_bytes(), key = identity->key_bytes(), trusted = trusted_client->cert_bytes();
        status = UA_ServerConfig_setDefaultWithSecurityPolicies(&config, port, &cert, &key,
                                                                       &trusted, 1, nullptr, 0, nullptr, 0);
        if (status == UA_STATUSCODE_GOOD) {
            config.securityPolicyNoneDiscoveryOnly = true;
            change_uri(config.applicationDescription.applicationUri, "urn:synthetic:server");
            for (std::size_t i = 0; i < config.endpointsSize; ++i)
                change_uri(config.endpoints[i].server.applicationUri, "urn:synthetic:server");
        }
#else
        (void)trusted_client;
        return false;
#endif
    }
    if (status != UA_STATUSCODE_GOOD) {
        UA_ServerConfig_clean(&config);
        return false;
    }
    UA_Array_delete(config.serverUrls, config.serverUrlsSize, &UA_TYPES[UA_TYPES_STRING]);
    config.serverUrls = static_cast<UA_String*>(UA_Array_new(1, &UA_TYPES[UA_TYPES_STRING]));
    config.serverUrlsSize = config.serverUrls ? 1 : 0;
    if (!config.serverUrls || !(config.serverUrls[0] = UA_String_fromChars(url.c_str())).data) {
        UA_ServerConfig_clean(&config);
        return false;
    }
    // Fixture identity and channel policy are separate from write permissions.
    auto& access = config.accessControl;
    access.getUserRightsMask = [](auto...) -> UA_UInt32 { return 0; };
    access.getUserAccessLevel = [](auto...) -> UA_Byte { return UA_ACCESSLEVELMASK_READ; };
    access.getUserExecutable = [](auto...) -> UA_Boolean { return false; };
    access.getUserExecutableOnObject = [](auto...) -> UA_Boolean { return false; };
    access.allowAddNode = [](auto...) -> UA_Boolean { return false; };
    access.allowAddReference = [](auto...) -> UA_Boolean { return false; };
    access.allowDeleteNode = [](auto...) -> UA_Boolean { return false; };
    access.allowDeleteReference = [](auto...) -> UA_Boolean { return false; };

    const Server server(
        UA_Server_newWithConfig(&config), UA_Server_delete);
    if (!server)
        return false;

    const auto ns = UA_Server_addNamespace(server.get(), "urn:synthetic:loopback");
    char name[] = "SyntheticSample";
    auto attr = UA_VariableAttributes_default;
    attr.accessLevel = UA_ACCESSLEVELMASK_READ;
    attr.dataType = UA_TYPES[UA_TYPES_INT32].typeId;
    UA_DataSource source{};
    source.read = read_sample;
    if (UA_Server_addDataSourceVariableNode(server.get(), UA_NODEID_NUMERIC(ns, 1),
            UA_NODEID_NUMERIC(0, UA_NS0ID_OBJECTSFOLDER), UA_NODEID_NUMERIC(0, UA_NS0ID_ORGANIZES),
            UA_QUALIFIEDNAME(ns, name), UA_NODEID_NUMERIC(0, UA_NS0ID_BASEDATAVARIABLETYPE),
            attr, source, &value, nullptr) != UA_STATUSCODE_GOOD
        || UA_Server_run_startup(server.get()) != UA_STATUSCODE_GOOD)
        return false;

    std::atomic<bool> finish{false};
    std::thread loop([&] {
        while (!finish.load()) {
            UA_Server_run_iterate(server.get(), false);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });
    const bool ok = exercise(ns);
    finish.store(true);
    loop.join();
    UA_Server_run_shutdown(server.get());
    return ok;
}
}

int main(int argc, char** argv) {
    if (argc != 2 && argc != 8) return 2;
    const std::string port_text(argv[1]);
    UA_UInt16 port = 0;
    const auto end = port_text.data() + port_text.size();
    const auto parsed = std::from_chars(port_text.data(), end, port);
    if (parsed.ec != std::errc{} || parsed.ptr != end || port < 1024) return 2;
    const std::string url = "opc.tcp://127.0.0.1:" + std::to_string(port);

    if (argc == 2) {
        const bool ok = serve(port, url, sample, nullptr, nullptr, [&](UA_UInt16 ns) { return read_fixture(url, ns); });
        std::cout << (ok ? "Synthetic OPC UA Read passed (value, quality, timestamps, missing node)"
                         : "Synthetic OPC UA Read validation failed") << '\n';
        return ok ? 0 : 1;
    }
#ifdef UA_ENABLE_ENCRYPTION
    Certificate server{read_file(argv[2]), read_file(argv[3])};
    Certificate client{read_file(argv[4]), read_file(argv[5])};
    Certificate rogue{read_file(argv[6]), read_file(argv[7])};
    const auto first = serve(port, url, 42, &server, &client, [&](UA_UInt16 ns) {
        return subscribe(url, ns, 42, client, server, true)
            && subscribe(url, ns, 42, rogue, server, false)
            && subscribe(url, ns, 42, client, rogue, false);
    });
    if (!first || !subscribe(url, 0, 42, client, server, false)) return 1;
    const auto second = serve(port, url, 43, &server, &client, [&](UA_UInt16 ns) {
        return subscribe(url, ns, 43, client, server, true);
    });
    if (second) std::cout << "Synthetic OPC UA SignAndEncrypt, trust rejection and subscription after restart passed\n";
    return second ? 0 : 1;
#else
    std::cerr << "Fixture build lacks open62541 encryption support\n";
    return 1;
#endif
}
