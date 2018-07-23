/* mqttbroker_service_impl.cpp

 Generated by phxrpc_pb2service from mqttbroker.proto

*/

#include "mqttbroker_service_impl.h"

#include "phxrpc/file.h"

#include "phxqueue_phxrpc/comm.h"
#include "phxqueue_phxrpc/plugin.h"

#include "event_loop_server.h"
#include "mqtt/mqtt_msg.h"
#include "mqtt/mqtt_packet_id.h"
#include "mqttbroker_mgr.h"
#include "mqttbroker_server_config.h"


using namespace phxqueue_phxrpc::mqttbroker;
using namespace std;


constexpr char *KEY_BROKER_TOPIC2CLIENT_PREFIX{"__broker__:topic2client:"};


MqttBrokerServiceImpl::MqttBrokerServiceImpl(ServiceArgs_t &app_args,
        phxrpc::UThreadEpollScheduler *const worker_uthread_scheduler,
        const uint64_t session_id)
        : args_(app_args),
          worker_uthread_scheduler_(worker_uthread_scheduler),
          session_id_(session_id), mgr_(new MqttBrokerMgr(args_.config)) {
}

MqttBrokerServiceImpl::~MqttBrokerServiceImpl() {
}

int MqttBrokerServiceImpl::PHXEcho(const google::protobuf::StringValue &req,
                                   google::protobuf::StringValue *resp) {
    resp->set_value(req.value());

    return 0;
}

int MqttBrokerServiceImpl::PhxHttpPublish(const phxqueue_phxrpc::mqttbroker::HttpPublishPb &req,
                                          phxqueue_phxrpc::mqttbroker::HttpPubackPb *resp) {
    // 1. check local session
    const auto sub_mqtt_session(args_.mqtt_session_mgr->GetByClientId(req.sub_client_id()));
    if (!sub_mqtt_session) {
        QLErr("GetByClientId err sub_client_id \"%s\"", req.sub_client_id().c_str());

        return -1;
    }

    // 2. publish to event_loop_server
    phxqueue_phxrpc::mqttbroker::MqttPublishPb mqtt_publish_pb;
    mqtt_publish_pb.CopyFrom(req.mqtt_publish());
    // mqtt-3.3.1-3: reset dup
    mqtt_publish_pb.set_dup(false);

    if (1 == mqtt_publish_pb.qos()) {
        uint16_t sub_packet_id{0};
        if (!args_.mqtt_packet_id_mgr->AllocPacketId(req.pub_client_id(),
                req.mqtt_publish().packet_identifier(), req.sub_client_id(), sub_packet_id)) {
            QLErr("sub_session_id %" PRIx64 " AllocPacketId err sub_client_id \"%s\"",
                  sub_mqtt_session->session_id, req.sub_client_id().c_str());

            return -1;
        }
        mqtt_publish_pb.set_packet_identifier(sub_packet_id);

        // ack_key = sub_client_id + sub_packet_id
        const string ack_key(req.sub_client_id() + ':' + to_string(sub_packet_id));
        void *data{nullptr};
        auto *mqtt_publish(new phxqueue_phxrpc::mqttbroker::MqttPublish);
        mqtt_publish->FromPb(mqtt_publish_pb);
        args_.server_mgr->SendAndWaitAck(sub_mqtt_session->session_id, mqtt_publish,
                                         worker_uthread_scheduler_, ack_key, data);

        phxqueue_phxrpc::mqttbroker::MqttPuback *puback{
                (phxqueue_phxrpc::mqttbroker::MqttPuback *)data};
        if (!puback) {
            QLErr("sub_session_id %" PRIx64 " server_mgr.SendAndWaitAck nullptr "
                  "qos %u ack_key \"%s\"", sub_mqtt_session->session_id,
                  req.mqtt_publish().qos(), ack_key.c_str());

            return -1;
        }

        QLInfo("sub_session_id %" PRIx64 " server_mgr.SendAndWaitAck ack_key \"%s\" qos %u",
               sub_mqtt_session->session_id, ack_key.c_str(), req.mqtt_publish().qos());

        int ret{puback->ToPb(resp->mutable_mqtt_puback())};

        delete puback;

        args_.mqtt_packet_id_mgr->ReleasePacketId(req.pub_client_id(),
                req.mqtt_publish().packet_identifier(), req.sub_client_id());

        if (0 != ret) {
            QLErr("ToPb err %d", ret);

            return ret;
        }
    } else {
        auto *mqtt_publish(new phxqueue_phxrpc::mqttbroker::MqttPublish);
        mqtt_publish->FromPb(mqtt_publish_pb);
        args_.server_mgr->Send(sub_mqtt_session->session_id, mqtt_publish);
        QLInfo("sub_session_id %" PRIx64 " server_mgr.Send sub_client_id \"%s\" qos %u",
               sub_mqtt_session->session_id, req.sub_client_id().c_str(),
               req.mqtt_publish().qos());
    }

    return 0;
}

