#!/bin/bash
# Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

FUNCTION_SYSTEM_DEPLOY_DIR=$(dirname "$(readlink -f "$0")")
if [ -n "${BASE_DIR}" ]; then
  FUNCTION_SYSTEM_DEPLOY_DIR=${BASE_DIR}/../../functionsystem/deploy
fi
FUNCTION_SYSTEM_DIR=$(readlink -m "${FUNCTION_SYSTEM_DEPLOY_DIR}/..")
DATA_SYSTEM_DIR=$(readlink -m "${FUNCTION_SYSTEM_DIR}/../datasystem")
RUNTIME_HOME_DIR=$(readlink -m "${FUNCTION_SYSTEM_DIR}/../runtime")
if [ -d "${FUNCTION_SYSTEM_DIR}/../pattern/pattern_faas" ]; then
  PATTERN_FAAS_HOME_DIR=$(readlink -m "${FUNCTION_SYSTEM_DIR}/../pattern/pattern_faas")
  FUNCTION_META_PATH="$PATTERN_FAAS_HOME_DIR/executor-meta"
fi
function install_function_proxy() {
  log_info "start function proxy, proxy_port=${FUNCTION_PROXY_PORT}, grpc_port=${FUNCTION_PROXY_GRPC_PORT}..."
  local bin=${FUNCTION_SYSTEM_DIR}/bin/function_proxy
  local is_pseudo_data_plane="false"
  if [ ${CPU4COMP} -le 100 ]; then
    is_pseudo_data_plane="true"
  fi

  jemalloc_path=""
  if [ "X${ENABLE_JEMALLOC}" == "Xtrue" ]; then
    jemalloc_path="${JEMALLOC_LIB_PATH}"
    if [ ! -f "${jemalloc_path}" ]; then
      log_warning "jemalloc lib path ${jemalloc_path} does not exist!"
    fi
  fi

  if [ "x${ENABLE_META_STORE}" == "xtrue" ]; then
    META_STORE_ADDRESS="${FUNCTION_MASTER_IP}:${GLOBAL_SCHEDULER_PORT}"
  else
    META_STORE_ADDRESS="${ETCD_CLUSTER_ADDRESS}"
  fi

  local enable_driver="true"
  [[ "X${DRIVER_GATEWAY_ENABLE^^}" == "XFALSE" ]] && { enable_driver="false"; }
  LD_LIBRARY_PATH=${FUNCTION_SYSTEM_DIR}/lib:${LD_LIBRARY_PATH} \
    LD_PRELOAD="${jemalloc_path}" \
    ${bin} --address="${IP_ADDRESS}:${FUNCTION_PROXY_PORT}" --meta_store_address="${META_STORE_ADDRESS}" \
    --etcd_address="${ETCD_CLUSTER_ADDRESS}" \
    --node_id="${NODE_ID}" --log_config="${FS_LOG_CONFIG}" \
    --services_path="${SERVICES_PATH}" \
    --lib_path="${FUNCTION_SYSTEM_DIR}/lib" \
    --ip="${IP_ADDRESS}" \
    --grpc_listen_port="${FUNCTION_PROXY_GRPC_PORT}" \
    --enable_driver="${enable_driver}" \
    --enable_trace="${ENABLE_TRACE}" \
    --enable_metrics="${ENABLE_METRICS}" \
    --metrics_config="${METRICS_CONFIG}" \
    --metrics_config_file="${METRICS_CONFIG_FILE}" \
    --litebus_thread_num="${FUNCTION_PROXY_LITEBUS_THREAD}" \
    --runtime_heartbeat_enable="${RUNTIME_HEARTBEAT_ENABLE}" \
    --runtime_max_heartbeat_timeout_times="${RUNTIME_MAX_HEARTBEAT_TIMEOUT_TIMES}" \
    --runtime_heartbeat_timeout_ms="${RUNTIME_HEARTBEAT_TIMEOUT_MS}" \
    --runtime_recover_enable="${RUNTIME_RECOVER_ENABLE}" \
    --state_storage_type="${STATE_STORAGE_TYPE}" \
    --runtime_init_call_timeout_seconds="${RUNTIME_INIT_CALL_TIMEOUT_SECONDS}" \
    --global_scheduler_address="${FUNCTION_MASTER_IP}:${GLOBAL_SCHEDULER_PORT}" --update_resource_cycle=1000 \
    --runtime_conn_timeout_s="${RUNTIME_CONN_TIMEOUT_S}" \
    --pseudo_data_plane="${is_pseudo_data_plane}" \
    --system_timeout="${SYSTEM_TIMEOUT}" \
    --cache_storage_host="${IP_ADDRESS}" --cache_storage_port="${DS_WORKER_PORT}" \
    --enable_print_resource_view="${ENABLE_PRINT_RESOURCE_VIEW}" \
    --enable_server_mode="true" \
    --schedule_plugins="${LOCAL_SCHEDULE_PLUGINS}" \
    --max_priority="${MAX_PRIORITY}" --enable_preemption="${ENABLE_PREEMPTION}" \
    --min_instance_memory_size=${MIN_INSTANCE_MEMORY_SIZE} --min_instance_cpu_size=${MIN_INSTANCE_CPU_SIZE} \
    --max_instance_memory_size=${MAX_INSTANCE_MEMORY_SIZE} --max_instance_cpu_size=${MAX_INSTANCE_CPU_SIZE} \
    --election_mode=${ELECTION_MODE} --unregister_while_stop="${FUNCTION_PROXY_UNREGISTER_WHILE_STOP}" \
    --runtime_ds_encrypt_enable="${RUNTIME_DS_ENCRYPT_ENABLE}" --runtime_ds_auth_enable="${RUNTIME_DS_AUTH_ENABLE}" \
    --curve_key_path="${CURVE_KEY_PATH}" \
    --cache_storage_auth_type="${CACHE_STORAGE_AUTH_TYPE}" --cache_storage_auth_enable="${CACHE_STORAGE_AUTH_ENABLE}" \
    --ssl_downgrade_enable="true" \
    --ssl_enable="${SSL_ENABLE}" --ssl_base_path="${SSL_BASE_PATH}" \
    --ssl_root_file="${SSL_ROOT_FILE}" --ssl_cert_file="${SSL_CERT_FILE}" --ssl_key_file="${SSL_KEY_FILE}" \
    --etcd_auth_type="${ETCD_AUTH_TYPE}" --etcd_root_ca_file="${ETCD_CA_FILE}" --etcd_cert_file="${ETCD_CLIENT_CERT_FILE}" --etcd_key_file="${ETCD_CLIENT_KEY_FILE}" \
    --etcd_ssl_base_path=${ETCD_SSL_BASE_PATH} \
    --etcd_table_prefix="${ETCD_TABLE_PREFIX}" --etcd_target_name_override="${ETCD_TARGET_NAME_OVERRIDE}" \
    --enable_print_perf="${ENABLE_PRINT_PERF}" --is_partial_watch_instances="${IS_PARTIAL_WATCH_INSTANCES}" \
    --enable_meta_store="${ENABLE_META_STORE}" --meta_store_mode="${META_STORE_MODE}" --meta_store_excluded_keys="${META_STORE_EXCLUDED_KEYS}" \
    --runtime_instance_debug_enable="${RUNTIME_INSTANCE_DEBUG_ENABLE}"  >>"${FS_LOG_PATH}/${NODE_ID}-function_proxy${STD_LOG_SUFFIX}" 2>&1 &

  FUNCTION_PROXY_PID="$!"
  log_info "succeed to start function proxy, proxy_port=${FUNCTION_PROXY_PORT}, grpc_port=${FUNCTION_PROXY_GRPC_PORT}, pid=${FUNCTION_PROXY_PID}"
}

