/* mqttbroker_service_impl.cpp

 Generated by phxrpc_pb2service from mqttbroker.proto

*/

#include "mqttbroker_service_impl.h"

#include "phxrpc/file.h"

#include "../event_loop_server.h"
#include "../mqtt/mqtt_msg.h"
#include "../mqtt/mqtt_packet_id.h"
#include "../mqtt/mqtt_session.h"
#include "../mqttbroker_server_config.h"
#include "../server_mgr.h"


using namespace phxqueue_phxrpc::logic::mqtt;
using namespace phxqueue_phxrpc::mqttbroker;
using namespace std;


MqttBrokerServiceImpl::MqttBrokerServiceImpl(ServiceArgs_t &app_args,
        phxrpc::UThreadEpollScheduler *const worker_uthread_scheduler,
        const uint64_t session_id)
        : args_(app_args),
          worker_uthread_scheduler_(worker_uthread_scheduler),
          session_id_(session_id) {
}

MqttBrokerServiceImpl::~MqttBrokerServiceImpl() {
}

int MqttBrokerServiceImpl::PHXEcho(const google::protobuf::StringValue &req,
                                   google::protobuf::StringValue *resp) {
    resp->set_value(req.value());

    return 0;
}

int MqttBrokerServiceImpl::HttpPublish(const HttpPublishPb &req, HttpPubackPb *resp) {
    // 1. check local session
    auto sub_mqtt_session(MqttSessionMgr::GetInstance()->GetByClientId(req.sub_client_id()));
    if (!sub_mqtt_session) {
        phxrpc::log(LOG_ERR, "%s GetByClientId err sub_client_id \"%s\"",
                    __func__, req.sub_client_id().c_str());

        return -1;
    }

    // 2. publish to event_loop_server
    MqttPublishPb mqtt_publish_pb;
    mqtt_publish_pb.CopyFrom(req.mqtt_publish());
    // mqtt-3.3.1-3: reset dup
    mqtt_publish_pb.set_dup(false);

    if (1 == mqtt_publish_pb.qos()) {
        uint16_t sub_packet_id{0};
        if (!MqttPacketIdMgr::GetInstance()->AllocPacketId(0uLL, req.pub_client_id(),
                req.mqtt_publish().packet_identifier(), req.sub_client_id(), &sub_packet_id)) {
            phxrpc::log(LOG_ERR, "%s sub_session_id %" PRIx64 " AllocPacketId err sub_client_id \"%s\"",
                        __func__, sub_mqtt_session->session_id, req.sub_client_id().c_str());

            return -1;
        }
        mqtt_publish_pb.set_packet_identifier(sub_packet_id);

        // ack_key = sub_client_id + sub_packet_id
        const string ack_key(req.sub_client_id() + ':' + to_string(sub_packet_id));
        void *data{nullptr};
        auto *mqtt_publish(new phxqueue_phxrpc::mqttbroker::MqttPublish);
        mqtt_publish->FromPb(mqtt_publish_pb);
        ServerMgr::GetInstance()->SendAndWaitAck(sub_mqtt_session->session_id, mqtt_publish,
                                                 worker_uthread_scheduler_, ack_key, data);

        phxqueue_phxrpc::mqttbroker::MqttPuback *puback{
                (phxqueue_phxrpc::mqttbroker::MqttPuback *)data};
        if (!puback) {
            phxrpc::log(LOG_ERR, "%s sub_session_id %" PRIx64 " server_mgr.SendAndWaitAck nullptr "
                        "qos %u ack_key \"%s\"", __func__, sub_mqtt_session->session_id,
                        req.mqtt_publish().qos(), ack_key.c_str());

            return -1;
        }

        phxrpc::log(LOG_NOTICE, "%s sub_session_id %" PRIx64
                    " server_mgr.SendAndWaitAck ack_key \"%s\" qos %u",
                    __func__, sub_mqtt_session->session_id,
                    ack_key.c_str(), req.mqtt_publish().qos());

        int ret{puback->ToPb(resp->mutable_mqtt_puback())};

        delete puback;
        puback = nullptr;

        MqttPacketIdMgr::GetInstance()->ReleasePacketId(req.sub_client_id(), sub_packet_id);

        if (0 != ret) {
            phxrpc::log(LOG_ERR, "%s ToPb err %d", __func__, ret);

            return ret;
        }
    } else {
        auto *mqtt_publish(new phxqueue_phxrpc::mqttbroker::MqttPublish);
        mqtt_publish->FromPb(mqtt_publish_pb);
        ServerMgr::GetInstance()->Send(sub_mqtt_session->session_id, mqtt_publish);
        phxrpc::log(LOG_NOTICE, "%s sub_session_id %" PRIx64
                    " server_mgr.Send sub_client_id \"%s\" qos %u",
                    __func__, sub_mqtt_session->session_id, req.sub_client_id().c_str(),
                    req.mqtt_publish().qos());
    }

    return 0;
}