int MqttBrokerServiceImpl::PhxMqttConnect(const phxqueue_phxrpc::mqttbroker::MqttConnectPb &req,
                                          phxqueue_phxrpc::mqttbroker::MqttConnackPb *resp) {
    // 1. init local session
    const auto old_mqtt_session(args_.mqtt_session_mgr->GetByClientId(
            req.client_identifier()));
    if (old_mqtt_session) {
        if (old_mqtt_session->session_id == session_id_) {
            // mqtt-3.1.0-2: disconnect current connection

            args_.mqtt_session_mgr->DestroyBySessionId(session_id_);
            args_.server_mgr->DestroySession(session_id_);

            QLErr("%s err session_id %" PRIx64 " client_id \"%s\"", __func__,
                  session_id_, req.client_identifier().c_str());

            return -1;
        } else {
            // mqtt-3.1.4-2: disconnect other connection with same client_id
            args_.mqtt_session_mgr->DestroyBySessionId(old_mqtt_session->session_id);
            args_.server_mgr->DestroySession(old_mqtt_session->session_id);

            QLInfo("%s disconnect session_id old %" PRIx64 " new %" PRIx64
                   " client_id \"%s\"", __func__, old_mqtt_session->session_id,
                   session_id_, req.client_identifier().c_str());
        }
    }

    // mqtt connect: set client_id and init
    const auto mqtt_session(args_.mqtt_session_mgr->Create(
            req.client_identifier(), session_id_));
    mqtt_session->keep_alive = req.keep_alive();
    mqtt_session->Heartbeat();

    // 2. init remote session
    phxqueue_phxrpc::mqttbroker::SessionPb session_pb;
    // TODO:
    //session_pb.set_session_ip();
    session_pb.set_session_id(session_id_);

    const auto &session_attribute(session_pb.mutable_session_attribute());
    session_attribute->set_clean_session(req.clean_session());
    session_attribute->set_user_name(req.user_name());
    session_attribute->set_password(req.password());
    session_attribute->set_will_flag(req.will_flag());
    session_attribute->set_will_qos(req.will_qos());
    session_attribute->set_will_retain(req.will_retain());
    session_attribute->set_will_topic(req.will_topic());
    session_attribute->set_will_message(req.will_message());

    // 3. set remote session
    phxqueue::comm::RetCode ret{mgr_->SetSessionByClientIdRemote(req.client_identifier(), -1, session_pb)};
    if (phxqueue::comm::RetCode::RET_OK != ret) {
        // destroy local session
        args_.mqtt_session_mgr->DestroyBySessionId(session_id_);
        args_.server_mgr->DestroySession(session_id_);

        QLErr("session_id %" PRIx64 " client_id \"%s\" SetSessionByClientIdRemote err %d",
              session_id_, req.client_identifier().c_str(),
              phxqueue::comm::as_integer(ret));

        return phxqueue::comm::as_integer(ret);
    }

    // 3. response
    resp->set_session_present(!req.clean_session());
    resp->set_connect_return_code(0u);

    QLInfo("session_id %" PRIx64 " client_id \"%s\" keep_alive %u expire_time_ms %llu now %llu",
           session_id_, req.client_identifier().c_str(),
           mqtt_session->keep_alive, mqtt_session->expire_time_ms(),
           phxrpc::Timer::GetSteadyClockMS());

    return 0;
}

