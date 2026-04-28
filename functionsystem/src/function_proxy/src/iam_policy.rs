//! C++ `function_proxy/common/iam` policy authorization compatibility.

use crate::config::Config;
use serde_json::Value;
use std::collections::{HashMap, HashSet};
use std::sync::Arc;

pub const CALL_METHOD_CREATE: &str = "create";
pub const CALL_METHOD_INVOKE: &str = "invoke";
pub const CALL_METHOD_KILL: &str = "kill";
pub const WHITE_LIST: &str = "white_list";
pub const TENANT_ID: &str = "tenantId";

const TENANT_GROUP: &str = "tenant_group";
const POLICY: &str = "policy";
const ALLOW_POLICY: &str = "allow";
const DENY_POLICY: &str = "deny";
const DENY_POLICY_TENANT_LIST: &str = "tenant_list";
const SAME_TENANT: &str = "=";
const ALL_TENANT: &str = "*";
const EXTERNAL_TENANT_GROUP: &str = "external";

#[derive(Debug, Clone)]
struct TenantFuncGroup {
    func_group: String,
    tenant_id: String,
}

type SingleRule = HashMap<String, HashSet<String>>;
type CallerRule = HashMap<String, SingleRule>;

#[derive(Debug, Clone, Default)]
pub struct IamPolicy {
    func_groups: Vec<TenantFuncGroup>,
    func_group_set: HashSet<String>,
    allow_rules: HashMap<String, CallerRule>,
    tenant_blacklist: HashSet<String>,
    func_whitelist: HashMap<String, HashSet<String>>,
}

#[derive(Debug, Clone, Default)]
pub struct AuthorizeParam {
    pub caller_tenant_id: String,
    pub callee_tenant_id: String,
    pub call_method: String,
    pub func_name: String,
}

#[derive(Debug, Clone, Default)]
pub struct IamAuthorizer {
    enabled: bool,
    policy: Option<Arc<IamPolicy>>,
    load_error: Option<String>,
}

impl IamAuthorizer {
    pub fn from_config(config: &Config) -> Self {
        if !config.enable_iam {
            return Self::default();
        }
        let path = config.iam_policy_file.trim();
        if path.is_empty() {
            return Self {
                enabled: true,
                policy: None,
                load_error: Some("policyPath is empty".into()),
            };
        }
        let real = match std::fs::canonicalize(path) {
            Ok(p) => p,
            Err(e) => {
                return Self {
                    enabled: true,
                    policy: None,
                    load_error: Some(format!("policyPath({path}) is invalid: {e}")),
                }
            }
        };
        let content = match std::fs::read_to_string(&real) {
            Ok(c) => c,
            Err(e) => {
                return Self {
                    enabled: true,
                    policy: None,
                    load_error: Some(format!("read policy file {} failed: {e}", real.display())),
                }
            }
        };
        match IamPolicy::parse(&content) {
            Ok(policy) => Self {
                enabled: true,
                policy: Some(Arc::new(policy)),
                load_error: None,
            },
            Err(e) => Self {
                enabled: true,
                policy: None,
                load_error: Some(format!("update policy failed: {e}")),
            },
        }
    }

    pub fn is_enabled(&self) -> bool {
        self.enabled
    }

    pub fn authorize(&self, param: &AuthorizeParam) -> Result<(), String> {
        if !self.enabled {
            return Ok(());
        }
        if let Some(e) = &self.load_error {
            return Err(e.clone());
        }
        let policy = self
            .policy
            .as_ref()
            .ok_or_else(|| "policy content is empty".to_string())?;
        policy.authorize(param)
    }
}

impl IamPolicy {
    pub fn parse(policy_str: &str) -> Result<Self, String> {
        if policy_str.trim().is_empty() {
            return Err("empty policy content".into());
        }
        let policy_json: Value = serde_json::from_str(policy_str)
            .map_err(|e| format!("not a valid json, reason: {e}"))?;
        let tenant_group = required(&policy_json, TENANT_GROUP)?;
        let white_list = required(&policy_json, WHITE_LIST)?;
        let policy = required(&policy_json, POLICY)?;
        let allow = required(policy, ALLOW_POLICY)?;
        let deny = required(policy, DENY_POLICY)?;

        let mut out = Self::default();
        out.parse_tenant_groups(tenant_group)?;
        out.parse_whitelist(white_list)?;
        out.parse_allow_rules(allow)?;
        out.parse_deny_rules(deny)?;
        Ok(out)
    }

