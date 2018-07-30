/* mqttbroker_service_impl.h

 Generated by phxrpc_pb2service from mqttbroker.proto

*/

#pragma once

#include "phxrpc/network.h"

#include "phxqueue/comm.h"

#include "mqttbroker.pb.h"
#include "phxrpc_mqttbroker_service.h"


class MqttBrokerServerConfig;


namespace phxqueue_phxrpc {

namespace mqttbroker {


class MqttBrokerMgr;
class MqttPacketIdMgr;
class MqttSession;
class MqttSessionMgr;
class ServerMgr;


}  // namespace mqttbroker

}  // namespace phxqueue_phxrpc


typedef struct tagServiceArgs {
    const MqttBrokerServerConfig *config;
    phxqueue_phxrpc::mqttbroker::ServerMgr *server_mgr;
    phxqueue_phxrpc::mqttbroker::MqttSessionMgr *mqtt_session_mgr;
    phxqueue_phxrpc::mqttbroker::MqttPacketIdMgr *mqtt_packet_id_mgr;
} ServiceArgs_t;


class MqttBrokerServiceImpl : public MqttBrokerService {
  public:
    MqttBrokerServiceImpl(ServiceArgs_t &app_args,
            phxrpc::UThreadEpollScheduler *worker_uthread_scheduler,
            const uint64_t session_id);
    virtual ~MqttBrokerServiceImpl() override;

    virtual int PHXEcho(const google::protobuf::StringValue &req,
                        google::protobuf::StringValue *resp) override;
    virtual int PhxHttpPublish(const phxqueue_phxrpc::mqttbroker::HttpPublishPb &req,
                               phxqueue_phxrpc::mqttbroker::HttpPubackPb *resp) override;
    virtual int PhxMqttConnect(const phxqueue_phxrpc::mqttbroker::MqttConnectPb &req,
                               phxqueue_phxrpc::mqttbroker::MqttConnackPb *resp) override;
    virtual int PhxMqttPublish(const phxqueue_phxrpc::mqttbroker::MqttPublishPb &req,
                               google::protobuf::Empty *resp) override;
    virtual int PhxMqttPuback(const phxqueue_phxrpc::mqttbroker::MqttPubackPb &req,
                              google::protobuf::Empty *resp) override;
    virtual int PhxMqttPubrec(const phxqueue_phxrpc::mqttbroker::MqttPubrecPb &req,
                              google::protobuf::Empty *resp) override;
    virtual int PhxMqttPubrel(const phxqueue_phxrpc::mqttbroker::MqttPubrelPb &req,
                              google::protobuf::Empty *resp) override;
    virtual int PhxMqttPubcomp(const phxqueue_phxrpc::mqttbroker::MqttPubcompPb &req,
                               google::protobuf::Empty *resp) override;
    virtual int PhxMqttSubscribe(const phxqueue_phxrpc::mqttbroker::MqttSubscribePb &req,
                                 phxqueue_phxrpc::mqttbroker::MqttSubackPb *resp) override;
    virtual int PhxMqttUnsubscribe(const phxqueue_phxrpc::mqttbroker::MqttUnsubscribePb &req,
                                   phxqueue_phxrpc::mqttbroker::MqttUnsubackPb *resp) override;
    virtual int PhxMqttPing(const phxqueue_phxrpc::mqttbroker::MqttPingreqPb &req,
                            phxqueue_phxrpc::mqttbroker::MqttPingrespPb *resp) override;
    virtual int PhxMqttDisconnect(const phxqueue_phxrpc::mqttbroker::MqttDisconnectPb &req,
                                  google::protobuf::Empty *resp) override;

  private:
    phxqueue::comm::RetCode CheckSession(phxqueue_phxrpc::mqttbroker::MqttSession *&mqtt_session);
    phxqueue::comm::RetCode FinishSession();

    ServiceArgs_t &args_;
    phxrpc::UThreadEpollScheduler *worker_uthread_scheduler_{nullptr};
    uint64_t session_id_{0uLL};
    std::unique_ptr<phxqueue_phxrpc::mqttbroker::MqttBrokerMgr> mgr_;
};