int MqttBrokerServiceImpl::PhxMqttPublish(const phxqueue_phxrpc::mqttbroker::MqttPublishPb &req,
                                          google::protobuf::Empty *resp) {
    // 1. check local session
    MqttSession *mqtt_session{nullptr};
    phxqueue::comm::RetCode ret{CheckSession(mqtt_session)};
    if (phxqueue::comm::RetCode::RET_OK != ret) {
        QLErr("session_id %" PRIx64 " CheckSession err %d qos %u packet_id %d",
              session_id_, phxqueue::comm::as_integer(ret), req.qos(), req.packet_identifier());

        return phxqueue::comm::as_integer(ret);
    }

    // 2. get remote session
    //uint64_t version{0uLL};
    //phxqueue_phxrpc::mqttbroker::SessionPb session_pb;
    //ret = mgr_->GetSessionByClientIdRemote(mqtt_session->client_id, version, session_pb);
    //if (phxqueue::comm::RetCode::RET_OK != ret) {
    //    FinishSession();
    //    QLErr("session_id %" PRIx64 " GetSessionByClientIdRemote err %d qos %u packet_id %d",
    //          data_flow_args_->session_id, phxqueue::comm::as_integer(ret),
    //          req.qos(), req.packet_identifier());

    //    return phxqueue::comm::as_integer(ret);
    //}

    // 2. enqueue
    phxqueue_phxrpc::mqttbroker::HttpPublishPb http_publish_pb;
    http_publish_pb.set_pub_client_id(mqtt_session->client_id);
    http_publish_pb.mutable_mqtt_publish()->CopyFrom(req);
    ret = mgr_->EnqueueMessage(http_publish_pb);
    if (phxqueue::comm::RetCode::RET_OK != ret) {
        FinishSession();
        QLErr("session_id %" PRIx64 " GetSessionByClientIdRemote err %d qos %u packet_id %d",
              session_id_, phxqueue::comm::as_integer(ret),
              req.qos(), req.packet_identifier());

        return phxqueue::comm::as_integer(ret);
    }

    // 3. ack
    if (1 == req.qos()) {
        phxqueue_phxrpc::mqttbroker::MqttPubackPb puback_pb;
        puback_pb.set_packet_identifier(req.packet_identifier());
        auto *puback(new phxqueue_phxrpc::mqttbroker::MqttPuback);
        puback->FromPb(puback_pb);
        args_.server_mgr->Send(session_id_, (phxrpc::BaseResponse *)puback);
    }

    // TODO: remove
    if (isprint(req.data().at(req.data().size() - 1)) && isprint(req.data().at(0))) {
        QLInfo("session_id %" PRIx64 " client_id \"%s\" qos %u packet_id %d topic \"%s\" data \"%s\"",
               session_id_, mqtt_session->client_id.c_str(), req.qos(),
               req.packet_identifier(), req.topic_name().c_str(), req.data().c_str());
    } else {
        QLInfo("session_id %" PRIx64 " client_id \"%s\" qos %u packet_id %d topic \"%s\" data.size %zu",
               session_id_, mqtt_session->client_id.c_str(), req.qos(),
               req.packet_identifier(), req.topic_name().c_str(), req.data().size());
    }

    return 0;
}

int MqttBrokerServiceImpl::PhxMqttPuback(const phxqueue_phxrpc::mqttbroker::MqttPubackPb &req,
                                         google::protobuf::Empty *resp) {
    // 1. check local session
    MqttSession *mqtt_session{nullptr};
    phxqueue::comm::RetCode ret{CheckSession(mqtt_session)};
    if (phxqueue::comm::RetCode::RET_OK != ret) {
        QLErr("session_id %" PRIx64 " CheckSession err %d packet_id %d",
              session_id_, phxqueue::comm::as_integer(ret),
              req.packet_identifier());

        return phxqueue::comm::as_integer(ret);
    }

    // 2. puback to hsha_server
    auto *puback(new phxqueue_phxrpc::mqttbroker::MqttPuback);
    int ret2{puback->FromPb(req)};
    if (0 != ret2) {
        delete puback;
        QLErr("session_id %" PRIx64 " FromPb err %d packet_id %d",
              session_id_, ret2, req.packet_identifier());

        return ret2;
    }

    // ack_key = sub_client_id + sub_packet_id
    const string ack_key(mqtt_session->client_id + ':' + to_string(req.packet_identifier()));
    // forward puback and do not delete here
    ret2 = args_.server_mgr->Ack(ack_key, (void *)puback);
    if (0 != ret2) {
        QLErr("session_id %" PRIx64 " server_mgr.Ack err %d ack_key \"%s\"",
              session_id_, ret2, ack_key.c_str());

        return ret2;
    }

    QLInfo("session_id %" PRIx64 " packet_id %d",
           session_id_, req.packet_identifier());

    return ret2;
}

int MqttBrokerServiceImpl::PhxMqttPubrec(const phxqueue_phxrpc::mqttbroker::MqttPubrecPb &req,
                                         google::protobuf::Empty *resp) {
    return -1;
}

