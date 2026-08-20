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

#include "common/hex/hex.h"
#include "common/metadata/metadata.h"
#include "common/utils/resume_identity.h"
#include "common/utils/struct_transfer.h"

namespace functionsystem {
using namespace messages;

namespace {

Status ValidateReusableSnapshotRestore(
    const ::messages::ReusableSnapshotRestore &restore,
    const std::string &requestedSnapshotID)
{
    const auto &artifact = restore.artifact();
    if (requestedSnapshotID.empty() || restore.snapshotid() != requestedSnapshotID
        || !restore.allowlogicalinstanceidrebind() || !restore.has_artifact()
        || artifact.storagebackend().empty() || artifact.objectkey().empty()
        || artifact.size() <= 0 || artifact.sha256().size() != 64
        || artifact.format() != "gvisor-checkpoint" || artifact.formatversion() != 1) {
        return Status(StatusCode::ERR_PARAM_INVALID,
                      "resolved reusable Snapshot restore metadata is invalid");
    }
    return Status::OK();
}

Status MergeReusableSnapshotResources(
    const resources::Resources &source, resources::Resources *target)
{
    if (target == nullptr) {
        return Status(StatusCode::ERR_PARAM_INVALID,
                      "reusable Snapshot target resources are unavailable");
    }
    for (const auto &[name, sourceResource] : source.resources()) {
        auto targetResource = target->mutable_resources()->find(name);
        if (targetResource == target->mutable_resources()->end()) {
            (*target->mutable_resources())[name].CopyFrom(sourceResource);
            continue;
        }
        if (!sourceResource.has_scalar() || !targetResource->second.has_scalar()
            || targetResource->second.scalar().value() < sourceResource.scalar().value()) {
            return Status(StatusCode::ERR_PARAM_INVALID,
                          "Create-from-Snapshot cannot reduce or change a Snapshot resource");
        }
    }
    return Status::OK();
}

}  // namespace

Status ApplyResolvedReusableSnapshotForCreate(
    const ::messages::ResolveReusableSnapshotForCreateResponse &resolved,
    const std::shared_ptr<ScheduleRequest> &request)
{
    if (request == nullptr || resolved.code() != common::ERR_NONE
        || !resolved.has_instancetemplate() || !resolved.has_reusablesnapshotrestore()) {
        return Status(StatusCode::ERR_PARAM_INVALID,
                      "reusable Snapshot resolution is incomplete");
    }
    auto *target = request->mutable_instance();
    auto *scheduleExtensions = target->mutable_scheduleoption()->mutable_extension();
    const auto requested = scheduleExtensions->find(REUSABLE_SNAPSHOT_REQUESTED_ID_EXTENSION);
    if (requested == scheduleExtensions->end()) {
        return Status(StatusCode::ERR_PARAM_INVALID,
                      "Create request omitted the reusable Snapshot ID");
    }
    const auto restoreStatus = ValidateReusableSnapshotRestore(
        resolved.reusablesnapshotrestore(), requested->second);
    if (restoreStatus.IsError()) {
        return restoreStatus;
    }
    const auto &source = resolved.instancetemplate();
    if (source.function().empty()
        || (!target->function().empty() && target->function() != source.function())) {
        return Status(StatusCode::ERR_PARAM_INVALID,
                      "Create function does not match the reusable Snapshot template");
    }
    const auto resourceStatus = MergeReusableSnapshotResources(
        source.resources(), target->mutable_resources());
    if (resourceStatus.IsError()) {
        return resourceStatus;
    }

    // Keep the new logical identity, parent, naming and scheduling choices.
    // Workload/bootstrap inputs are frozen by the Snapshot template in v1.
    target->set_function(source.function());
    target->set_restartpolicy(source.restartpolicy());
    *target->mutable_createoptions() = source.createoptions();
    target->set_storagetype(source.storagetype());
    target->mutable_args()->CopyFrom(source.args());
    target->set_gracefulshutdowntime(source.gracefulshutdowntime());
    target->set_executortype(source.executortype());
    target->mutable_extensions()->erase("portForward");

    scheduleExtensions->erase(REUSABLE_SNAPSHOT_REQUESTED_ID_EXTENSION);
    (*scheduleExtensions)[REUSABLE_SNAPSHOT_TRUSTED_RESTORE_EXTENSION] =
        CharStringToHexString(resolved.reusablesnapshotrestore().SerializeAsString());
    return Status::OK();
}

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
    if (request->instance().has_snapshotinfo()) {
        deployInstanceRequest->mutable_snapshotinfo()->CopyFrom(request->instance().snapshotinfo());
    }
    const auto trustedRestore = request->instance().scheduleoption().extension().find(
        REUSABLE_SNAPSHOT_TRUSTED_RESTORE_EXTENSION);
    if (trustedRestore != request->instance().scheduleoption().extension().end()) {
        ::messages::ReusableSnapshotRestore restore;
        if (restore.ParseFromString(HexStringToCharString(trustedRestore->second))) {
            deployInstanceRequest->mutable_reusablesnapshotrestore()->CopyFrom(restore);
        }
    }
    BuildDeploySpec(funcMeta, deployInstanceRequest);
    BuildRootfsConfig(funcMeta, deployInstanceRequest);
    BuildBootstrapConfig(funcMeta, deployInstanceRequest);
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