int MqttBrokerServiceImpl::MqttConnect(const MqttConnectPb &req,
                                       google::protobuf::Empty *resp) {
    const auto old_mqtt_session(MqttSessionMgr::GetInstance()->
                                GetByClientId(req.client_identifier()));
    if (old_mqtt_session) {
        if (old_mqtt_session->session_id == session_id_) {
            // mqtt-3.1.0-2: disconnect current connection

            MqttSessionMgr::GetInstance()->DestroyBySessionId(session_id_);
            ServerMgr::GetInstance()->DestroySession(session_id_);

            phxrpc::log(LOG_ERR, "%s err session_id %" PRIx64 " client_id \"%s\"", __func__,
                        session_id_, req.client_identifier().c_str());

            return 0;
        } else {
            // mqtt-3.1.4-2: disconnect other connection with same client_id
            MqttSessionMgr::GetInstance()->DestroyBySessionId(old_mqtt_session->session_id);
            ServerMgr::GetInstance()->DestroySession(old_mqtt_session->session_id);

            phxrpc::log(LOG_NOTICE, "%s disconnect session_id old %" PRIx64 " new %" PRIx64
                        " client_id \"%s\"", __func__, old_mqtt_session->session_id,
                        session_id_, req.client_identifier().c_str());
        }
    }

    // mqtt connect: set client_id and init
    const auto mqtt_session(MqttSessionMgr::GetInstance()->
                            Create(req.client_identifier(), session_id_));
    mqtt_session->keep_alive = req.keep_alive();
    mqtt_session->Heartbeat();

    MqttConnackPb connack_pb;
    connack_pb.set_session_present(!req.clean_session());
    connack_pb.set_connect_return_code(0u);

    auto *connack(new phxqueue_phxrpc::mqttbroker::MqttConnack);
    connack->FromPb(connack_pb);
    int ret2{ServerMgr::GetInstance()->Send(session_id_, (phxrpc::BaseResponse *)connack)};
    // TODO: check ret
    if (0 != ret2) {
        phxrpc::log(LOG_ERR, "%s session_id %" PRIx64 " Send err %d",
                    __func__, session_id_, ret2);
    }

    phxrpc::log(LOG_NOTICE, "%s session_id %" PRIx64 " client_id \"%s\"", __func__,
                session_id_, req.client_identifier().c_str());

    return 0;
}

