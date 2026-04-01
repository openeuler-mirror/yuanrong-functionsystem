load("@rules_cc//cc:find_cc_toolchain.bzl", "find_cc_toolchain")

# Shared compiler options for functionsystem C++ targets
# Note: -DVERSION=1 removed — it conflicts with the SortTarget/TargetType enum values
# in meta_store_client headers, and this macro is not used anywhere in the codebase.
COPTS = [
    "-Wno-stringop-overflow",
    "-Werror",
    "-fstack-protector-strong",
    "-Wno-deprecated-declarations",
    "-Wno-unused-function",
    "-Wno-unused-variable",
    "-fPIC",
]

LOPTS = []

def copy_file(name, srcs, dstdir = "", pre_cmd = "echo"):
    if dstdir.startswith("/"):
        fail("Subdirectory must be a relative path: " + dstdir)
    src_locations = " ".join(["$(locations {})".format(src) for src in srcs])
    native.genrule(
        name = name,
        srcs = srcs,
        outs = [name + ".out"],
        cmd = r"""
            mkdir -p -- {dstdir}
            for f in {locations}; do
                {pre_cmd} "$$f"
                rm -f -- {dstdir}$${{f##*/}}
                cp -af -- "$$f" {dstdir}
            done
            date > $@
        """.format(
            locations = src_locations,
            dstdir = "." + ("/" + dstdir).rstrip("/") + "/",
            pre_cmd = pre_cmd,
        ),
        local = 1,
        tags = ["no-cache"],
    )

def _filter_files_with_suffix_impl(ctx):
    suffix = ctx.attr.suffix
    filtered_files = [f for f in ctx.files.srcs if suffix in f.basename]
    return [
        DefaultInfo(
            files = depset(filtered_files),
        ),
    ]

filter_files_with_suffix = rule(
    implementation = _filter_files_with_suffix_impl,
    attrs = {
        "srcs": attr.label_list(allow_files = True),
        "suffix": attr.string(),
    },
)

def _cc_strip_impl(ctx):
    compilation_mode = ctx.var["COMPILATION_MODE"]
    if compilation_mode == "dbg":
        return [
            DefaultInfo(
                files = depset(ctx.files.srcs),
            ),
        ]
    cc_toolchain = find_cc_toolchain(ctx)
    input_files = ctx.files.srcs
    in_files_path = []
    output_files = []
    for s in ctx.attr.srcs:
        for f in s.files.to_list():
            in_files_path.append(f.path)
            output_files.append(ctx.actions.declare_file("%s_dir/%s" % (f.basename, f.basename), sibling = f))

    commands = [
        """chmod +w {obj} &&
        {obj_cpy} --only-keep-debug {obj} {dest} &&
        {obj_cpy} --add-gnu-debuglink={dest} {obj} &&
        {strip} --strip-all {obj} &&
        mkdir -p build/output/symbols && cp {dest} build/output/symbols
        mkdir -p {output_dir}
        cp -fr {obj} {output_dir}
        """.format(
            obj_cpy = cc_toolchain.objcopy_executable,
            strip = cc_toolchain.strip_executable,
            obj = src,
            dest = src + ".sym",
            output_dir = src + "_dir",
        )
        for src in in_files_path
    ]

    ctx.actions.run_shell(
        inputs = depset(
            direct = input_files,
            transitive = [
                cc_toolchain.all_files,
            ],
        ),
        outputs = output_files,
        progress_message = "CcStripping %s" % in_files_path,
        command = "".join(commands),
        mnemonic = "CcStrip",
    )
    return [
        DefaultInfo(
            files = depset(output_files),
        ),
    ]

cc_strip = rule(
    implementation = _cc_strip_impl,
    fragments = ["cpp"],
    attrs = {
        "_cc_toolchain": attr.label(
            default = Label(
                "@rules_cc//cc:current_cc_toolchain",
            ),
        ),
        "srcs": attr.label_list(allow_files = True, mandatory = True),
        "out": attr.output(),
    },
    toolchains = [
        "@rules_cc//cc:toolchain_type",
    ],
)