function install_dashboard() {
  log_info "start dashboard, ip=${IP_ADDRESS}, port=${DASHBOARD_PORT}..."
  dashboard_config=${FUNCTION_SYSTEM_DIR}/config/dashboard_config.json
  install_dashboard_config=${config_install_dir}/dashboard_config.json
  cp "${dashboard_config}" "${install_dashboard_config}"
  sed -i "s/{ip}/${IP_ADDRESS}/g" "${install_dashboard_config}"
  sed -i "s/{port}/${DASHBOARD_PORT}/g" "${install_dashboard_config}"
  sed -i "s*{staticPath}*${FUNCTION_SYSTEM_DIR}/bin/client/dist*g" "${install_dashboard_config}"
  sed -i "s/{grpcIP}/${IP_ADDRESS}/g" "${install_dashboard_config}"
  sed -i "s/{grpcPort}/${DASHBOARD_GRPC_PORT}/g" "${install_dashboard_config}"
  sed -i "s/{functionMasterAddr}/${IP_ADDRESS}:${GLOBAL_SCHEDULER_PORT}/g" "${install_dashboard_config}"
  sed -i "s/{frontendAddr}/${IP_ADDRESS}:${FAAS_FRONTEND_HTTP_PORT}/g" "${install_dashboard_config}"
  sed -i "s/{prometheusAddr}/${PROMETHEUS_ADDRESS}/g" "${install_dashboard_config}"
  sed -i "s/{etcdAddr}/$(echo ${ETCD_CLUSTER_ADDRESS} | sed 's/,/","/g')/g" "${install_dashboard_config}"
  sed -i "s/{etcdSsl}/${SSL_ENABLE}/g" "${install_dashboard_config}"
  sed -i "s/{etcdAuthType}/${ETCD_AUTH_TYPE}/g" "${install_dashboard_config}"
  sed -i "s*{azPrefix}*${ETCD_TABLE_PREFIX}*g" "${install_dashboard_config}"
  LD_LIBRARY_PATH=${FUNCTION_SYSTEM_DIR}/lib:${ld_library_path} \
    LOG_CONFIG="${FAAS_LOG_CONFIG}" \
    "${FUNCTION_SYSTEM_DIR}"/bin/dashboard \
    --config_path="${install_dashboard_config}" \
    >>"${FS_LOG_PATH}/${NODE_ID}-dashboard${STD_LOG_SUFFIX}" 2>&1 &
  DASHBOARD_PID=$!
  if dashboard_health_check ${DASHBOARD_PID}; then
    log_info "succeed to start dashboard process, ip=${IP_ADDRESS}, port=${DASHBOARD_PORT} pid=${DASHBOARD_PID}"
    return 0
  fi
  return 1
}

