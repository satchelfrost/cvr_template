#define NOB_STRIP_PREFIX
#define NOB_IMPLEMENTATION
#include "nob.h"

#define BUILD "build/"
#define LINUX BUILD "linux/"
#define SRC "src/"
#define EXTERNAL SRC "external/"
#define EXEC "main"
#define SHADERS "shaders/"

const char *core[] = {
    SRC"core.c",
    SRC"cvr.h",
    SRC"rvk.h",
};

const char *shaders[] = {
    "standard_rectangle_2D.vert.glsl",
    "standard_rectangle_2D.frag.glsl",
    "point_cloud.vert.glsl",
    "point_cloud.frag.glsl",
    "line.vert.glsl",
    "line.frag.glsl",
};

bool compile_shaders(Cmd *cmd)
{
    for (size_t i = 0; i < ARRAY_LEN(shaders); i++) {
        const char *src = temp_sprintf(SHADERS"%s", shaders[i]);
        const char *dst = temp_sprintf(SHADERS"%s.spv", shaders[i]);

        if (needs_rebuild(dst, &src, 1)) {
            char *stage = NULL;
            if (strstr(shaders[i], "frag")) stage = "-fshader-stage=frag";
            if (strstr(shaders[i], "vert")) stage = "-fshader-stage=vert";
            if (strstr(shaders[i], "comp")) stage = "-fshader-stage=comp";
            assert(stage && "shader stage unrecognize");
            cmd_append(cmd, "glslc", stage, "-o", dst, src);
            if (!cmd_run(cmd)) return false;
        }
    }

    return true;
}


bool build_glfw_linux(Cmd *cmd)
{
    if (file_exists(LINUX "rglfw.o")) return true;

    cmd_append(cmd, "gcc", "-Wall", "-Wextra", "-g");
    cmd_append(cmd, "-c", EXTERNAL "rglfw.c");
    cmd_append(cmd, "-o", LINUX "rglfw.o");
    cmd_append(cmd, "-lm");
    return cmd_run(cmd);
}

bool build_cvr_linux(Cmd *cmd, bool force_rebuild)
{
    if (!needs_rebuild(LINUX"core.o", core, ARRAY_LEN(core)) && !force_rebuild) return true;
    cmd_append(cmd, "gcc", "-Wall", "-Wextra", "-g");
    cmd_append(cmd, "-DVULKAN_VALIDATION_ON");
    cmd_append(cmd, "-I./"EXTERNAL);
    cmd_append(cmd, "-c", SRC"core.c");
    cmd_append(cmd, "-o", LINUX"core.o");
    cmd_append(cmd, "-lm");
    return cmd_run(cmd);
}

bool build_example_linux(Cmd *cmd, bool force_rebuild, const char *example_name)
{
    const char *src  = temp_sprintf(SRC"%s.c", example_name);
    const char *exec = temp_sprintf(LINUX"%s", example_name);
    bool src_touched = needs_rebuild1(exec, src);
    bool core_updated = needs_rebuild1(exec, LINUX"core.o");
    if (!src_touched && !core_updated && !force_rebuild) return true;

    cmd_append(cmd, "gcc", "-Wall", "-Wextra", "-g");
    cmd_append(cmd, "-I./"EXTERNAL);
    cmd_append(cmd, "-I./"SRC);
    cmd_append(cmd, "-o", exec);
    cmd_append(cmd, src, LINUX"rglfw.o", LINUX"core.o");
    cmd_append(cmd, "-lm", "-lvulkan");
    return cmd_run(cmd);
}

int main(int argc, char **argv)
{
    NOB_GO_REBUILD_URSELF(argc, argv);

    if (!mkdir_if_not_exists(BUILD)) return 1;
    if (!mkdir_if_not_exists(LINUX)) return 1;

    Cmd cmd = {0};

    if (!build_glfw_linux(&cmd)) return 1;
    if (!build_cvr_linux(&cmd, false)) return 1;
    if (!build_example_linux(&cmd, false, "dvd_logo")) return 1;
    if (!compile_shaders(&cmd)) return 1;

    return 0;
}
