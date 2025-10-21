aux_source_directory(${CMAKE_CURRENT_LIST_DIR}/grpc GRPC_SRCS)
aux_source_directory(${CMAKE_CURRENT_LIST_DIR}/proxy PROXY_SRCS)
aux_source_directory(${CMAKE_CURRENT_LIST_DIR}/rpc_client RPC_CLIENT_SRCS)

target_sources(function_proxy_common_lib PRIVATE ${GRPC_SRCS} ${PROXY_SRCS} ${RPC_CLIENT_SRCS})