function install_collector() {
  log_info "start collector, port=${COLLECTOR_PORT}..."
  GO_RUNTIME_BIN=${RUNTIME_HOME_DIR}/service/go/bin
  DS_SDK_GO_LIB=${DATA_SYSTEM_DIR}/sdk/go/lib
  LD_LIBRARY_PATH=${GO_RUNTIME_BIN}:${DS_SDK_GO_LIB}:${LD_LIBRARY_PATH} \
  "${FUNCTION_SYSTEM_DIR}"/bin/collector \
    --collect_id="${NODE_ID}" \
    --datasystem_port="${DS_WORKER_PORT}" \
    --ip="${IP_ADDRESS}" \
    --port="${COLLECTOR_PORT}" \
    --log_root="${LOG_ROOT}" \
    --etcd_config_servers="${ETCD_CLUSTER_ADDRESS}" \
    --manager_address="${IP_ADDRESS}:${DASHBOARD_GRPC_PORT}" > "${FS_LOG_PATH}/${NODE_ID}-collector${STD_LOG_SUFFIX}" 2>&1 &
  COLLECTOR_PID="$!"
  if dashboard_health_check ${COLLECTOR_PID}; then
    log_info "succeed to start collector process, port=${COLLECTOR_PORT} pid=${DASHBOARD_PID}"
    return 0
  fi
}

