/* phxrpc_search_tool.h

 Generated by phxrpc_pb2tool from search.proto

 Please DO NOT edit unless you know exactly what you are doing.

*/

#pragma once


namespace phxrpc {


class OptMap;


}


class SearchTool {
  public:
    SearchTool();
    virtual ~SearchTool();

    // http protocol
    virtual int PHXEcho(phxrpc::OptMap &bigmap);
    virtual int PhxHttpPublish(phxrpc::OptMap &bigmap);

    // mqtt protocol
    virtual int PhxMqttConnect(phxrpc::OptMap &bigmap);
    virtual int PhxMqttPublish(phxrpc::OptMap &bigmap);
    virtual int PhxMqttPuback(phxrpc::OptMap &bigmap);
    virtual int PhxMqttPubrec(phxrpc::OptMap &bigmap);
    virtual int PhxMqttPubrel(phxrpc::OptMap &bigmap);
    virtual int PhxMqttPubcomp(phxrpc::OptMap &bigmap);
    virtual int PhxMqttSubscribe(phxrpc::OptMap &bigmap);
    virtual int PhxMqttUnsubscribe(phxrpc::OptMap &bigmap);
    virtual int PhxMqttPing(phxrpc::OptMap &bigmap);
    virtual int PhxMqttDisconnect(phxrpc::OptMap &bigmap);

    typedef int (SearchTool::*ToolFunc_t)(phxrpc::OptMap &);

    typedef struct tagName2Func {
        const char *name;
        SearchTool::ToolFunc_t func;
        const char *opt_string;
        const char *usage;
    } Name2Func_t;

    static Name2Func_t *GetName2Func() {
        static Name2Func_t name2func[]{
            {"PHXEcho", &SearchTool::PHXEcho, "c:f:vs:",
                    "-s <string>"},
            {"PhxHttpPublish", &SearchTool::PhxHttpPublish, "c:f:vx:y:d:q:r:t:p:s:",
                    "-x <pub_client_id> -y <sub_client_id> -d <dup> -q <qos> -r <retain> -t <topic_name> -p <packet_identifier> -s <string>"},
            {"PhxMqttConnect", &SearchTool::PhxMqttConnect, "c:f:vl:",
                    "-l <client_identifier>"},
            {"PhxMqttPublish", &SearchTool::PhxMqttPublish, "c:f:vl:d:q:r:t:p:s:",
                    "-l <client_identifier> -d <dup> -q <qos> -r <retain> -t <topic_name> -p <packet_identifier> -s <string>"},
            {"PhxMqttPuback", &SearchTool::PhxMqttPuback, "c:f:vl:p:",
                    "-l <client_identifier> -p <packet_identifier>"},
            {"PhxMqttSubscribe", &SearchTool::PhxMqttSubscribe, "c:f:vl:p:t:q:",
                    "-l <client_identifier> -p <packet_identifier> -t <topic_filters> -q <qoss>"},
            {"PhxMqttUnsubscribe", &SearchTool::PhxMqttUnsubscribe, "c:f:vl:p:t:",
                    "-l <client_identifier> -p <packet_identifier> -t <topic_filters>"},
            {"PhxMqttPing", &SearchTool::PhxMqttPing, "c:f:vl:",
                    "-l <client_identifier>"},
            {"PhxMqttDisconnect", &SearchTool::PhxMqttDisconnect, "c:f:vl:",
                    "-l <client_identifier>"},
            {nullptr, nullptr}
        };

        return name2func;
    };
};