int MqttBrokerServiceImpl::MqttPublish(const MqttPublishPb &req,
                                       google::protobuf::Empty *resp) {
    // 1. check local session
    MqttSession *mqtt_session{nullptr};
    int ret{CheckSession(mqtt_session)};
    if (0 != ret) {
        phxrpc::log(LOG_ERR, "%s session_id %" PRIx64 " CheckSession err %d qos %u packet_id %d",
                    __func__, session_id_, ret, req.qos(), req.packet_identifier());

        return 0;
    }

    // 2. ack
    if (1 == req.qos()) {
        MqttPubackPb puback_pb;
        puback_pb.set_packet_identifier(req.packet_identifier());
        auto *puback(new phxqueue_phxrpc::mqttbroker::MqttPuback);
        puback->FromPb(puback_pb);
        ServerMgr::GetInstance()->Send(session_id_, (phxrpc::BaseResponse *)puback);
    }

    // TODO: remove
    if (isprint(req.data().at(req.data().size() - 1)) && isprint(req.data().at(0))) {
        phxrpc::log(LOG_NOTICE, "%s session_id %" PRIx64 " client_id \"%s\" qos %u "
                    "packet_id %d topic \"%s\" data \"%s\"",
                    __func__, session_id_, mqtt_session->client_id.c_str(), req.qos(),
                    req.packet_identifier(), req.topic_name().c_str(), req.data().c_str());
    } else {
        phxrpc::log(LOG_NOTICE, "%s session_id %" PRIx64 " client_id \"%s\" qos %u "
                    "packet_id %d topic \"%s\" data.size %zu",
                    __func__, session_id_, mqtt_session->client_id.c_str(), req.qos(),
                    req.packet_identifier(), req.topic_name().c_str(), req.data().size());
    }

    return 0;
}

int MqttBrokerServiceImpl::MqttPuback(const MqttPubackPb &req,
                                      google::protobuf::Empty *resp) {
    // 1. check local session
    MqttSession *mqtt_session{nullptr};
    int ret{CheckSession(mqtt_session)};
    if (0 != ret) {
        phxrpc::log(LOG_ERR, "%s session_id %" PRIx64 " CheckSession err %d packet_id %d",
                    __func__, session_id_, ret, req.packet_identifier());

        return 0;
    }

    // 2. puback to hsha_server
    auto *puback(new phxqueue_phxrpc::mqttbroker::MqttPuback);
    ret = puback->FromPb(req);
    if (0 != ret) {
        delete puback;
        puback = nullptr;
        phxrpc::log(LOG_ERR, "%s session_id %" PRIx64 " FromPb err %d packet_id %d",
                    __func__, session_id_, ret, req.packet_identifier());

        return 0;
    }

    // ack_key = sub_client_id + sub_packet_id
    const string ack_key(mqtt_session->client_id + ':' + to_string(req.packet_identifier()));
    // forward puback and do not delete here
    int ret2{ServerMgr::GetInstance()->Ack(ack_key, (void *)puback)};
    if (0 != ret2) {
        phxrpc::log(LOG_ERR, "%s session_id %" PRIx64 " server_mgr.Ack err %d "
                    "ack_key \"%s\"", __func__, session_id_,
                    ret2, ack_key.c_str());

        return 0;
    }

    phxrpc::log(LOG_NOTICE, "%s session_id %" PRIx64 " packet_id %d",
                __func__, session_id_, req.packet_identifier());

    return 0;
}

int MqttBrokerServiceImpl::MqttPubrec(const MqttPubrecPb &req,
                                      google::protobuf::Empty *resp) {
    return -1;
}

int MqttBrokerServiceImpl::MqttPubrel(const MqttPubrelPb &req,
                                      google::protobuf::Empty *resp) {
    return -1;
}

int MqttBrokerServiceImpl::MqttPubcomp(const MqttPubcompPb &req,
                                       google::protobuf::Empty *resp) {
    return -1;
}