function install_faas_frontend() {
  log_info "start faas frontend, http_ip=${IP_ADDRESS}, http_port=${FAAS_FRONTEND_HTTP_PORT}, grpc_port=${FAAS_FRONTEND_GRPC_PORT}..."
  init_frontend_config=${FUNCTION_SYSTEM_DIR}/config/init_frontend_args.json
  install_init_frontend_config=${config_install_dir}/init_frontend_args_temp.json
  cp ${init_frontend_config} ${install_init_frontend_config}
  sed -i "s/{etcdAddr}/$(echo ${ETCD_CLUSTER_ADDRESS} | sed 's/,/","/g')/g" ${install_init_frontend_config}
  sed -i "s/{faas_frontend_http_ip}/${IP_ADDRESS}/g" ${install_init_frontend_config}
  sed -i "s/{faas_frontend_http_port}/${FAAS_FRONTEND_HTTP_PORT}/g" ${install_init_frontend_config}
  sed -i "s/{sslEnable}/${SSL_ENABLE}/g" ${install_init_frontend_config}
  sed -i "s/{sccEnable}/${SCC_ENABLE}/g" ${install_init_frontend_config}
  sed -i "s/{etcdAuthType}/${ETCD_AUTH_TYPE}/g" ${install_init_frontend_config}
  sed -i "s*{azPrefix}*${ETCD_TABLE_PREFIX}*g" ${install_init_frontend_config}
  sed -i "s*{sslBasePath}*${SSL_BASE_PATH}*g" ${install_init_frontend_config}
  sed -i "s*{sccBasePath}*${SCC_BASE_PATH}*g" ${install_init_frontend_config}
  if [ "X${SSL_ENABLE}" = "Xtrue" ] && [ -n "${ETCD_SSL_BASE_PATH}" ]; then
    sed -i "s*{etcdCAFile}*${ETCD_SSL_BASE_PATH}/${ETCD_CA_FILE}*g" ${install_init_frontend_config}
    sed -i "s*{etcdCertFile}*${ETCD_SSL_BASE_PATH}/${ETCD_CLIENT_CERT_FILE}*g" ${install_init_frontend_config}
    sed -i "s*{etcdKeyFile}*${ETCD_SSL_BASE_PATH}/${ETCD_CLIENT_KEY_FILE}*g" ${install_init_frontend_config}
    sed -i "s*{passphraseFile}*${ETCD_SSL_BASE_PATH}/${ETCD_CLIENT_PWD_FILE}*g" ${install_init_frontend_config}
  else
    sed -i "s*{etcdCAFile}**g" ${install_init_frontend_config}
    sed -i "s*{etcdCertFile}**g" ${install_init_frontend_config}
    sed -i "s*{etcdKeyFile}**g" ${install_init_frontend_config}
    sed -i "s*{passphraseFile}**g" ${install_init_frontend_config}
  fi
  GO_RUNTIME_BIN=${RUNTIME_HOME_DIR}/service/go/bin
  POD_NAME="frontend-process" \
  FUNCTION_LIB_PATH=${PATTERN_FAAS_HOME_DIR}/faasfrontend/faasfrontend.so \
  INIT_ARGS_FILE_PATH=${install_init_frontend_config} \
  LD_LIBRARY_PATH=${FUNCTION_SYSTEM_DIR}/lib:${GO_RUNTIME_BIN}:${LD_LIBRARY_PATH} \
  ENABLE_SERVER_MODE="true" \
  INIT_HANDLER="faasfrontend.InitHandler" \
  CALL_HANDLER="faasfrontend.CallHandler" \
  CHECKPOINT_HANDLER="faasfrontend.CheckpointHandler" \
  RECOVER_HANDLER="faasfrontend.RecoverHandler" \
  SHUTDOWN_HANDLER="faasfrontend.ShutdownHandler" \
  SIGNAL_HANDLER="faasfrontend.SignalHandler" \
  YR_FUNCTION_LIB_PATH=${PATTERN_FAAS_HOME_DIR}/faasfrontend/ \
  GLOG_log_dir="${FS_LOG_PATH}" \
  YR_LOG_LEVEL=${FS_LOG_LEVEL} \
  POD_IP=${IP_ADDRESS} \
  NODE_IP=${IP_ADDRESS} \
  DATASYSTEM_ADDR=${IP_ADDRESS}:${DS_WORKER_PORT} \
  INSTANCE_ID="driver-faas-frontend-${NODE_ID}" \
  FAAS_LOG_PATH=${FS_LOG_PATH} \
  ${GO_RUNTIME_BIN}/goruntime \
  -jobId=${NODE_ID} \
  -runtimeId='faas_frontend_libruntime' \
  -instanceId="driver-faas-frontend-${NODE_ID}" \
  -functionName='0/0-system-faasfrontend/$latest' \
  -logLevel=${FS_LOG_LEVEL} \
  -logPath=${FS_LOG_PATH} \
  -enableMTLS=${ENABLE_MTLS} \
  -privateKeyPath=${PRIVATE_KEY_PATH} \
  -certificateFilePath=${CERTIFICATE_FILE_PATH} \
  -verifyFilePath=${VERIFY_FILE_PATH} \
  -encryptPrivateKeyPasswd=${ENCRYPT_PRIVATE_KEY_PASSWD} \
  -primaryKeyStoreFile=${PRIMARY_KEY_STORE_FILE} \
  -standbyKeyStoreFile=${STANDBY_KEY_STORE_FILE} \
  -enableDsEncrypt=${RUNTIME_DS_ENCRYPT_ENABLE} \
  -functionSystemAddress="${IP_ADDRESS}:${FUNCTION_PROXY_GRPC_PORT}" \
  -driverMode true  >> "${FS_LOG_PATH}/${NODE_ID}-faas_frontend${STD_LOG_SUFFIX}"  2>&1 &
  FAAS_FRONTEND_PID="$!"
  log_info "succeed to start faas frontend, http_ip=${IP_ADDRESS}, http_port=${FAAS_FRONTEND_HTTP_PORT}, grpc_port=${FAAS_FRONTEND_GRPC_PORT}, pid=${FAAS_FRONTEND_PID}"
}

function install_function_agent() {
  install_function_agent_and_runtime_manager_in_the_same_process
  return  $?
}

