//! Plugin name and scoring constants (`schedule_plugin/common/constants.h`).

pub const DEFAULT_PREFILTER_NAME: &str = "DefaultPreFilter";

pub const DEFAULT_FILTER_NAME: &str = "DefaultFilter";
pub const RESOURCE_SELECTOR_FILTER_NAME: &str = "ResourceSelectorFilter";
pub const DEFAULT_HETEROGENEOUS_FILTER_NAME: &str = "DefaultHeterogeneousFilter";
pub const LABEL_AFFINITY_FILTER_NAME: &str = "LabelAffinityFilter";
pub const RELAXED_ROOT_LABEL_AFFINITY_FILTER_NAME: &str = "RelaxedRootLabelAffinityFilter";
pub const STRICT_ROOT_LABEL_AFFINITY_FILTER_NAME: &str = "StrictRootLabelAffinityFilter";
pub const RELAXED_NON_ROOT_LABEL_AFFINITY_FILTER_NAME: &str = "RelaxedNonRootLabelAffinityFilter";
pub const STRICT_NON_ROOT_LABEL_AFFINITY_FILTER_NAME: &str = "StrictNonRootLabelAffinityFilter";
pub const DISK_FILTER_NAME: &str = "DiskFilter";

pub const DEFAULT_SCORER_NAME: &str = "DefaultScorer";
pub const DEFAULT_HETEROGENEOUS_SCORER_NAME: &str = "DefaultHeterogeneousScorer";
pub const LABEL_AFFINITY_SCORER_NAME: &str = "LabelAffinityScorer";
pub const RELAXED_LABEL_AFFINITY_SCORER_NAME: &str = "RelaxedLabelAffinityScorer";
pub const STRICT_LABEL_AFFINITY_SCORER_NAME: &str = "StrictLabelAffinityScorer";
pub const DISK_SCORER_NAME: &str = "DiskScorer";

pub const DEFAULT_SCORE: i64 = 100;
pub const INVALID_SCORE: f32 = -1.0;
pub const MIN_SCORE_THRESHOLD: f32 = 0.1;
pub const BASE_SCORE_FACTOR: f32 = 1.0;
pub const INVALID_INDEX: i32 = -1;
pub const MONOPOLY_MODE: &str = "monopoly";

pub const HETERO_RESOURCE_FIELD_NUM: usize = 3;
pub const VENDOR_IDX: usize = 0;
pub const PRODUCT_INDEX: usize = 1;
pub const RESOURCE_IDX: usize = 2;

pub const LABEL_AFFINITY_PLUGIN: &str = "LabelAffinitPlugin";
pub const DEFAULT_FILTER_PLUGIN: &str = "DefaultFilterPlugin";
pub const GROUP_SCHEDULE_CONTEXT: &str = "GroupScheduleContext";

pub const RESOURCE_OWNER_KEY: &str = "resource.owner";
pub const DEFAULT_OWNER_VALUE: &str = "default";