int MqttBrokerServiceImpl::MqttSubscribe(const MqttSubscribePb &req,
                                         google::protobuf::Empty *resp) {
    const auto mqtt_session(MqttSessionMgr::GetInstance()->GetBySessionId(session_id_));
    mqtt_session->Heartbeat();

    MqttSubackPb suback_pb;
    suback_pb.set_packet_identifier(req.packet_identifier());
    for (int i{0}; req.topic_filters().size() > i; ++i) {
        suback_pb.add_return_codes(0x00);
        phxrpc::log(LOG_DEBUG, "topic \"%s\"", req.topic_filters(i).c_str());
    }

    auto *suback(new phxqueue_phxrpc::mqttbroker::MqttSuback);
    suback->FromPb(suback_pb);
    int ret2{ServerMgr::GetInstance()->Send(session_id_, (phxrpc::BaseResponse *)suback)};
    // TODO: check ret
    if (0 != ret2) {
        phxrpc::log(LOG_ERR, "%s session_id %" PRIx64 " Send err %d",
                    __func__, session_id_, ret2);
    }

    phxrpc::log(LOG_NOTICE, "%s session_id %" PRIx64 " packet_id %d", __func__,
                session_id_, req.packet_identifier());

    return 0;
}

int MqttBrokerServiceImpl::MqttUnsubscribe(const MqttUnsubscribePb &req,
                                           google::protobuf::Empty *resp) {
    const auto mqtt_session(MqttSessionMgr::GetInstance()->GetBySessionId(session_id_));
    mqtt_session->Heartbeat();

    MqttUnsubackPb unsuback_pb;
    unsuback_pb.set_packet_identifier(req.packet_identifier());
    for (int i{0}; req.topic_filters().size() > i; ++i) {
        phxrpc::log(LOG_DEBUG, "topic \"%s\"", req.topic_filters(i).c_str());
    }

    auto *unsuback(new phxqueue_phxrpc::mqttbroker::MqttUnsuback);
    unsuback->FromPb(unsuback_pb);
    int ret2{ServerMgr::GetInstance()->Send(session_id_, (phxrpc::BaseResponse *)unsuback)};
    // TODO: check ret
    if (0 != ret2) {
        phxrpc::log(LOG_ERR, "%s session_id %" PRIx64 " Send err %d",
                    __func__, session_id_, ret2);
    }

    phxrpc::log(LOG_NOTICE, "%s session_id %" PRIx64 " packet_id %d",
                __func__, session_id_, req.packet_identifier());

    return 0;
}

int MqttBrokerServiceImpl::MqttPing(const MqttPingreqPb &req,
                                    google::protobuf::Empty *resp) {
    const auto mqtt_session(MqttSessionMgr::GetInstance()->GetBySessionId(session_id_));
    mqtt_session->Heartbeat();

    MqttPingrespPb pingresp_pb;
    auto *pingresp(new phxqueue_phxrpc::mqttbroker::MqttPingresp);
    pingresp->FromPb(pingresp_pb);
    int ret2{ServerMgr::GetInstance()->Send(session_id_, (phxrpc::BaseResponse *)pingresp)};
    // TODO: check ret
    if (0 != ret2) {
        phxrpc::log(LOG_ERR, "%s session_id %" PRIx64 " Send err %d",
                    __func__, session_id_, ret2);
    }

    phxrpc::log(LOG_NOTICE, "%s session_id %" PRIx64, __func__, session_id_);

    return 0;
}

int MqttBrokerServiceImpl::MqttDisconnect(const MqttDisconnectPb &req,
                                          google::protobuf::Empty *resp) {
    MqttSessionMgr::GetInstance()->DestroyBySessionId(session_id_);
    ServerMgr::GetInstance()->DestroySession(session_id_);

    phxrpc::log(LOG_NOTICE, "%s session_id %" PRIx64, __func__, session_id_);

    return 0;
}

int MqttBrokerServiceImpl::CheckSession(MqttSession *&mqtt_session) {
    const auto tmp_mqtt_session(MqttSessionMgr::GetInstance()->GetBySessionId(session_id_));
    if (!tmp_mqtt_session || tmp_mqtt_session->IsExpired()) {
        // ignore return

        // destroy local session
        MqttSessionMgr::GetInstance()->DestroyBySessionId(session_id_);
        ServerMgr::GetInstance()->DestroySession(session_id_);

        return -1;
    }
    mqtt_session = tmp_mqtt_session;

    return 0;
}