    pub fn authorize(&self, param: &AuthorizeParam) -> Result<(), String> {
        check_authorize_param(param)?;
        let caller_group = self.group_from_tenant(&param.caller_tenant_id);
        let callee_group = self.group_from_tenant(&param.callee_tenant_id);

        if self.tenant_blacklist.contains(&param.caller_tenant_id) {
            return Err(format!(
                "authorize failed, caller tenantID in blacklist, caller tenantID: {}",
                param.caller_tenant_id
            ));
        }
        if self.allow_rules.is_empty() {
            return Err("authorize failed, allow rules are empty.".into());
        }
        let caller_rule = self
            .allow_rules
            .get(&param.call_method)
            .ok_or_else(|| format!("{} allow rules not found.", param.call_method))?;
        let single_rule = caller_rule.get(&caller_group).ok_or_else(|| {
            format!("caller group allow rules not found, callerGroup: {caller_group}")
        })?;
        let func_rule_list = single_rule.get(&callee_group).ok_or_else(|| {
            format!("callee group allow rules not found, calleeGroup: {callee_group}")
        })?;
        self.is_func_in_list(func_rule_list, param)
            .map_err(|e| format!("Unauthorized call in allow rules, err: {e}"))
    }

    fn parse_tenant_groups(&mut self, groups: &Value) -> Result<(), String> {
        let obj = groups
            .as_object()
            .ok_or_else(|| "tenant_group must be object".to_string())?;
        for (group, tenants) in obj {
            if group.is_empty() {
                continue;
            }
            self.func_group_set.insert(group.clone());
            let Some(tenant_obj) = tenants.as_object() else {
                return Err(format!("tenant group {group} must be object"));
            };
            for (tenant, _funcs) in tenant_obj {
                if tenant.is_empty() {
                    continue;
                }
                self.func_groups.push(TenantFuncGroup {
                    func_group: group.clone(),
                    tenant_id: tenant.clone(),
                });
            }
        }
        Ok(())
    }

    fn parse_whitelist(&mut self, white_list: &Value) -> Result<(), String> {
        let obj = white_list
            .as_object()
            .ok_or_else(|| "white_list must be object".to_string())?;
        for (func_name, tenants) in obj {
            if func_name.is_empty() {
                return Err("func name in whitelist is empty.".into());
            }
            if !tenants_empty(tenants) {
                self.func_whitelist
                    .insert(func_name.clone(), string_set(tenants)?);
            }
        }
        Ok(())
    }

    fn parse_allow_rules(&mut self, allow: &Value) -> Result<(), String> {
        let obj = allow
            .as_object()
            .ok_or_else(|| "allow rules must be object".to_string())?;
        for method in obj.keys() {
            if method.is_empty() {
                return Err("call method is empty.".into());
            }
            if !matches!(
                method.as_str(),
                CALL_METHOD_CREATE | CALL_METHOD_INVOKE | CALL_METHOD_KILL
            ) {
                return Err(format!("call method is not allowed: {method}"));
            }
        }
        for method in [CALL_METHOD_INVOKE, CALL_METHOD_CREATE, CALL_METHOD_KILL] {
            let Some(group_rule) = obj.get(method) else {
                continue;
            };
            let caller_obj = group_rule
                .as_object()
                .ok_or_else(|| format!("{method} allow rule must be object"))?;
            let mut caller_rule = CallerRule::new();
            for (caller_group, callee_rules) in caller_obj {
                if caller_group.is_empty() {
                    return Err("parse func group rule failed, err: caller group is empty.".into());
                }
                if !self.func_group_set.contains(caller_group) {
                    return Err(format!(
                        "parse func group rule failed, function caller group in policy rule not exist in TENANT_GROUP, err group: {caller_group}"
                    ));
                }
                let callee_obj = callee_rules
                    .as_object()
                    .ok_or_else(|| format!("caller group {caller_group} rule must be object"))?;
                let mut single_rule = SingleRule::new();
                for (callee_group, funcs) in callee_obj {
                    if callee_group.is_empty() {
                        return Err("func callee group is empty.".into());
                    }
                    if !self.func_group_set.contains(callee_group) {
                        return Err(format!(
                            "function callee group in policy rule not exist in TENANT_GROUP, err group: {callee_group}"
                        ));
                    }
                    if !tenants_empty(funcs) {
                        single_rule.insert(callee_group.clone(), string_set(funcs)?);
                    }
                }
                caller_rule.insert(caller_group.clone(), single_rule);
            }
            self.allow_rules.insert(method.to_string(), caller_rule);
        }
        Ok(())
    }

