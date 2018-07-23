/*
Tencent is pleased to support the open source community by making PhxQueue available.
Copyright (C) 2017 THL A29 Limited, a Tencent company. All rights reserved.
Licensed under the BSD 3-Clause License (the "License"); you may not use this file except in compliance with the License. You may obtain a copy of the License at

<https://opensource.org/licenses/BSD-3-Clause>

Unless required by applicable law or agreed to in writing, software distributed under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the License for the specific language governing permissions and limitations under the License.
*/



#pragma once

#include "phxqueue/comm.h"

#include "mqttbroker.pb.h"
#include "mqtt/mqtt_session.h"


class MqttBrokerServerConfig;


namespace phxqueue_phxrpc {

namespace mqttbroker {


class MqttBrokerMgr {
  public:
    MqttBrokerMgr(const MqttBrokerServerConfig *const config);
    virtual ~MqttBrokerMgr();

    phxqueue::comm::RetCode
    FinishRemoteSession(const std::string &client_id,
                        const phxqueue_phxrpc::mqttbroker::SessionPb &session_pb);
    phxqueue::comm::RetCode
    EnqueueMessage(const phxqueue_phxrpc::mqttbroker::HttpPublishPb &message);

    phxqueue::comm::RetCode GetStringRemote(const std::string &prefix, const std::string &key,
                                            uint64_t &version, std::string &value);
    phxqueue::comm::RetCode SetStringRemote(const std::string &prefix, const std::string &key,
                                            const uint64_t version, const std::string &value);
    phxqueue::comm::RetCode DeleteStringRemote(const std::string &prefix, const std::string &key,
                                               const uint64_t version);
    phxqueue::comm::RetCode LockRemote(const std::string &lock_key,
                                       const std::string &client_id, const uint64_t lease_time);

    phxqueue::comm::RetCode GetSessionAndClientIdBySessionIdRemote(const uint64_t session_id,
            std::string &client_id, phxqueue_phxrpc::mqttbroker::SessionPb &session_pb);
    phxqueue::comm::RetCode GetSessionByClientIdRemote(const std::string &client_id,
            uint64_t &version, phxqueue_phxrpc::mqttbroker::SessionPb &session_pb);
    phxqueue::comm::RetCode SetSessionByClientIdRemote(const std::string &client_id,
            const uint64_t version, phxqueue_phxrpc::mqttbroker::SessionPb const &session_pb);
    phxqueue::comm::RetCode DeleteSessionByClientIdRemote(const std::string &client_id,
                                                          const uint64_t version);

    phxqueue::comm::RetCode
    GetLockInfo(const phxqueue::comm::proto::GetLockInfoRequest &req,
                phxqueue::comm::proto::GetLockInfoResponse &resp);
    phxqueue::comm::RetCode
    AcquireLock(const phxqueue::comm::proto::AcquireLockRequest &req,
                phxqueue::comm::proto::AcquireLockResponse &resp);
    phxqueue::comm::RetCode
    GetString(const phxqueue::comm::proto::GetStringRequest &req,
              phxqueue::comm::proto::GetStringResponse &resp);
    phxqueue::comm::RetCode
    SetString(const phxqueue::comm::proto::SetStringRequest &req,
              phxqueue::comm::proto::SetStringResponse &resp);
    phxqueue::comm::RetCode
    DeleteString(const phxqueue::comm::proto::DeleteStringRequest &req,
                 phxqueue::comm::proto::DeleteStringResponse &resp);

    phxqueue::comm::RetCode GetTopicIdAndLockId(int &topic_id, int &lock_id);

  private:
    const MqttBrokerServerConfig *config_{nullptr};
};


}  // namespace mqttbroker

}  // namespace phxqueue_phxrpc