function install_function_agent_and_runtime_manager_in_the_same_process() {
  log_info "start function agent and runtime manager, port=${FUNCTION_AGENT_PORT}..."
  local bin=${FUNCTION_SYSTEM_DIR}/bin/function_agent
  local ld_library_path=${LD_LIBRARY_PATH}
  local unique_proxy_option="--local_node_id=${NODE_ID}"
  if [ "X${DEPLOY_FUNCTION_PROXY}" = "Xfalse" ] && [ ! -z "${UNIQUE_NODE_ID}" ]; then
    unique_proxy_option="--local_node_id=${UNIQUE_NODE_ID}"
  fi
  local user_lod_export_option=">>${FS_LOG_PATH}/${NODE_ID}-function_agent${STD_LOG_SUFFIX} 2>&1"
  if [ "x${USER_LOG_EXPORT_MODE}" == "xstd" ]; then
    user_lod_export_option=""
  fi
  local agent_uid=${YR_POD_NAME}
  if [ "x${YR_POD_NAME}" == "x" ]; then
    agent_uid="${NODE_ID}"
  fi
  LD_LIBRARY_PATH=${FUNCTION_SYSTEM_DIR}/lib:${ld_library_path} \
  HOST_IP="${IP_ADDRESS}" \
  RUNTIME_METRICS_CONFIG=$RUNTIME_METRICS_CONFIG\
    INIT_LABELS=${LABELS} \
    ${bin} \
    --enable_merge_process=true \
    --ip="${IP_ADDRESS}" \
    --node_id="${NODE_ID}" \
    --agent_uid="${agent_uid}" \
    --alias="${FUNCTION_AGENT_ALIAS}" \
    --log_config="${FS_LOG_CONFIG}" \
    --litebus_thread_num="${FUNCTION_AGENT_LITEBUS_THREAD}" \
    --local_scheduler_address="${IP_ADDRESS}:${FUNCTION_PROXY_PORT}" \
    --agent_listen_port="${FUNCTION_AGENT_PORT}" \
    --runtime_dir="${RUNTIME_HOME_DIR}/service" \
    --runtime_home_dir="${RUNTIME_USER_HOME_DIR}" \
    --runtime_logs_dir="${RUNTIME_LOG_PATH}" --runtime_std_log_dir="" \
    --runtime_ld_library_path="${ld_library_path}:${RUNTIME_HOME_DIR}/service/cpp/snlib:${RUNTIME_HOME_DIR}/sdk/cpp/lib" \
    --runtime_log_level="${RUNTIME_LOG_LEVEL}" \
    --runtime_max_log_size="${RUNTIME_LOG_ROLLING_MAX_SIZE}" \
    --runtime_max_log_file_num="${RUNTIME_LOG_ROLLING_MAX_FILES}" \
    --runtime_config_dir="${RUNTIME_HOME_DIR}/service/cpp/config/" \
    --enable_separated_redirect_runtime_std="${SEPARATED_REDIRECT_RUNTIME_STD}" \
    --user_log_export_mode="${USER_LOG_EXPORT_MODE}" \
    --npu_collection_mode="${NPU_COLLECTION_MODE}" \
    --gpu_collection_enable="${GPU_COLLECTION_ENABLE}" \
    --proxy_grpc_server_port="${FUNCTION_PROXY_GRPC_PORT}" \
    --setCmdCred=false \
    --python_dependency_path="${PYTHONPATH}:${RUNTIME_HOME_DIR}/service/python" \
    --python_log_config_path="${RUNTIME_HOME_DIR}/service/python/config/python-runtime-log.json" \
    --java_system_property="${RUNTIME_HOME_DIR}/service/java/log4j2.xml" \
    --java_system_library_path="${RUNTIME_HOME_DIR}/service/java/lib" \
    --host_ip="${IP_ADDRESS}" \
    --port="${FUNCTION_AGENT_PORT}" \
    --data_system_port="${DS_WORKER_PORT}" \
    --agent_address="${IP_ADDRESS}:${FUNCTION_AGENT_PORT}" \
    --enable_metrics="${ENABLE_METRICS}" \
    --metrics_config="${METRICS_CONFIG}" \
    --metrics_config_file="${METRICS_CONFIG_FILE}" \
    --runtime_initial_port="${RUNTIME_INIT_PORT}" \
    --port_num="${RUNTIME_PORT_NUM}" \
    --system_timeout="${SYSTEM_TIMEOUT}" \
    --metrics_collector_type="${METRICS_COLLECTOR_TYPE}" \
    --proc_metrics_cpu="${CPU4COMP}" \
    --custom_resources="${CUSTOM_RESOURCES}" \
    --is_protomsg_to_runtime="${IS_PROTOMSG_TO_RUNTIME}" \
    --massif_enable="${MASSIF_ENABLE}" \
    --enable_inherit_env="${ENABLE_INHERIT_ENV}" \
    --memory_detection_interval="${MEMORY_DETECTION_INTERVAL}" \
    --oom_kill_enable="${OOM_KILL_ENABLE}" \
    --oom_kill_control_limit="${OOM_KILL_CONTROL_LIMIT}" \
    --oom_consecutive_detection_count="${OOM_CONSECUTIVE_DETECTION_COUNT}" \
    --kill_process_timeout_seconds="${KILL_PROCESS_TIMEOUT_SECONDS}" \
    --runtime_ds_connect_timeout="${RUNTIME_DS_CONNECT_TIMEOUT}" \
    --runtime_direct_connection_enable="${RUNTIME_DIRECT_CONNECTION_ENABLE}" \
    --ssl_enable="${SSL_ENABLE}" --ssl_base_path="${SSL_BASE_PATH}" \
    --ssl_root_file="${SSL_ROOT_FILE}" --ssl_cert_file="${SSL_CERT_FILE}" --ssl_key_file="${SSL_KEY_FILE}" \
    --etcd_auth_type="${ETCD_AUTH_TYPE}" --etcd_root_ca_file="${ETCD_CA_FILE}" --etcd_cert_file="${ETCD_CLIENT_CERT_FILE}" --etcd_key_file="${ETCD_CLIENT_KEY_FILE}" \
    --etcd_ssl_base_path=${ETCD_SSL_BASE_PATH} \
    --runtime_default_config="${RUNTIME_DEFAULT_CONFIG}" \
    --proc_metrics_memory="${MEM4COMP}" ${unique_proxy_option} \
    --runtime_instance_debug_enable="${RUNTIME_INSTANCE_DEBUG_ENABLE}" ${user_lod_export_option} &
  FUNCTION_AGENT_PID="$!"
  log_info "succeed to start function agent and runtime manager, port=${FUNCTION_AGENT_PORT} pid=${FUNCTION_AGENT_PID}"
}

