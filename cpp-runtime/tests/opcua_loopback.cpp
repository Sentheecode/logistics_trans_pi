#include <open62541/client_config_default.h>
#include <open62541/server_config_default.h>

#include <atomic>
#include <charconv>
#include <chrono>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

namespace {
constexpr UA_Int32 sample = 42;
constexpr UA_DateTime source_time = UA_DATETIME_UNIX_EPOCH + UA_DATETIME_SEC;

UA_StatusCode read_sample(UA_Server*, const UA_NodeId*, void*, const UA_NodeId*, void*,
                         UA_Boolean include_time, const UA_NumericRange*, UA_DataValue* value) {
    const auto status = UA_Variant_setScalarCopy(&value->value, &sample, &UA_TYPES[UA_TYPES_INT32]);
    value->hasValue = status == UA_STATUSCODE_GOOD;
    value->hasSourceTimestamp = include_time;
    value->sourceTimestamp = source_time;
    return status;
}

bool read_fixture(const std::string& url, UA_UInt16 ns) {
    const std::unique_ptr<UA_Client, decltype(&UA_Client_delete)> client(UA_Client_new(), UA_Client_delete);
    if (!client || UA_ClientConfig_setDefault(UA_Client_getConfig(client.get())) != UA_STATUSCODE_GOOD)
        return false;
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
}

int main(int argc, char** argv) {
    if (argc != 2)
        return 2;
    const std::string port_text(argv[1]);
    UA_UInt16 port = 0;
    const auto end = port_text.data() + port_text.size();
    const auto parsed = std::from_chars(port_text.data(), end, port);
    if (parsed.ec != std::errc{} || parsed.ptr != end || port < 1024)
        return 2;

    const std::string url = "opc.tcp://127.0.0.1:" + std::to_string(port);
    UA_ServerConfig config{};
    if (UA_ServerConfig_setMinimal(&config, port, nullptr) != UA_STATUSCODE_GOOD) {
        UA_ServerConfig_clean(&config);
        return 1;
    }
    UA_Array_delete(config.serverUrls, config.serverUrlsSize, &UA_TYPES[UA_TYPES_STRING]);
    config.serverUrls = static_cast<UA_String*>(UA_Array_new(1, &UA_TYPES[UA_TYPES_STRING]));
    config.serverUrlsSize = config.serverUrls ? 1 : 0;
    if (!config.serverUrls || !(config.serverUrls[0] = UA_String_fromChars(url.c_str())).data) {
        UA_ServerConfig_clean(&config);
        return 1;
    }
    // Minimal policy is anonymous and isolated. Lock down even the built-in address space.
    auto& access = config.accessControl;
    access.getUserRightsMask = [](auto...) -> UA_UInt32 { return 0; };
    access.getUserAccessLevel = [](auto...) -> UA_Byte { return UA_ACCESSLEVELMASK_READ; };
    access.getUserExecutable = [](auto...) -> UA_Boolean { return false; };
    access.getUserExecutableOnObject = [](auto...) -> UA_Boolean { return false; };
    access.allowAddNode = [](auto...) -> UA_Boolean { return false; };
    access.allowAddReference = [](auto...) -> UA_Boolean { return false; };
    access.allowDeleteNode = [](auto...) -> UA_Boolean { return false; };
    access.allowDeleteReference = [](auto...) -> UA_Boolean { return false; };

    const std::unique_ptr<UA_Server, decltype(&UA_Server_delete)> server(
        UA_Server_newWithConfig(&config), UA_Server_delete);
    if (!server)
        return 1;

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
            attr, source, nullptr, nullptr) != UA_STATUSCODE_GOOD
        || UA_Server_run_startup(server.get()) != UA_STATUSCODE_GOOD)
        return 1;

    std::atomic<bool> finish{false};
    std::thread loop([&] {
        while (!finish.load()) {
            UA_Server_run_iterate(server.get(), false);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });
    const bool ok = read_fixture(url, ns);
    finish.store(true);
    loop.join();
    UA_Server_run_shutdown(server.get());
    std::cout << (ok ? "Synthetic OPC UA Read passed (value, quality, timestamps, missing node)"
                     : "Synthetic OPC UA Read validation failed") << '\n';
    return ok ? 0 : 1;
}
