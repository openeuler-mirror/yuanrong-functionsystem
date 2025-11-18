/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include "instance_ctrl_message.h"

#include "common/utils/struct_transfer.h"
#include "common/metadata/metadata.h"

namespace functionsystem {
using namespace messages;
std::shared_ptr<DeployInstanceRequest> GetDeployInstanceReq(const FunctionMeta &funcMeta,
                                                            const std::shared_ptr<ScheduleRequest> &request)
{
    auto deployInstanceRequest = std::make_shared<DeployInstanceRequest>();
    deployInstanceRequest->set_instanceid(request->instance().instanceid());
    deployInstanceRequest->set_traceid(request->traceid());
    deployInstanceRequest->set_requestid(request->requestid());
    deployInstanceRequest->set_entryfile(funcMeta.funcMetaData.entryFile);
    deployInstanceRequest->set_envkey(funcMeta.envMetaData.envKey);
    deployInstanceRequest->set_envinfo(funcMeta.envMetaData.envInfo);
    deployInstanceRequest->set_encrypteduserdata(funcMeta.envMetaData.encryptedUserData);
    deployInstanceRequest->set_cryptoalgorithm(funcMeta.envMetaData.cryptoAlgorithm);
    deployInstanceRequest->set_language(funcMeta.funcMetaData.runtime);
    deployInstanceRequest->set_codesha512(funcMeta.funcMetaData.codeSha512);
    deployInstanceRequest->set_codesha256(funcMeta.funcMetaData.codeSha256);
    deployInstanceRequest->mutable_resources()->CopyFrom(request->instance().resources());
    BuildDeploySpec(funcMeta, deployInstanceRequest);
    for (auto &[key, handler] : funcMeta.funcMetaData.hookHandler) {
        deployInstanceRequest->mutable_hookhandler()->operator[](key) = handler;
    }

    if (funcMeta.funcMetaData.isSystemFunc) {
        deployInstanceRequest->set_instancelevel(SYSTEM_FUNCTION_INSTANCE_LEVEL);
    }

    if (auto requestOptions = request->instance().createoptions(); !requestOptions.empty()) {
        auto createOptions = deployInstanceRequest->mutable_createoptions();
        (void)createOptions->insert({ "S3_DEPLOY_DIR", GetDeployDir() });
        for (auto &pair : requestOptions) {
            (void)createOptions->insert({ pair.first, pair.second });
        }
    }
    deployInstanceRequest->mutable_scheduleoption()->set_schedpolicyname(
        request->instance().scheduleoption().schedpolicyname());
    auto mountUser = funcMeta.extendedMetaData.mountConfig.mountUser;
    auto config = deployInstanceRequest->mutable_funcmountconfig();
    config->mutable_funcmountuser()->set_userid(mountUser.userID);
    config->mutable_funcmountuser()->set_groupid(mountUser.groupID);
    for (auto &mount : funcMeta.extendedMetaData.mountConfig.funcMounts) {
        auto funcMountPtr = config->add_funcmounts();
        funcMountPtr->set_mounttype(mount.mountType);
        funcMountPtr->set_mountresource(mount.mountResource);
        funcMountPtr->set_mountsharepath(mount.mountSharePath);
        funcMountPtr->set_localmountpath(mount.localMountPath);
        funcMountPtr->set_status(mount.status);
    }
    deployInstanceRequest->set_gracefulshutdowntime(request->instance().gracefulshutdowntime());

    // for app driver
    if (auto createOpts = request->instance().createoptions(); IsAppDriver(request->instance().createoptions())) {
        if (createOpts.find(APP_ENTRYPOINT) != createOpts.end()) {
            deployInstanceRequest->set_entryfile(createOpts.find(APP_ENTRYPOINT)->second);
        }
        auto spec = deployInstanceRequest->mutable_funcdeployspec();
        spec->set_deploydir("");
        spec->set_storagetype(WORKING_DIR_STORAGE_TYPE);
    }
    return deployInstanceRequest;
}

void BuildDeploySpec(const FunctionMeta &funcMeta,
                     const std::shared_ptr<messages::DeployInstanceRequest> &deployInstanceRequest)
{
    auto spec = deployInstanceRequest->mutable_funcdeployspec();
    spec->set_bucketid(funcMeta.codeMetaData.bucketID);
    spec->set_objectid(funcMeta.codeMetaData.objectID);
    spec->set_bucketurl(funcMeta.codeMetaData.bucketUrl);
    for (auto &l : funcMeta.codeMetaData.layers) {
        messages::Layer layer;
        layer.set_appid(l.appID);
        layer.set_bucketid(l.bucketID);
        layer.set_objectid(l.objectID);
        layer.set_bucketurl(l.bucketURL);
        layer.set_sha256(l.sha256);
        spec->add_layers()->CopyFrom(layer);
    }
    spec->set_deploydir(funcMeta.codeMetaData.deployDir);
    spec->set_storagetype(funcMeta.codeMetaData.storageType);
}

std::shared_ptr<messages::StaticFunctionChangeRequest> GetStaticFunctionChangeRequest(const InstanceInfo &instanceInfo,
                                                                                      int32_t status)
{
    auto staticFunctionChangeRequest = std::make_shared<messages::StaticFunctionChangeRequest>();
    staticFunctionChangeRequest->set_instanceid(instanceInfo.instanceid());
    staticFunctionChangeRequest->set_requestid(instanceInfo.requestid());
    staticFunctionChangeRequest->set_status(status);
    return staticFunctionChangeRequest;
}
}  // namespace functionsystem
