//! `PluginFactory` (`plugin_factory.h` / `.cpp`).

use crate::scheduler_framework::PolicyPlugin;
use parking_lot::Mutex;
use std::collections::HashMap;
use std::sync::OnceLock;

pub type PluginCreator = fn() -> PolicyPlugin;

static GLOBAL_FACTORY: OnceLock<Mutex<PluginFactoryInner>> = OnceLock::new();

struct PluginFactoryInner {
    plugins: HashMap<String, PluginCreator>,
}

impl Default for PluginFactoryInner {
    fn default() -> Self {
        Self {
            plugins: HashMap::new(),
        }
    }
}

/// Factory for scheduler policy plugins (singleton analogue of C++ `PluginFactory`).
pub struct PluginFactory;

impl PluginFactory {
    fn inner() -> &'static Mutex<PluginFactoryInner> {
        GLOBAL_FACTORY.get_or_init(|| Mutex::new(PluginFactoryInner::default()))
    }

    pub fn create_plugin(plugin_name: &str) -> Option<PolicyPlugin> {
        let g = Self::inner().lock();
        g.plugins.get(plugin_name).map(|c| c())
    }

    pub fn register_plugin_creator(plugin_name: impl Into<String>, gen: PluginCreator) -> bool {
        let mut g = Self::inner().lock();
        g.plugins.insert(plugin_name.into(), gen).is_none()
    }
}

/// RAII registration (`PluginRegister` in C++).
pub struct PluginRegister {
    #[allow(dead_code)]
    name: String,
}

impl PluginRegister {
    pub fn new(plugin_name: impl Into<String>, gen: PluginCreator) -> Self {
        let name = plugin_name.into();
        let _ = PluginFactory::register_plugin_creator(name.clone(), gen);
        Self { name }
    }
}
