//! Runtime executable selection for spawn parameters.

use yr_runtime_manager::executor::pick_runtime_executable;

#[test]
fn pick_returns_none_when_path_list_empty() {
    assert_eq!(pick_runtime_executable(&[], "python"), None);
}

#[test]
fn pick_numeric_runtime_type_selects_by_index() {
    let paths = vec!["/a/first".into(), "/b/second".into()];
    assert_eq!(
        pick_runtime_executable(&paths, "0"),
        Some("/a/first".into())
    );
    assert_eq!(
        pick_runtime_executable(&paths, "1"),
        Some("/b/second".into())
    );
    // Out-of-range numeric index falls through to substring match, then first path.
    assert_eq!(
        pick_runtime_executable(&paths, "9"),
        Some("/a/first".into())
    );
}

#[test]
fn pick_matches_substring_in_path_or_filename() {
    let paths = vec![
        "/opt/bin/generic-runner".into(),
        "/usr/bin/python3.11".into(),
    ];
    assert_eq!(
        pick_runtime_executable(&paths, "python"),
        Some("/usr/bin/python3.11".into())
    );
}

#[test]
fn pick_falls_back_to_first_entry_when_no_match() {
    let paths = vec!["/bin/sleep".into(), "/bin/true".into()];
    assert_eq!(
        pick_runtime_executable(&paths, "unknown-runtime"),
        Some("/bin/sleep".into())
    );
}