function install_function_master() {
  log_info "start function master, port=${GLOBAL_SCHEDULER_PORT}..."
  jemalloc_path=""
  if [ "X${ENABLE_JEMALLOC}" == "Xtrue" ]; then
    jemalloc_path="${JEMALLOC_LIB_PATH}"
    if [ ! -f "${jemalloc_path}" ]; then
      log_warning "jemalloc lib path ${jemalloc_path} does not exist!"
    fi
  fi

  if [ "x${ENABLE_META_STORE}" == "xtrue" ] && [ "x${META_STORE_MODE}" == "xlocal" ]; then
    META_STORE_ADDRESS="${IP_ADDRESS}:${GLOBAL_SCHEDULER_PORT}"
  else
    META_STORE_ADDRESS="${ETCD_CLUSTER_ADDRESS}"
  fi

  if check_port "${FUNCTION_MASTER_IP}" "${GLOBAL_SCHEDULER_PORT}"; then
    OPENSSL_CONF="" LD_LIBRARY_PATH=${FUNCTION_SYSTEM_DIR}/lib:${LD_LIBRARY_PATH} \
      LD_PRELOAD="${jemalloc_path}":"${LD_PRELOAD}" \
      "${FUNCTION_SYSTEM_DIR}"/bin/function_master --ip="${IP_ADDRESS}:${GLOBAL_SCHEDULER_PORT}" \
      --meta_store_address="${META_STORE_ADDRESS}" --log_config="${FS_LOG_CONFIG}" \
      --etcd_address="${ETCD_CLUSTER_ADDRESS}" \
      --node_id="${NODE_ID}" --sys_func_retry_period="${SYS_FUNC_RETRY_PERIOD}" \
      --runtime_recover_enable="${RUNTIME_RECOVER_ENABLE}" \
      --litebus_thread_num="${FUNCTION_MASTER_LITEBUS_THREAD}" \
      --system_timeout="${SYSTEM_TIMEOUT}" --enable_metrics="${ENABLE_METRICS}" \
      --metrics_config="${METRICS_CONFIG}" \
      --metrics_config_file="${METRICS_CONFIG_FILE}" \
      --pull_resource_interval="${PULL_RESOURCE_INTERVAL}" \
      --is_schedule_tolerate_abnormal="${IS_SCHEDULE_TOLERATE_ABNORMAL}" \
      --enable_print_resource_view="${ENABLE_PRINT_RESOURCE_VIEW}" \
      --schedule_plugins="${DOMAIN_SCHEDULE_PLUGINS}" \
      --schedule_relaxed="${SCHEDULE_RELAXED}" \
      --max_priority="${MAX_PRIORITY}" --enable_preemption="${ENABLE_PREEMPTION}" \
      --enable_meta_store="${ENABLE_META_STORE}" \
      --enable_persistence="${ENABLE_PERSISTENCE}" \
      --meta_store_mode="${META_STORE_MODE}" \
      --meta_store_excluded_keys="${META_STORE_EXCLUDED_KEYS}" \
      --election_mode=${ELECTION_MODE} \
      --services_path="${SERVICES_PATH}" \
      --lib_path="${FUNCTION_SYSTEM_DIR}/lib" \
      --ssl_enable="${SSL_ENABLE}" --ssl_base_path="${SSL_BASE_PATH}" \
      --etcd_auth_type="${ETCD_AUTH_TYPE}" --etcd_root_ca_file="${ETCD_CA_FILE}" --etcd_cert_file="${ETCD_CLIENT_CERT_FILE}" --etcd_key_file="${ETCD_CLIENT_KEY_FILE}" \
      --etcd_ssl_base_path=${ETCD_SSL_BASE_PATH} \
      --etcd_table_prefix="${ETCD_TABLE_PREFIX}" --etcd_target_name_override="${ETCD_TARGET_NAME_OVERRIDE}" \
      --ssl_root_file="${SSL_ROOT_FILE}" --ssl_cert_file="${SSL_CERT_FILE}" --ssl_key_file="${SSL_KEY_FILE}" \
      --meta_store_max_flush_concurrency="${META_STORE_MAX_FLUSH_CONCURRENCY}" --meta_store_max_flush_batch_size="${META_STORE_MAX_FLUSH_BATCH_SIZE}" \
      >>"${FS_LOG_PATH}/${NODE_ID}-function_master${STD_LOG_SUFFIX}" 2>&1 &
    FUNCTION_MASTER_PID=$!
    if function_system_health_check ${FUNCTION_MASTER_PID} "${GLOBAL_SCHEDULER_PORT}" "global-scheduler"; then
      log_info "succeed to start function master process, port=${GLOBAL_SCHEDULER_PORT}, pid=${FUNCTION_MASTER_PID}"
      return 0
    fi
    if [ ${FUNCTION_MASTER_PID} -gt 0 ]; then
      log_warning "health check failed, killing function_master process pid: ${FUNCTION_MASTER_PID}"
      kill -9 ${FUNCTION_MASTER_PID}
    fi
  fi
  return 1
}