int MqttBrokerServiceImpl::PhxMqttPubrel(const phxqueue_phxrpc::mqttbroker::MqttPubrelPb &req,
                                         google::protobuf::Empty *resp) {
    return -1;
}

int MqttBrokerServiceImpl::PhxMqttPubcomp(const phxqueue_phxrpc::mqttbroker::MqttPubcompPb &req,
                                          google::protobuf::Empty *resp) {
    return -1;
}

int MqttBrokerServiceImpl::PhxMqttSubscribe(const phxqueue_phxrpc::mqttbroker::MqttSubscribePb &req,
                                            phxqueue_phxrpc::mqttbroker::MqttSubackPb *resp) {
    // 1. check local session
    MqttSession *mqtt_session{nullptr};
    phxqueue::comm::RetCode ret{CheckSession(mqtt_session)};
    if (phxqueue::comm::RetCode::RET_OK != ret) {
        QLErr("session_id %" PRIx64 " CheckSession err %d",
              session_id_, phxqueue::comm::as_integer(ret));

        return phxqueue::comm::as_integer(ret);
    }

    // 2. get retmote session
    uint64_t version{0uLL};
    phxqueue_phxrpc::mqttbroker::SessionPb session_pb;
    ret = mgr_->GetSessionByClientIdRemote(mqtt_session->client_id, version, session_pb);
    if (phxqueue::comm::RetCode::RET_OK != ret) {
        FinishSession();
        QLErr("session_id %" PRIx64 " GetSessionByClientIdRemote err %d packet_id %d",
              session_id_, phxqueue::comm::as_integer(ret),
              req.packet_identifier());

        return phxqueue::comm::as_integer(ret);
    }

    // 3. set topic_name -> client_id to lock
    vector<uint8_t> return_codes;
    return_codes.resize(req.topic_filters().size());
    for (int i{0}; req.topic_filters().size() > i; ++i) {

        // 3.1. get topic_name -> session_ids from lock
        uint64_t version{0uLL};
        string topic_pb_string;
        phxqueue::comm::RetCode ret{mgr_->GetStringRemote(string(KEY_BROKER_TOPIC2CLIENT_PREFIX),
                req.topic_filters(i), version, topic_pb_string)};
        phxqueue_phxrpc::mqttbroker::TopicPb topic_pb;
        if (phxqueue::comm::RetCode::RET_ERR_KEY_NOT_EXIST != ret) {
            if (phxqueue::comm::RetCode::RET_OK != ret) {
                FinishSession();
                QLErr("session_id %" PRIx64 " GetStringRemote err %d packet_id %d topic \"%s\"",
                      session_id_, phxqueue::comm::as_integer(ret),
                      req.packet_identifier(), req.topic_filters(i).c_str());

                return_codes.at(i) = 0x80;

                continue;
            }

            // 3.2. modify topic
            if (!topic_pb.ParseFromString(topic_pb_string)) {
                FinishSession();
                QLErr("session_id %" PRIx64 " ParseFromString err packet_id %d",
                      session_id_, req.packet_identifier());

                return_codes.at(i) = 0x80;

                continue;
            }
        }

        const auto &subscribe_pb(topic_pb.add_subscribes());
        subscribe_pb->set_client_identifier(mqtt_session->client_id);
        if (req.qoss().size() > i) {
            if (1 >= req.qoss(i)) {
                subscribe_pb->set_qos(req.qoss(i));
                return_codes.at(i) = req.qoss(i);
            } else {
                subscribe_pb->set_qos(1);
                return_codes.at(i) = 0x01;
            }
        } else {
            subscribe_pb->set_qos(0);
            return_codes.at(i) = 0x00;
        }

        topic_pb_string.clear();
        if (!topic_pb.SerializeToString(&topic_pb_string)) {
            FinishSession();
            QLErr("session_id %" PRIx64 " SerializeToString err packet_id %d",
                  session_id_, req.packet_identifier());

            return_codes.at(i) = 0x80;

            continue;
        }

        // 3.3. set topic_name -> session_ids to lock
        ret = mgr_->SetStringRemote(string(KEY_BROKER_TOPIC2CLIENT_PREFIX),
                                    req.topic_filters(i), version, move(topic_pb_string));
        if (phxqueue::comm::RetCode::RET_OK != ret) {
            FinishSession();
            QLErr("session_id %" PRIx64 " SetStringRemote err %d packet_id %d topic \"%s\"",
                  session_id_, phxqueue::comm::as_integer(ret), req.packet_identifier(),
                  req.topic_filters(i).c_str());

            return_codes.at(i) = 0x80;

            continue;
        }

    }  // foreach req.topic_filters()

    // 4. response
    resp->set_packet_identifier(req.packet_identifier());
    resp->clear_return_codes();
    for (int i{0}; req.topic_filters().size() > i; ++i) {
        resp->add_return_codes(return_codes.at(i));
        QLVerb("topic \"%s\" return_code %x", req.topic_filters(i).c_str(), return_codes.at(i));
    }

    QLInfo("session_id %" PRIx64 " packet_id %d",
           session_id_, req.packet_identifier());

    return 0;
}

