//! Domain scheduler: sits between global scheduler (function_master) and local schedulers (function_proxy).

pub mod abnormal_processor;
pub mod config;
pub mod election;
pub mod function_meta;
pub mod group;
pub mod heartbeat_observer;
pub mod http_api;
pub mod nodes;
pub mod resource_view;
pub mod schedule_decision;
pub mod scheduler;
pub mod scheduler_framework;
pub mod service;
pub mod state;
pub mod worker_status;

pub use state::DomainSchedulerState;
