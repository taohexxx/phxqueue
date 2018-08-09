/* mqttbroker_service_impl.cpp

 Generated by phxrpc_pb2service from mqttbroker.proto

*/

#include "mqttbroker_service_impl.h"

#include "phxqueue_phxrpc/comm.h"
#include "phxqueue_phxrpc/plugin.h"
#include "phxrpc/file.h"

#include "event_loop_server.h"
#include "mqtt/mqtt_msg.h"
#include "mqtt/mqtt_session.h"
#include "mqttbroker_logic.h"
#include "mqttbroker_server_config.h"
#include "publish/publish_memory.h"
#include "server_mgr.h"


using namespace phxqueue_phxrpc::logic::mqtt;
using namespace phxqueue_phxrpc::mqttbroker;
using namespace std;


MqttBrokerServiceImpl::MqttBrokerServiceImpl(ServiceArgs_t &app_args,
        phxrpc::UThreadEpollScheduler *const worker_uthread_scheduler,
        const uint64_t session_id)
        : args_(app_args), worker_uthread_scheduler_(worker_uthread_scheduler),
          session_id_(session_id),
          table_mgr_(new TableMgr(args_.config->topic_id())) {
}

MqttBrokerServiceImpl::~MqttBrokerServiceImpl() {
}

int MqttBrokerServiceImpl::PHXEcho(const google::protobuf::StringValue &req,
                                   google::protobuf::StringValue *resp) {
    resp->set_value(req.value());

    return 0;
}

int MqttBrokerServiceImpl::HttpPublish(const HttpPublishPb &req, HttpPubackPb *resp) {
    int ret{PublishQueue::GetInstance()->
            push_back(req.cursor_id(), req)};
    if (0 != ret) {
        QLErr("err %d pub_client_id \"%s\" packet_id %u", ret,
              req.pub_client_id().c_str(), req.mqtt_publish().packet_identifier());

        return ret;
    }

    auto mqtt_puback(resp->mutable_mqtt_puback());
    mqtt_puback->set_packet_identifier(req.mqtt_publish().packet_identifier());

    return 0;
}