int MqttBrokerServiceImpl::PhxMqttUnsubscribe(const phxqueue_phxrpc::mqttbroker::MqttUnsubscribePb &req,
                                              phxqueue_phxrpc::mqttbroker::MqttUnsubackPb *resp) {
    // 1. check local session
    MqttSession *mqtt_session{nullptr};
    phxqueue::comm::RetCode ret{CheckSession(mqtt_session)};
    if (phxqueue::comm::RetCode::RET_OK != ret) {
        QLErr("session_id %" PRIx64 " CheckSession err %d",
              session_id_, phxqueue::comm::as_integer(ret));

        return phxqueue::comm::as_integer(ret);
    }

    // 2. get remote session
    uint64_t version{0uLL};
    phxqueue_phxrpc::mqttbroker::SessionPb session_pb;
    ret = mgr_->GetSessionByClientIdRemote(mqtt_session->client_id, version, session_pb);
    if (phxqueue::comm::RetCode::RET_OK != ret) {
        FinishSession();
        QLErr("session_id %" PRIx64 " GetSessionByClientIdRemote err %d packet_id %d",
              session_id_, phxqueue::comm::as_integer(ret),
              req.packet_identifier());

        return phxqueue::comm::as_integer(ret);
    }

    // 3. remove topic_name -> client_id from lock
    for (int i{0}; req.topic_filters().size() > i; ++i) {

        // 3.1. get topic_name -> session_ids from lock
        uint64_t version{0uLL};
        string topic_pb_string;
        phxqueue::comm::RetCode ret{mgr_->GetStringRemote(string(KEY_BROKER_TOPIC2CLIENT_PREFIX),
                req.topic_filters(i), version, topic_pb_string)};
        phxqueue_phxrpc::mqttbroker::TopicPb topic_pb_new;
        if (phxqueue::comm::RetCode::RET_ERR_KEY_NOT_EXIST != ret) {
            if (phxqueue::comm::RetCode::RET_OK != ret) {
                FinishSession();
                QLErr("session_id %" PRIx64 " GetStringRemote err %d packet_id %d topic \"%s\"",
                      session_id_, phxqueue::comm::as_integer(ret),
                      req.packet_identifier(), req.topic_filters(i).c_str());

                return phxqueue::comm::as_integer(ret);
            }

            // 3.2. modify topic
            phxqueue_phxrpc::mqttbroker::TopicPb topic_pb_old;
            if (!topic_pb_old.ParseFromString(topic_pb_string)) {
                FinishSession();
                QLErr("session_id %" PRIx64 " ParseFromString err packet_id %d topic \"%s\"",
                      session_id_, req.packet_identifier(), req.topic_filters(i).c_str());

                return -1;
            }

            for_each (topic_pb_old.subscribes().cbegin(), topic_pb_old.subscribes().cend(),
                    [&](const phxqueue_phxrpc::mqttbroker::SubscribePb &subscribe_pb) {
                        if (subscribe_pb.client_identifier() != mqtt_session->client_id) {
                            const auto &new_subscribe(topic_pb_new.add_subscribes());
                            new_subscribe->CopyFrom(subscribe_pb);
                        }
                    });
        }

        topic_pb_string.clear();
        if (!topic_pb_new.SerializeToString(&topic_pb_string)) {
            FinishSession();
            QLErr("session_id %" PRIx64 " SerializeToString err packet_id %d topic \"%s\"",
                  session_id_, req.packet_identifier(), req.topic_filters(i).c_str());

            return -1;
        }

        // 3.3. set topic_name -> session_ids to lock
        ret = mgr_->SetStringRemote(string(KEY_BROKER_TOPIC2CLIENT_PREFIX),
                                    req.topic_filters(i), version, move(topic_pb_string));
        if (phxqueue::comm::RetCode::RET_OK != ret) {
            FinishSession();
            QLErr("session_id %" PRIx64 " SetStringRemote err %d packet_id %d topic \"%s\"",
                  session_id_, phxqueue::comm::as_integer(ret),
                  req.packet_identifier(), req.topic_filters(i).c_str());

            return phxqueue::comm::as_integer(ret);
        }

    }  // foreach req.topic_filters()

    // 4. response
    resp->set_packet_identifier(req.packet_identifier());
    for (int i{0}; req.topic_filters().size() > i; ++i) {
        QLVerb("topic \"%s\"", req.topic_filters(i).c_str());
    }

    QLInfo("session_id %" PRIx64 " packet_id %d", session_id_, req.packet_identifier());

    return 0;
}

