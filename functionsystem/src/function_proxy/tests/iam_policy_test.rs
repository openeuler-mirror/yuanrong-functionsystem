//! C++ `function_proxy/common/iam` policy compatibility tests.

use yr_proxy::iam_policy::{AuthorizeParam, IamPolicy};

fn same_tenant_policy() -> &'static str {
    r#"{
        "tenant_group": { "external": {} },
        "white_list": {},
        "policy": {
            "allow": {
                "invoke": { "external": { "external": ["="] } },
                "create": { "external": { "external": ["="] } }
            },
            "deny": {}
        }
    }"#
}

#[test]
fn iam_policy_allows_same_tenant_symbol_and_denies_cross_tenant() {
    let policy = IamPolicy::parse(same_tenant_policy()).expect("parse policy");

    policy
        .authorize(&AuthorizeParam {
            caller_tenant_id: "tenant-a".into(),
            callee_tenant_id: "tenant-a".into(),
            call_method: "invoke".into(),
            func_name: "callee".into(),
        })
        .expect("same tenant is allowed");

    let err = policy
        .authorize(&AuthorizeParam {
            caller_tenant_id: "tenant-a".into(),
            callee_tenant_id: "tenant-b".into(),
            call_method: "invoke".into(),
            func_name: "callee".into(),
        })
        .expect_err("cross tenant must be denied");
    assert!(err.contains("caller and callee not same"), "{err}");
}

#[test]
fn iam_policy_blacklist_overrides_allow_rules() {
    let policy = IamPolicy::parse(
        r#"{
            "tenant_group": { "external": {} },
            "white_list": {},
            "policy": {
                "allow": { "invoke": { "external": { "external": ["*"] } } },
                "deny": { "tenant_list": ["tenant-a"] }
            }
        }"#,
    )
    .expect("parse policy");

    let err = policy
        .authorize(&AuthorizeParam {
            caller_tenant_id: "tenant-a".into(),
            callee_tenant_id: "tenant-b".into(),
            call_method: "invoke".into(),
            func_name: "callee".into(),
        })
        .expect_err("blacklisted caller must be denied");
    assert!(err.contains("blacklist"), "{err}");
}

#[test]
fn iam_policy_white_list_rule_checks_function_tenant_list() {
    let policy = IamPolicy::parse(
        r#"{
            "tenant_group": { "external": {} },
            "white_list": { "callee": ["tenant-a"] },
            "policy": {
                "allow": { "invoke": { "external": { "external": ["white_list"] } } },
                "deny": {}
            }
        }"#,
    )
    .expect("parse policy");

    policy
        .authorize(&AuthorizeParam {
            caller_tenant_id: "tenant-a".into(),
            callee_tenant_id: "tenant-b".into(),
            call_method: "invoke".into(),
            func_name: "callee".into(),
        })
        .expect("whitelisted tenant is allowed");

    let err = policy
        .authorize(&AuthorizeParam {
            caller_tenant_id: "tenant-c".into(),
            callee_tenant_id: "tenant-b".into(),
            call_method: "invoke".into(),
            func_name: "callee".into(),
        })
        .expect_err("non-whitelisted tenant must be denied");
    assert!(err.contains("not in the whitelist"), "{err}");
}