    if (!funcMeta.sandboxType.empty()) {
        (*deployInstanceRequest->mutable_createoptions())["sandbox_type"] = funcMeta.sandboxType;
    }
    deployInstanceRequest->mutable_scheduleoption()->set_schedpolicyname(
        request->instance().scheduleoption().schedpolicyname());
    // 传递 NUMA 等 extension 到 runtime_manager（StartInstanceRequest 无 createoptions，需从 scheduleOption 读取）
    for (const auto& [k, v] : request->instance().scheduleoption().extension()) {
        if (resume_identity::IsReservedExtension(k)
            || k == REUSABLE_SNAPSHOT_REQUESTED_ID_EXTENSION
            || k == REUSABLE_SNAPSHOT_TRUSTED_RESTORE_EXTENSION) {
            continue;
        }
        (*deployInstanceRequest->mutable_scheduleoption()->mutable_extension())[k] = v;
    }
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

    if (request->instance().createoptions().find(RUNTIME_ENTRYPOINT) != request->instance().createoptions().end()) {
        deployInstanceRequest->set_entryfile(request->instance().createoptions().find(RUNTIME_ENTRYPOINT)->second);
    }

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

void BuildRootfsConfig(
    const FunctionMeta &funcMeta, const std::shared_ptr<DeployInstanceRequest> &deployInstanceRequest)
{
    if (funcMeta.rootfs.runtime.empty() || funcMeta.rootfs.type == RootfsSrcType::INVALID) {
        return;
    }
    auto container = deployInstanceRequest->mutable_container();
    container->set_id(std::to_string(
        std::hash<std::string>{}(funcMeta.funcMetaData.name + funcMeta.funcMetaData.revisionId)));
    container->set_runtime(funcMeta.rootfs.runtime);
    container->set_mountpoint(funcMeta.rootfs.mountpoint);
    container->mutable_rootfsconfig()->set_readonly(funcMeta.rootfs.readonly);
    container->mutable_rootfsconfig()->set_type(static_cast<runtime::v1::RootfsSrcType>(funcMeta.rootfs.type));
    if (funcMeta.rootfs.type == RootfsSrcType::S3) {
        auto s3Config = container->mutable_rootfsconfig()->mutable_s3_config();
        s3Config->set_endpoint(funcMeta.rootfs.storageInfo.endpoint);
        s3Config->set_bucket(funcMeta.rootfs.storageInfo.bucket);
        s3Config->set_object(funcMeta.rootfs.storageInfo.object);
        s3Config->set_access_key_id(funcMeta.rootfs.storageInfo.accessKey);
        s3Config->set_access_key_secret(funcMeta.rootfs.storageInfo.secretKey);
    } else if (funcMeta.rootfs.type == RootfsSrcType::IMAGE) {
        container->mutable_rootfsconfig()->set_image_url(funcMeta.rootfs.imageurl);
    } else if (funcMeta.rootfs.type == RootfsSrcType::LOCAL) {
        container->mutable_rootfsconfig()->set_path(funcMeta.rootfs.path);
    }
}

void BuildBootstrapConfig(
    const FunctionMeta &funcMeta, const std::shared_ptr<DeployInstanceRequest> &deployInstanceRequest)
{
    if (funcMeta.bootstrap.type.empty() && funcMeta.bootstrap.root.empty() &&
        funcMeta.bootstrap.entrypoint.empty() && funcMeta.bootstrap.cmd.empty()) {
        return;
    }
    auto bootstrapConfig = deployInstanceRequest->mutable_bootstrapconfig();
    bootstrapConfig->set_type(funcMeta.bootstrap.type);
    bootstrapConfig->set_root(funcMeta.bootstrap.root);
    bootstrapConfig->set_entrypoint(funcMeta.bootstrap.entrypoint);
    bootstrapConfig->set_cmd(funcMeta.bootstrap.cmd);
}
}  // namespace functionsystem