function install_metaservice() {
  log_info "start metaservice, ip=${IP_ADDRESS}, port=${METASERVICE_PORT}..."
  metaservice_config=${FUNCTION_SYSTEM_DIR}/config/meta_service/metaservice_config.json
  install_metaservice_config=${config_install_dir}/metaservice_config.json
  cp ${metaservice_config} ${install_metaservice_config}
  sed -i "s/{ip}/${IP_ADDRESS}/g" ${install_metaservice_config}
  sed -i "s/{port}/${METASERVICE_PORT}/g" ${install_metaservice_config}
  sed -i "s/{maxFunctionVersion}/${MAX_FUNCTION_VERSION}/g" ${install_metaservice_config}
  sed -i "s/{maxInstanceLabel}/${MAX_INSTANCE_LABEL}/g" ${install_metaservice_config}
  sed -i "s/{functionMasterAddr}/${IP_ADDRESS}:${GLOBAL_SCHEDULER_PORT}/g" ${install_metaservice_config}
  sed -i "s/{frontendAddr}/${IP_ADDRESS}:${FAAS_FRONTEND_HTTP_PORT}/g" ${install_metaservice_config}
  sed -i "s/{etcdAddr}/$(echo ${ETCD_CLUSTER_ADDRESS} | sed 's/,/","/g')/g" ${install_metaservice_config}
  sed -i "s/{sslEnable}/${SSL_ENABLE}/g" ${install_metaservice_config}
  sed -i "s*{azPrefix}*${ETCD_TABLE_PREFIX}*g" ${install_metaservice_config}
  sed -i "s/{etcdAuthType}/${ETCD_AUTH_TYPE}/g" ${install_metaservice_config}
  sed -i "s/{clusters}/${CLUSTER_LIST}/g" ${install_metaservice_config}
  sed -i "s/{sccEnable}/${SCC_ENABLE}/g" ${install_metaservice_config}
  sed -i "s*{sccBasePath}*${SCC_BASE_PATH}*g" ${install_metaservice_config}
  if [ "X${SSL_ENABLE}" = "Xtrue" ] && [ -n "${SSL_BASE_PATH}" ]; then
    sed -i "s*{rootCAFile}*${SSL_BASE_PATH}/${SSL_ROOT_FILE}*g" ${install_metaservice_config}
    sed -i "s*{moduleCertFile}*${SSL_BASE_PATH}/${SSL_CERT_FILE}*g" ${install_metaservice_config}
    sed -i "s*{moduleKeyFile}*${SSL_BASE_PATH}/${SSL_KEY_FILE}*g" ${install_metaservice_config}
    sed -i "s*{pwdFile}*${SSL_BASE_PATH}/${SSL_PWD_FILE}*g" ${install_metaservice_config}
  else
    sed -i "s*{rootCAFile}**g" ${install_metaservice_config}
    sed -i "s*{moduleCertFile}**g" ${install_metaservice_config}
    sed -i "s*{moduleKeyFile}**g" ${install_metaservice_config}
    sed -i "s*{pwdFile}**g" ${install_metaservice_config}
  fi
  if [ "X${SSL_ENABLE}" = "Xtrue" ] && [ -n "${ETCD_SSL_BASE_PATH}" ]; then
    sed -i "s*{etcdCAFile}*${ETCD_SSL_BASE_PATH}/${ETCD_CA_FILE}*g" ${install_metaservice_config}
    sed -i "s*{etcdCertFile}*${ETCD_SSL_BASE_PATH}/${ETCD_CLIENT_CERT_FILE}*g" ${install_metaservice_config}
    sed -i "s*{etcdKeyFile}*${ETCD_SSL_BASE_PATH}/${ETCD_CLIENT_KEY_FILE}*g" ${install_metaservice_config}
    sed -i "s*{passphraseFile}*${ETCD_SSL_BASE_PATH}/${ETCD_CLIENT_PWD_FILE}*g" ${install_metaservice_config}
  else
    sed -i "s*{etcdCAFile}**g" ${install_metaservice_config}
    sed -i "s*{etcdCertFile}**g" ${install_metaservice_config}
    sed -i "s*{etcdKeyFile}**g" ${install_metaservice_config}
    sed -i "s*{passphraseFile}**g" ${install_metaservice_config}
  fi
  if [ "X${SCC_ENABLE}" = "Xtrue" ]; then
    sed -i "s*{sslDecryptTool}*${SSL_DECRYPT_TOOL}*g" ${install_metaservice_config}
  else
    sed -i "s*{sslDecryptTool}**g" ${install_metaservice_config}
  fi
  metaservice_log=${FUNCTION_SYSTEM_DIR}/config/meta_service/metaservice_log.json
  metaservice_temp_log=${FUNCTION_SYSTEM_DIR}/config/metaservice_temp_log.json
  cp ${metaservice_log} ${metaservice_temp_log}
  metaservice_log_path=${FS_LOG_PATH}
  sed -i "s*{logConfigPath}*${metaservice_log_path}*g" ${metaservice_temp_log}
  sed -i "s/{logLevel}/${FS_LOG_LEVEL}/g" ${metaservice_temp_log}
  LD_LIBRARY_PATH=${FUNCTION_SYSTEM_DIR}/lib:${ld_library_path} \
    ${FUNCTION_SYSTEM_DIR}/bin/meta-service \
    --config_path="${install_metaservice_config}" \
    --log_config_path="${metaservice_temp_log}" \
    >>"${FS_LOG_PATH}/${NODE_ID}-metaservice${STD_LOG_SUFFIX}" 2>&1 &
    METASERVICE_PID=$!
  if metaservice_health_check ${METASERVICE_PID}; then
    log_info "succeed to start metaservice process, ip=${IP_ADDRESS} port=${METASERVICE_PORT} pid=${METASERVICE_PID}"
    return 0
  fi
  return 1
}

function install_function_system() {
  config_install_dir="${INSTALL_DIR_PARENT}/config"
  [ -d "${config_install_dir}" ] || mkdir -p "${config_install_dir}"
  case "$1" in
  function_master)
    install_function_master
    ;;
  function_agent)
    install_function_agent
    ;;
  function_proxy)
    install_function_proxy
    ;;
  dashboard)
    install_dashboard
    ;;
  collector)
    install_collector
    ;;
  faas_frontend)
    install_faas_frontend
    ;;
  metaservice)
    install_metaservice
    ;;
  *)
    log_warning >&2 "Unknown component $1"
    return 1
    ;;
  esac
  return $?
}