    fn parse_deny_rules(&mut self, deny: &Value) -> Result<(), String> {
        let obj = deny
            .as_object()
            .ok_or_else(|| "deny rules must be object".to_string())?;
        if let Some(tenants) = obj.get(DENY_POLICY_TENANT_LIST) {
            self.tenant_blacklist = string_set(tenants)?;
        }
        Ok(())
    }

    fn group_from_tenant(&self, tenant_id: &str) -> String {
        self.func_groups
            .iter()
            .find(|g| g.tenant_id == tenant_id)
            .map(|g| g.func_group.clone())
            .unwrap_or_else(|| EXTERNAL_TENANT_GROUP.to_string())
    }

    fn is_func_in_list(
        &self,
        func_rule_list: &HashSet<String>,
        param: &AuthorizeParam,
    ) -> Result<(), String> {
        if func_rule_list.contains(WHITE_LIST) && self.func_whitelist.contains_key(&param.func_name)
        {
            let tenants = self.func_whitelist.get(&param.func_name).expect("checked");
            if tenants.contains(&param.caller_tenant_id) {
                return Ok(());
            }
            return Err(format!(
                "tenantID: {} not in the whitelist of function: {}",
                param.caller_tenant_id, param.func_name
            ));
        }
        if func_rule_list.contains(ALL_TENANT) {
            return Ok(());
        }
        if func_rule_list.contains(SAME_TENANT) {
            if param.caller_tenant_id == param.callee_tenant_id {
                return Ok(());
            }
            return Err(format!(
                "caller and callee not same, callerTenantID: {}, calleeTenantID: {}",
                param.caller_tenant_id, param.callee_tenant_id
            ));
        }
        if func_rule_list.contains(&param.func_name) {
            return Ok(());
        }
        Err("no allow rule satisfied.".into())
    }
}

fn required<'a>(v: &'a Value, key: &str) -> Result<&'a Value, String> {
    v.get(key)
        .ok_or_else(|| format!("{key} not found in policy."))
}

fn string_set(v: &Value) -> Result<HashSet<String>, String> {
    let arr = v
        .as_array()
        .ok_or_else(|| "expected string array".to_string())?;
    arr.iter()
        .map(|item| {
            item.as_str()
                .map(str::to_string)
                .ok_or_else(|| "expected string array".to_string())
        })
        .collect()
}

fn tenants_empty(v: &Value) -> bool {
    match v {
        Value::Array(a) => a.is_empty(),
        Value::Object(o) => o.is_empty(),
        Value::Null => true,
        _ => false,
    }
}

fn check_authorize_param(param: &AuthorizeParam) -> Result<(), String> {
    if param.caller_tenant_id.is_empty() {
        return Err(
            "CheckAuthorizeParamValid failed, caller tenantID in authorizeParam is empty.".into(),
        );
    }
    if param.callee_tenant_id.is_empty() {
        return Err(
            "CheckAuthorizeParamValid failed, callee tenantID in authorizeParam is empty".into(),
        );
    }
    if param.call_method.is_empty() {
        return Err(
            "CheckAuthorizeParamValid failed, callMethod in authorizeParam is empty".into(),
        );
    }
    Ok(())
}