int MqttBrokerServiceImpl::PhxMqttPing(const phxqueue_phxrpc::mqttbroker::MqttPingreqPb &req,
                                       phxqueue_phxrpc::mqttbroker::MqttPingrespPb *resp) {
    MqttSession *mqtt_session{nullptr};
    phxqueue::comm::RetCode ret{CheckSession(mqtt_session)};
    if (phxqueue::comm::RetCode::RET_OK != ret) {
        QLErr("session_id %" PRIx64 " CheckSession err %d",
              session_id_, phxqueue::comm::as_integer(ret));

        return phxqueue::comm::as_integer(ret);
    }

    mqtt_session->Heartbeat();

    QLInfo("session_id %" PRIx64, session_id_);

    return 0;
}

int MqttBrokerServiceImpl::PhxMqttDisconnect(const phxqueue_phxrpc::mqttbroker::MqttDisconnectPb &req,
                                             google::protobuf::Empty *resp) {
    FinishSession();

    QLInfo("session_id %" PRIx64, session_id_);

    return 0;
}

phxqueue::comm::RetCode MqttBrokerServiceImpl::CheckSession(MqttSession *&mqtt_session) {
    const auto tmp_mqtt_session(args_.mqtt_session_mgr->GetBySessionId(session_id_));
    if (!tmp_mqtt_session || tmp_mqtt_session->IsExpired()) {
        QLErr("session_id %" PRIx64 " session %p mqtt_session %p", session_id_, tmp_mqtt_session);
        // ignore return
        FinishSession();

        return phxqueue::comm::RetCode::RET_ERR_LOGIC;
    }
    if (tmp_mqtt_session->IsExpired()) {
        QLErr("session_id %" PRIx64 " mqtt_session expire_time_ms %llu <= now %llu client_id %s",
              session_id_, tmp_mqtt_session->expire_time_ms(),
              phxrpc::Timer::GetSteadyClockMS(), tmp_mqtt_session->client_id.c_str());
        // ignore return
        FinishSession();

        return phxqueue::comm::RetCode::RET_ERR_LOGIC;
    }
    mqtt_session = tmp_mqtt_session;
    QLVerb("session_id %" PRIx64 " client_id %s",
           session_id_, mqtt_session->client_id.c_str());

    return phxqueue::comm::RetCode::RET_OK;
}

phxqueue::comm::RetCode MqttBrokerServiceImpl::FinishSession() {
    // get client_id
    const auto mqtt_session(args_.mqtt_session_mgr->GetBySessionId(session_id_));
    string client_id(mqtt_session->client_id);

    // destroy local session
    args_.mqtt_session_mgr->DestroyBySessionId(session_id_);
    args_.server_mgr->DestroySession(session_id_);

    // get remote session
    uint64_t version{0uLL};
    phxqueue_phxrpc::mqttbroker::SessionPb session_pb;
    phxqueue::comm::RetCode ret{mgr_->GetSessionByClientIdRemote(client_id, version, session_pb)};
    if (phxqueue::comm::RetCode::RET_OK != ret) {
        QLErr("session_id %" PRIx64 " client_id \"%s\" GetSessionByClientIdRemote err %d",
              session_id_, client_id.c_str(), phxqueue::comm::as_integer(ret));

        return ret;
    }

    // finish remote session
    QLInfo("session_id %" PRIx64 " client_id \"%s\" FinishRemoteSession",
           session_id_, client_id.c_str());
    ret = mgr_->FinishRemoteSession(client_id, session_pb);
    if (phxqueue::comm::RetCode::RET_OK != ret) {
        QLErr("session_id %" PRIx64 " client_id \"%s\" FinishRemoteSession err %d",
              session_id_, client_id.c_str(), phxqueue::comm::as_integer(ret));

        return ret;
    }

    return phxqueue::comm::RetCode::RET_OK;
}