int MqttBrokerServiceImpl::MqttConnect(const MqttConnectPb &req, MqttConnackPb *resp) {
    // 1. init local session
    const auto old_mqtt_session(MqttSessionMgr::GetInstance()->
                                GetByClientId(req.client_identifier()));
    if (old_mqtt_session) {
        if (old_mqtt_session->session_id == session_id_) {
            // mqtt-3.1.0-2: disconnect current connection

            MqttSessionMgr::GetInstance()->DestroyBySessionId(session_id_);
            args_.server_mgr->DestroySession(session_id_);

            QLErr("err session_id %" PRIx64 " client_id \"%s\"",
                  session_id_, req.client_identifier().c_str());

            return -1;
        } else {
            // mqtt-3.1.4-2: disconnect other connection with same client_id
            MqttSessionMgr::GetInstance()->DestroyBySessionId(old_mqtt_session->session_id);
            args_.server_mgr->DestroySession(old_mqtt_session->session_id);

            QLInfo("disconnect session_id old %" PRIx64 " new %" PRIx64 " client_id \"%s\"",
                   old_mqtt_session->session_id, session_id_, req.client_identifier().c_str());
        }
    }

    // mqtt connect: set client_id and init
    const auto mqtt_session(MqttSessionMgr::GetInstance()->
                            Create(req.client_identifier(), session_id_));
    mqtt_session->keep_alive = req.keep_alive();
    mqtt_session->Heartbeat();

    // 2. init remote session
    SessionPb session_pb;
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
    phxqueue::comm::RetCode ret{table_mgr_->
            SetSessionByClientIdRemote(req.client_identifier(), -1, session_pb)};
    if (phxqueue::comm::RetCode::RET_OK != ret) {
        // destroy local session
        MqttSessionMgr::GetInstance()->DestroyBySessionId(session_id_);
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

int MqttBrokerServiceImpl::MqttPublish(const MqttPublishPb &req,
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
    //SessionPb session_pb;
    //ret = table_mgr_->GetSessionByClientIdRemote(mqtt_session->client_id, version, session_pb);
    //if (phxqueue::comm::RetCode::RET_OK != ret) {
    //    FinishSession();
    //    QLErr("session_id %" PRIx64 " GetSessionByClientIdRemote err %d qos %u packet_id %d",
    //          data_flow_args_->session_id, phxqueue::comm::as_integer(ret),
    //          req.qos(), req.packet_identifier());

    //    return phxqueue::comm::as_integer(ret);
    //}

    // 2. enqueue
    HttpPublishPb http_publish_pb;
    http_publish_pb.set_pub_client_id(mqtt_session->client_id);
    http_publish_pb.mutable_mqtt_publish()->CopyFrom(req);
    ret = EnqueueMessage(http_publish_pb);
    if (phxqueue::comm::RetCode::RET_OK != ret) {
        FinishSession();
        QLErr("session_id %" PRIx64 " GetSessionByClientIdRemote err %d qos %u packet_id %d",
              session_id_, phxqueue::comm::as_integer(ret),
              req.qos(), req.packet_identifier());

        return phxqueue::comm::as_integer(ret);
    }

    // 3. ack
    if (1 == req.qos()) {
        MqttPubackPb puback_pb;
        puback_pb.set_packet_identifier(req.packet_identifier());
        auto *puback(new phxqueue_phxrpc::mqttbroker::MqttPuback);
        puback->FromPb(puback_pb);
        int ret{args_.server_mgr->Send(session_id_, (phxrpc::BaseResponse *)puback)};
        // TODO: check ret
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

int MqttBrokerServiceImpl::MqttPuback(const MqttPubackPb &req,
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

int MqttBrokerServiceImpl::MqttSubscribe(const MqttSubscribePb &req, MqttSubackPb *resp) {
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
    SessionPb session_pb;
    ret = table_mgr_->GetSessionByClientIdRemote(mqtt_session->client_id, version, session_pb);
    if (phxqueue::comm::RetCode::RET_OK != ret) {
        FinishSession();
        QLErr("session_id %" PRIx64 " GetSessionByClientIdRemote err %d packet_id %d",
              session_id_, phxqueue::comm::as_integer(ret), req.packet_identifier());

        return phxqueue::comm::as_integer(ret);
    }

    // 3. set topic_name -> client_id to lock
    vector<uint8_t> return_codes;
    return_codes.resize(req.topic_filters().size());
    for (auto &&return_code : return_codes) {
        return_code = 0x80;
    }
    for (int i{0}; req.topic_filters().size() > i; ++i) {

        // 3.1. get topic_name -> session_ids from lock
        uint64_t version{0uLL};
        TopicPb topic_pb;
        phxqueue::comm::RetCode ret{table_mgr_->GetTopicSubscribeRemote(req.topic_filters(i),
                                                                        &version, &topic_pb)};
        if (phxqueue::comm::RetCode::RET_OK != ret) {
            // mqtt-3.8.4-5
            return_codes.at(i) = 0x80;
            QLErr("session_id %" PRIx64 " GetTopicSubscribeRemote err %d "
                  "packet_id %d topic \"%s\"", session_id_, phxqueue::comm::as_integer(ret),
                  req.packet_identifier(), req.topic_filters(i).c_str());

            continue;
        }

        // 3.2. modify subscribe
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

        // 3.3. set topic_name -> session_ids to lock
        ret = table_mgr_->SetTopicSubscribeRemote(req.topic_filters(i), version, topic_pb);
        if (phxqueue::comm::RetCode::RET_OK != ret) {
            // mqtt-3.8.4-5
            return_codes.at(i) = 0x80;
            QLErr("session_id %" PRIx64 " SetTopicsubScribeRemote err %d "
                  "packet_id %d topic \"%s\"", session_id_, phxqueue::comm::as_integer(ret),
                  req.packet_identifier(), req.topic_filters(i).c_str());

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

    QLInfo("session_id %" PRIx64 " packet_id %d", session_id_, req.packet_identifier());

    return 0;
}

int MqttBrokerServiceImpl::MqttUnsubscribe(const MqttUnsubscribePb &req, MqttUnsubackPb *resp) {
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
    SessionPb session_pb;
    ret = table_mgr_->GetSessionByClientIdRemote(mqtt_session->client_id, version, session_pb);
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
        TopicPb topic_pb_old;
        phxqueue::comm::RetCode ret{table_mgr_->GetTopicSubscribeRemote(req.topic_filters(i),
                                                                        &version, &topic_pb_old)};
        if (phxqueue::comm::RetCode::RET_OK != ret) {
            // mqtt-3.10.4-1
            QLErr("session_id %" PRIx64 " GetTopicSubscribeRemote err %d "
                  "packet_id %d topic \"%s\"", session_id_, phxqueue::comm::as_integer(ret),
                  req.packet_identifier(), req.topic_filters(i).c_str());

            continue;
        }

        // 3.2. modify subscribe
        TopicPb topic_pb_new;
        for_each (topic_pb_old.subscribes().cbegin(), topic_pb_old.subscribes().cend(),
                [&](const SubscribePb &subscribe_pb) {
            if (subscribe_pb.client_identifier() != mqtt_session->client_id) {
                const auto &new_subscribe(topic_pb_new.add_subscribes());
                new_subscribe->CopyFrom(subscribe_pb);
            }
        });

        // 3.3. set topic_name -> session_ids to lock
        ret = table_mgr_->SetTopicSubscribeRemote(req.topic_filters(i), version, topic_pb_new);
        if (phxqueue::comm::RetCode::RET_OK != ret) {
            // mqtt-3.10.4-1
            QLErr("session_id %" PRIx64 " SetTopicSubscribeRemote err %d "
                  "packet_id %d topic \"%s\"", session_id_, phxqueue::comm::as_integer(ret),
                  req.packet_identifier(), req.topic_filters(i).c_str());

            continue;
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

int MqttBrokerServiceImpl::MqttPing(const MqttPingreqPb &req, MqttPingrespPb *resp) {
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

int MqttBrokerServiceImpl::MqttDisconnect(const MqttDisconnectPb &req,
                                          google::protobuf::Empty *resp) {
    FinishSession();

    QLInfo("session_id %" PRIx64, session_id_);

    return 0;
}

phxqueue::comm::RetCode
MqttBrokerServiceImpl::CheckSession(MqttSession *&mqtt_session) {
    const auto tmp_mqtt_session(MqttSessionMgr::GetInstance()->
                                GetBySessionId(session_id_));
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
    const auto mqtt_session(MqttSessionMgr::GetInstance()->
                            GetBySessionId(session_id_));
    string client_id(mqtt_session->client_id);

    // destroy local session
    MqttSessionMgr::GetInstance()->DestroyBySessionId(session_id_);
    args_.server_mgr->DestroySession(session_id_);

    // get remote session
    uint64_t version{0uLL};
    SessionPb session_pb;
    phxqueue::comm::RetCode ret{table_mgr_->
            GetSessionByClientIdRemote(client_id, version, session_pb)};
    if (phxqueue::comm::RetCode::RET_OK != ret) {
        QLErr("session_id %" PRIx64 " client_id \"%s\" GetSessionByClientIdRemote err %d",
              session_id_, client_id.c_str(), phxqueue::comm::as_integer(ret));

        return ret;
    }

    // finish remote session
    QLInfo("session_id %" PRIx64 " client_id \"%s\" FinishRemoteSession",
           session_id_, client_id.c_str());
    ret = table_mgr_->FinishRemoteSession(client_id, session_pb);
    if (phxqueue::comm::RetCode::RET_OK != ret) {
        QLErr("session_id %" PRIx64 " client_id \"%s\" FinishRemoteSession err %d",
              session_id_, client_id.c_str(), phxqueue::comm::as_integer(ret));

        return ret;
    }

    return phxqueue::comm::RetCode::RET_OK;
}

