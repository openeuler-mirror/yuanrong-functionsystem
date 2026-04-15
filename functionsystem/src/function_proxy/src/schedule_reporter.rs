//! Local schedule outcome logging (C++ `schedule_reporter` analogue; no separate RPC in current protos).

use tracing::info;

pub fn report_schedule_result(
    node_id: &str,
    function_name: &str,
    tenant_id: &str,
    instance_id: &str,
    success: bool,
    error_code: i32,
    message: &str,
) {
    if success {
        info!(
            target: "yr_proxy::schedule",
            %node_id,
            %function_name,
            %tenant_id,
            %instance_id,
            "local schedule committed"
        );
    } else {
        info!(
            target: "yr_proxy::schedule",
            %node_id,
            %function_name,
            %tenant_id,
            %instance_id,
            error_code,
            %message,
            "local schedule failed"
        );
    }
}
