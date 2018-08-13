/* search_client.h

 Generated by phxrpc_pb2client from search.proto

*/

#pragma once

#include "phxrpc/rpc.h"

#include "mqttbroker.pb.h"


class SearchClient {
  public:
    static bool Init(const char *config_file);

    static const char *GetPackageName();

    SearchClient();
    virtual ~SearchClient();

    // http protocol
    int PHXEcho(const google::protobuf::StringValue &req,
                google::protobuf::StringValue *resp);
    int PhxHttpPublish(const phxqueue_phxrpc::mqttbroker::HttpPublishPb &req,
                       phxqueue_phxrpc::mqttbroker::HttpPubackPb *resp);

    // mqtt protocol
    int PhxMqttConnect(const phxqueue_phxrpc::mqttbroker::MqttConnectPb &req,
                       phxqueue_phxrpc::mqttbroker::MqttConnackPb *resp);
    int PhxMqttPublish(const phxqueue_phxrpc::mqttbroker::MqttPublishPb &req,
                       google::protobuf::Empty *resp);
    int PhxMqttPuback(const phxqueue_phxrpc::mqttbroker::MqttPubackPb &req,
                      google::protobuf::Empty *resp);
    int PhxMqttPubrec(const phxqueue_phxrpc::mqttbroker::MqttPubrecPb &req,
                      google::protobuf::Empty *resp);
    int PhxMqttPubrel(const phxqueue_phxrpc::mqttbroker::MqttPubrelPb &req,
                      google::protobuf::Empty *resp);
    int PhxMqttPubcomp(const phxqueue_phxrpc::mqttbroker::MqttPubcompPb &req,
                       google::protobuf::Empty *resp);
    int PhxMqttSubscribe(const phxqueue_phxrpc::mqttbroker::MqttSubscribePb &req,
                         phxqueue_phxrpc::mqttbroker::MqttSubackPb *resp);
    int PhxMqttUnsubscribe(const phxqueue_phxrpc::mqttbroker::MqttUnsubscribePb &req,
                           phxqueue_phxrpc::mqttbroker::MqttUnsubackPb *resp);
    int PhxMqttPing(const phxqueue_phxrpc::mqttbroker::MqttPingreqPb &req,
                    phxqueue_phxrpc::mqttbroker::MqttPingrespPb *resp);
    int PhxMqttDisconnect(const phxqueue_phxrpc::mqttbroker::MqttDisconnectPb &req,
                          google::protobuf::Empty *resp);
};

