#include "cvr.h"

#define RVK_LOG_LEVEL RVK_INFO
#define RVK_IMPLEMENTATION
#include "rvk.h"

#include <GLFW/glfw3.h>

#define NOB_STRIP_PREFIX
#define NOB_IMPLEMENTATION
#include "../nob.h"

#ifdef VULKAN_VALIDATION_ON
    #define VK_VALIDATION 1
#else
    #define VK_VALIDATION 0
#endif

#define RAYMATH_IMPLEMENTATION
#include "raymath.h"

#define Z_NEAR 0.1
#define Z_FAR 500.0

static const char *instance_exts[] = {
    "VK_KHR_surface",
    "VK_KHR_xcb_surface",
#if VK_VALIDATION
    "VK_EXT_debug_utils",
#endif
};
static const char *layers[] = {
#if VK_VALIDATION
    "VK_LAYER_KHRONOS_validation",
#endif
};

static const char *device_exts[] = {"VK_KHR_swapchain"};

static Core_Context ctx = {0};

#define MAX_MAT_STACK 1024 * 1024
static Matrix mat_stack[MAX_MAT_STACK];
static size_t mat_stack_p = 0;

static struct {
    Matrix view;
    Matrix proj;
    Matrix view_proj;
} matrices = {0};

/* standard pipelines for */
static struct {
    struct {
        VkPipeline pipeline;
        VkPipelineLayout pipeline_layout;
        Rvk_Buffer idx;
        Rvk_Buffer vtx;
    } primitive_2D;
    struct {
        VkPipeline pipeline;
        VkPipelineLayout pipeline_layout;
        Rvk_Buffer bounding_box_vtx;
        Rvk_Buffer bounding_box_idx;
        Rvk_Buffer frustum_vtx;
        Rvk_Buffer frustum_idx;
    } line;
    struct {
        VkPipeline pipeline;
        VkPipelineLayout pipeline_layout;
    } dyn_line;
} standard = {0};

Rvk_Primitive_2D_Vertex primitive_2D_vertices[] = {
    {{-1.0f, -1.0f}, {1.0f, 0.0f, 0.0f}},
    {{ 1.0f, -1.0f}, {0.0f, 1.0f, 0.0f}},
    {{-1.0f,  1.0f}, {0.0f, 0.0f, 1.0f}},
    {{ 1.0f,  1.0f}, {1.0f, 1.0f, 1.0f}},
};

uint16_t primitive_2D_indices[] = {0, 1, 2, 2, 1, 3};

Rvk_Line_Vertex bounding_box_vertices[8] = {
    {.position = {-0.5f, -0.5f,  0.5f}, .color = 0xff000000},
    {.position = { 0.5f, -0.5f,  0.5f}, .color = 0xff000000},
    {.position = {-0.5f,  0.5f,  0.5f}, .color = 0xff000000},
    {.position = { 0.5f,  0.5f,  0.5f}, .color = 0xff000000},
    {.position = {-0.5f, -0.5f, -0.5f}, .color = 0xff000000},
    {.position = { 0.5f, -0.5f, -0.5f}, .color = 0xff000000},
    {.position = {-0.5f,  0.5f, -0.5f}, .color = 0xff000000},
    {.position = { 0.5f,  0.5f, -0.5f}, .color = 0xff000000},
};
uint16_t bounding_box_indices[24] = {0, 1, 2, 3, 0, 2, 1, 3,
                                     4, 5, 6, 7, 4, 6, 5, 7,
                                     0, 4, 1, 5, 2, 6, 3, 7};

Rvk_Line_Vertex frustum_vertices[8] = {
    {.position = {-1.0f, -1.0f, 0.0f}, .color = 0xff000000},
    {.position = { 1.0f, -1.0f, 0.0f}, .color = 0xff000000},
    {.position = {-1.0f,  1.0f, 0.0f}, .color = 0xff000000},
    {.position = { 1.0f,  1.0f, 0.0f}, .color = 0xff000000},
    {.position = {-1.0f, -1.0f, 1.0f}, .color = 0xff000000},
    {.position = { 1.0f, -1.0f, 1.0f}, .color = 0xff000000},
    {.position = {-1.0f,  1.0f, 1.0f}, .color = 0xff000000},
    {.position = { 1.0f,  1.0f, 1.0f}, .color = 0xff000000},
};
uint16_t frustum_indices[24] = {0, 1, 2, 3, 0, 2, 1, 3,
                                4, 5, 6, 7, 4, 6, 5, 7,
                                0, 4, 1, 5, 2, 6, 3, 7};

#define MAX_KEYBOARD_KEYS 512
#define MAX_KEY_PRESSED_QUEUE 16
#define MAX_CHAR_PRESSED_QUEUE 16
#define CAMERA_MOVE_SPEED 10.0f
#define CAMERA_MOUSE_MOVE_SENSITIVITY 0.001f
#define CAMERA_ROT_SENSITIVITY 0.1f
#define GAMEPAD_ROT_SENSITIVITY 1.0f
#define MAX_MOUSE_BUTTONS 8
#define MAX_GAMEPAD_BUTTONS 32
#define MAX_GAMEPAD_AXIS 8
#define FPS_CAPTURE_FRAMES_COUNT 30
#define FPS_AVERAGE_TIME_SECONDS 0.5f
#define FPS_STEP (FPS_AVERAGE_TIME_SECONDS/FPS_CAPTURE_FRAMES_COUNT)
#define DEAD_ZONE 0.25f

struct {
    int exit_key;
    char curr_key_state[MAX_KEYBOARD_KEYS];
    char prev_key_state[MAX_KEYBOARD_KEYS];
    char key_repeat_in_frame[MAX_KEYBOARD_KEYS];
    int key_pressed_queue[MAX_KEY_PRESSED_QUEUE];
    int key_pressed_queue_count;
    int char_pressed_queue[MAX_CHAR_PRESSED_QUEUE];
    int char_pressed_queue_count;
} keyboard;

struct {
    Vector2 prev_pos;
    Vector2 curr_pos;
    Vector2 curr_wheel_move;
    Vector2 prev_wheel_move;
    char curr_button_state[MAX_MOUSE_BUTTONS];
    char prev_button_state[MAX_MOUSE_BUTTONS];
} mouse;

struct {
    float axis_state[MAX_GAMEPAD_AXIS];
    char curr_button_state[MAX_GAMEPAD_BUTTONS];
    char prev_button_state[MAX_GAMEPAD_BUTTONS];
    int last_button_pressed;
} gamepad;

static void key_callback(GLFWwindow *window, int key, int scancode, int action, int mods);
static void mouse_cursor_pos_callback(GLFWwindow *window, double x, double y);
static void mouse_button_callback(GLFWwindow *window, int button, int action, int mods);
static void mouse_scroll_callback(GLFWwindow *window, double x_offset, double y_offset);

bool init_window(int width, int height, char *title)
{
    /* initialize glfw and window */
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    ctx.window = glfwCreateWindow(width, height, title, NULL, NULL);
    glfwSetKeyCallback(ctx.window, key_callback);
    glfwSetMouseButtonCallback(ctx.window, mouse_button_callback);
    glfwSetCursorPosCallback(ctx.window, mouse_cursor_pos_callback);
    glfwSetScrollCallback(ctx.window, mouse_scroll_callback);

    VkDebugUtilsMessengerCreateInfoEXT debug_messenger_ci = r_get_debug_messenger_info();

    /* create vulkan instance (w/ or w/o validation layers i.e. VK_VALIDATION = 1/0) */
    if (!vk_create_instance(NULL, &ctx.instance,
                            .pNext = (VK_VALIDATION) ? &debug_messenger_ci : NULL,
                            .ppEnabledLayerNames = layers,
                            .enabledLayerCount = RVK_ARRAY_LEN(layers),
                            .ppEnabledExtensionNames = instance_exts,
                            .enabledExtensionCount = RVK_ARRAY_LEN(instance_exts))) return false;

    /* create the vulkan surface */
    if (!RVK(glfwCreateWindowSurface(ctx.instance, ctx.window, NULL, &ctx.surface))) return false;

    /* TODO: at some point I might want to factor out device config to something that happens before init_window */
    Rvk_Device_Config device_config = {
        .extension_count = RVK_ARRAY_LEN(device_exts),
        .extensions = device_exts,
        .layer_count = RVK_ARRAY_LEN(layers),
        .layers = layers,
    };
    ctx.device = r_create_rvk_device(ctx.instance, ctx.surface, device_config);
    if (!ctx.device.logical) return false;

    /* create swapchain */
    ctx.swapchain = r_create_rvk_swapchain(ctx.device, ctx.surface, width, height);
    if (!ctx.swapchain.handle) return false;

    return true;
}

void close_window()
{
    vkQueueWaitIdle(ctx.device.queue);

    if (standard.primitive_2D.pipeline) {
        r_destroy_rvk_buffer(ctx.device.logical, standard.primitive_2D.vtx);
        r_destroy_rvk_buffer(ctx.device.logical, standard.primitive_2D.idx);
        vkDestroyPipeline(ctx.device.logical, standard.primitive_2D.pipeline, NULL);
        vkDestroyPipelineLayout(ctx.device.logical, standard.primitive_2D.pipeline_layout, NULL);
    }

    if (standard.line.pipeline) {
        r_destroy_rvk_buffer(ctx.device.logical, standard.line.bounding_box_vtx);
        r_destroy_rvk_buffer(ctx.device.logical, standard.line.bounding_box_idx);
        vkDestroyPipeline(ctx.device.logical, standard.line.pipeline, NULL);
        vkDestroyPipelineLayout(ctx.device.logical, standard.line.pipeline_layout, NULL);
    }
    if (standard.dyn_line.pipeline) {
        vkDestroyPipeline(ctx.device.logical, standard.dyn_line.pipeline, NULL);
        vkDestroyPipelineLayout(ctx.device.logical, standard.dyn_line.pipeline_layout, NULL);
    }

    r_destroy_rvk_swapchain(ctx.device.logical, ctx.swapchain);
    r_destroy_rvk_device(ctx.device);
    vkDestroySurfaceKHR(ctx.instance, ctx.surface, NULL);
    vkDestroyInstance(ctx.instance, NULL);
}

struct {
    Vector4 color;
    Vector4 pos_width;
} primitive_2D_push_const;

bool create_primitive_2D_pipeline_()
{
    bool result = true;
    VkPipelineShaderStageCreateInfo stages[2];
    String_Builder sb = {0};

    VkPushConstantRange pk_range = {
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
        .size = sizeof(primitive_2D_push_const),
    };
    if (!vk_create_pipeline_layout(ctx.device.logical,
                                   NULL,
                                   &standard.primitive_2D.pipeline_layout,
                                   .pushConstantRangeCount = 1,
                                   .pPushConstantRanges = &pk_range)) return_defer(false);

    /* load shaders */
    if (!read_entire_file("shaders/standard_rectangle_2D.vert.glsl.spv", &sb)) return_defer(false);
    stages[0] = r_create_vertex_stage_ci(ctx.device.logical, sb.count, (uint32_t*)sb.items);
    if (!stages[0].module) return_defer(false);
    sb.count = 0; // reuse memory
    if (!read_entire_file("shaders/standard_rectangle_2D.frag.glsl.spv", &sb)) return_defer(false);
    stages[1] = r_create_fragment_stage_ci(ctx.device.logical, sb.count, (uint32_t*)sb.items);
    if (!stages[0].module) return_defer(false);
    sb.count = 0;

    /* temporary allocator for creating graphics pipeline */
    size_t temp_alloc_save_point = r_temp_save();
    if (!vk_create_graphics_pipeline(ctx.device.logical, NULL, NULL, &standard.primitive_2D.pipeline,
                                     .stageCount = ARRAY_LEN(stages),
                                     .pStages = stages,
                                     .pVertexInputState = r_temp_default_primitive_2D_vertex_input_state_ci(),
                                     .pInputAssemblyState = r_temp_default_input_assembly_state_ci(),
                                     .pViewportState = r_temp_default_viewport_state_ci(ctx.swapchain.extent),
                                     .pRasterizationState = r_temp_default_rasterization_state_ci(),
                                     .pMultisampleState = r_temp_default_multisample_state_ci(),
                                     .pDepthStencilState = r_temp_default_depth_stencil_state_ci(),
                                     .pColorBlendState = r_temp_default_color_blend_state_ci(),
                                     .pDynamicState = r_temp_default_dynamic_state_ci(),
                                     .layout = standard.primitive_2D.pipeline_layout,
                                     .renderPass = ctx.swapchain.render_pass)) return_defer(false);
    r_temp_rewind(temp_alloc_save_point);
    vkDestroyShaderModule(ctx.device.logical, stages[0].module, NULL);
    vkDestroyShaderModule(ctx.device.logical, stages[1].module, NULL);

    /* create and upload vertex/index buffers */
    size_t size = ARRAY_LEN(primitive_2D_vertices)*sizeof(*primitive_2D_vertices);
    standard.primitive_2D.vtx = r_create_vertex_buffer(ctx.device, size, primitive_2D_vertices);
    if (!standard.primitive_2D.vtx.info.buffer) return_defer(false);

    size = ARRAY_LEN(primitive_2D_indices)*sizeof(*primitive_2D_indices);
    standard.primitive_2D.idx = r_create_index_buffer(ctx.device, size, primitive_2D_indices);
    if (!standard.primitive_2D.idx.info.buffer) return_defer(false);

defer:
    sb_free(sb);
    return result;
}

bool draw_rectangle(int x, int y, int width, int height, Color color)
{
    if (!standard.primitive_2D.pipeline) if (!create_primitive_2D_pipeline_()) return false;

    VkCommandBuffer cb = ctx.device.cmd_buffs[ctx.current_frame];
    vkCmdBindPipeline(cb, 0, standard.primitive_2D.pipeline);
    primitive_2D_push_const.color = (Vector4){color.r/255.0f, color.g/255.0f, color.g/255.0f, color.a/255.0f};
    primitive_2D_push_const.pos_width = (Vector4){x, y, width, height};
    vkCmdPushConstants(cb, standard.primitive_2D.pipeline_layout,
                       VK_SHADER_STAGE_VERTEX_BIT, 0,
                       sizeof(primitive_2D_push_const), &primitive_2D_push_const);
    r_cmd_set_viewport_scissor(cb, ctx.swapchain.extent);
    r_cmd_draw_buffers(cb, standard.primitive_2D.vtx.info.buffer,
                       standard.primitive_2D.idx.info.buffer, ARRAY_LEN(primitive_2D_indices));

    return true;
}

struct {
    float16 mvp;
    Vector4 color;
} line_push_const;

bool create_line_pipeline_()
{
    bool result = true;
    VkPipelineShaderStageCreateInfo stages[2];
    String_Builder sb = {0};
    Core_Context ctx = get_core_context();

    VkPushConstantRange pc_range = {
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
        .size = sizeof(line_push_const),
    };
    if (!vk_create_pipeline_layout(ctx.device.logical,
                                   NULL,
                                   &standard.line.pipeline_layout,
                                   .pushConstantRangeCount = 1,
                                   .pPushConstantRanges = &pc_range)) return_defer(false);

    /* load shaders */
    if (!read_entire_file("shaders/line.vert.glsl.spv", &sb)) return_defer(false);
    stages[0] = r_create_vertex_stage_ci(ctx.device.logical, sb.count, (uint32_t*)sb.items);
    if (!stages[0].module) return_defer(false);
    sb.count = 0; // reuse memory
    if (!read_entire_file("shaders/line.frag.glsl.spv", &sb)) return_defer(false);
    stages[1] = r_create_fragment_stage_ci(ctx.device.logical, sb.count, (uint32_t*)sb.items);
    if (!stages[0].module) return_defer(false);
    sb.count = 0;

    /* temporary allocator for creating graphics pipeline */
    size_t temp_alloc_save_point = r_temp_save();
    if (!vk_create_graphics_pipeline(ctx.device.logical, NULL, NULL, &standard.line.pipeline,
                                     .stageCount = ARRAY_LEN(stages),
                                     .pStages = stages,
                                     .pVertexInputState = r_temp_default_primitive_line_input_state_ci(),
                                     .pInputAssemblyState = r_temp_default_line_input_assembly_state_ci(),
                                     .pViewportState = r_temp_default_viewport_state_ci(ctx.swapchain.extent),
                                     .pRasterizationState = r_temp_default_line_rasterization_state_ci(),
                                     .pMultisampleState = r_temp_default_multisample_state_ci(),
                                     .pDepthStencilState = r_temp_default_depth_stencil_state_ci(),
                                     .pColorBlendState = r_temp_default_color_blend_state_ci(),
                                     .pDynamicState = r_temp_default_dynamic_state_ci(),
                                     .layout = standard.line.pipeline_layout,
                                     .renderPass = ctx.swapchain.render_pass)) return_defer(false);
    r_temp_rewind(temp_alloc_save_point);
    vkDestroyShaderModule(ctx.device.logical, stages[0].module, NULL);
    vkDestroyShaderModule(ctx.device.logical, stages[1].module, NULL);

defer:
    sb_free(sb);
    return result;
}

struct {
    float16 mvp;
    Vector4 color;
    Vector4 start;
    Vector4 end;
} dyn_line_push_const;

bool create_dynamic_line_pipeline_()
{
    bool result = true;
    VkPipelineShaderStageCreateInfo stages[2];
    String_Builder sb = {0};
    Core_Context ctx = get_core_context();

    VkPushConstantRange pc_range = {
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
        .size = sizeof(dyn_line_push_const),
    };
    if (!vk_create_pipeline_layout(ctx.device.logical,
                                   NULL,
                                   &standard.dyn_line.pipeline_layout,
                                   .pushConstantRangeCount = 1,
                                   .pPushConstantRanges = &pc_range)) return_defer(false);

    /* load shaders */
    if (!read_entire_file("shaders/dynamic_line.vert.glsl.spv", &sb)) return_defer(false);
    stages[0] = r_create_vertex_stage_ci(ctx.device.logical, sb.count, (uint32_t*)sb.items);
    if (!stages[0].module) return_defer(false);
    sb.count = 0; // reuse memory
    if (!read_entire_file("shaders/dynamic_line.frag.glsl.spv", &sb)) return_defer(false);
    stages[1] = r_create_fragment_stage_ci(ctx.device.logical, sb.count, (uint32_t*)sb.items);
    if (!stages[0].module) return_defer(false);
    sb.count = 0;

    /* temporary allocator for creating graphics pipeline */
    VkPipelineVertexInputStateCreateInfo empty_input_state = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO
    };
    size_t temp_alloc_save_point = r_temp_save();
    if (!vk_create_graphics_pipeline(ctx.device.logical, NULL, NULL, &standard.dyn_line.pipeline,
                                     .stageCount = ARRAY_LEN(stages),
                                     .pStages = stages,
                                     .pVertexInputState = &empty_input_state,
                                     .pInputAssemblyState = r_temp_default_line_input_assembly_state_ci(),
                                     .pViewportState = r_temp_default_viewport_state_ci(ctx.swapchain.extent),
                                     .pRasterizationState = r_temp_default_line_rasterization_state_ci(),
                                     .pMultisampleState = r_temp_default_multisample_state_ci(),
                                     .pDepthStencilState = r_temp_default_depth_stencil_state_ci(),
                                     .pColorBlendState = r_temp_default_color_blend_state_ci(),
                                     .pDynamicState = r_temp_default_dynamic_state_ci(),
                                     .layout = standard.dyn_line.pipeline_layout,
                                     .renderPass = ctx.swapchain.render_pass)) return_defer(false);
    r_temp_rewind(temp_alloc_save_point);
    vkDestroyShaderModule(ctx.device.logical, stages[0].module, NULL);
    vkDestroyShaderModule(ctx.device.logical, stages[1].module, NULL);

defer:
    sb_free(sb);
    return result;
}

bool draw_line(Vector3 start, Vector3 end, Color color)
{
    if (!standard.dyn_line.pipeline) if (!create_dynamic_line_pipeline_()) return false;

    dyn_line_push_const.mvp   = MatrixToFloatV(get_model_view_projection());
    dyn_line_push_const.color = (Vector4){color.r/255.0f, color.g/255.0f, color.b/255.0f, color.a/255.0f};
    dyn_line_push_const.start = (Vector4){start.x, start.y, start.z, 1.0f};
    dyn_line_push_const.end   = (Vector4){end.x, end.y, end.z, 1.0f};

    VkCommandBuffer cb = ctx.device.cmd_buffs[ctx.current_frame];
    vkCmdBindPipeline(cb, 0, standard.dyn_line.pipeline);
    vkCmdPushConstants(cb, standard.dyn_line.pipeline_layout,
                       VK_SHADER_STAGE_VERTEX_BIT, 0,
                       sizeof(dyn_line_push_const), &dyn_line_push_const);
    r_cmd_set_viewport_scissor(cb, ctx.swapchain.extent);
    vkCmdDraw(cb, 2, 1, 0, 0);

    return true;
}

bool draw_wireframe_box_from_mat_stack(Color color)
{
    if (!standard.line.pipeline) if (!create_line_pipeline_()) return false;

    if (!standard.line.bounding_box_vtx.info.buffer) {
        size_t bb_vtx_count = ARRAY_LEN(bounding_box_vertices);
        size_t bb_idx_count = ARRAY_LEN(bounding_box_indices);
        standard.line.bounding_box_vtx = r_create_vertex_buffer(ctx.device,
                                                                bb_vtx_count*sizeof(Rvk_Line_Vertex),
                                                                bounding_box_vertices);
        standard.line.bounding_box_idx = r_create_index_buffer(ctx.device,
                                                               bb_idx_count*sizeof(uint16_t),
                                                               bounding_box_indices);
    }

    VkCommandBuffer cb = ctx.device.cmd_buffs[ctx.current_frame];
    vkCmdBindPipeline(cb, 0, standard.line.pipeline);
    line_push_const.mvp   = MatrixToFloatV(get_model_view_projection());
    line_push_const.color = (Vector4){color.r/255.0f, color.g/255.0f, color.b/255.0f, color.a/255.0f};

    vkCmdPushConstants(cb, standard.line.pipeline_layout,
                       VK_SHADER_STAGE_VERTEX_BIT, 0,
                       sizeof(line_push_const), &line_push_const);
    r_cmd_set_viewport_scissor(cb, ctx.swapchain.extent);
    r_cmd_draw_buffers(cb, standard.line.bounding_box_vtx.info.buffer,
                       standard.line.bounding_box_idx.info.buffer, ARRAY_LEN(bounding_box_indices));

    return true;
}

bool draw_frustum(Color color, Camera camera)
{
    if (!standard.line.pipeline) if (!create_line_pipeline_()) return false;

    if (!standard.line.frustum_vtx.info.buffer) {

        size_t vtx_count = ARRAY_LEN(frustum_vertices);
        size_t idx_count = ARRAY_LEN(frustum_indices);
        standard.line.frustum_vtx = r_create_vertex_buffer(ctx.device,
                                                           vtx_count*sizeof(Rvk_Line_Vertex),
                                                           frustum_vertices);
        standard.line.frustum_idx = r_create_index_buffer(ctx.device,
                                                          idx_count*sizeof(uint16_t),
                                                          frustum_indices);
    }

    VkCommandBuffer cb = ctx.device.cmd_buffs[ctx.current_frame];
    vkCmdBindPipeline(cb, 0, standard.line.pipeline);

    /* calculate the model matrix of the camera whose frstum we want to view */
    Matrix camera_model_matrix = MatrixInvert(MatrixLookAt(camera.position, camera.target, camera.up));
    double aspect = ctx.swapchain.extent.width / (double)ctx.swapchain.extent.height;
    Matrix proj = MatrixPerspective(camera.fovy * DEG2RAD, aspect, Z_NEAR, Z_FAR/10.0f);
    camera_model_matrix = MatrixMultiply(MatrixInvert(proj), camera_model_matrix);

    line_push_const.mvp = MatrixToFloatV(MatrixMultiply(camera_model_matrix, matrices.view_proj));
    line_push_const.color = (Vector4){color.r/255.0f, color.g/255.0f, color.b/255.0f, color.a/255.0f};

    vkCmdPushConstants(cb, standard.line.pipeline_layout,
                       VK_SHADER_STAGE_VERTEX_BIT, 0,
                       sizeof(line_push_const), &line_push_const);
    r_cmd_set_viewport_scissor(cb, ctx.swapchain.extent);
    r_cmd_draw_buffers(cb, standard.line.frustum_vtx.info.buffer,
                       standard.line.frustum_idx.info.buffer, ARRAY_LEN(frustum_indices));

    return true;
}

bool window_should_close()
{
    bool result = (glfwGetKey(ctx.window, GLFW_KEY_ESCAPE) == GLFW_PRESS) || glfwWindowShouldClose(ctx.window);
    glfwPollEvents();
    return result;
}

bool begin_drawing(Color bg_color)
{
    begin_timer();
    if (!r_wait_reset_fence(ctx.device.logical, &ctx.device.fences[ctx.current_frame])) return false;
    if (!r_acquire_next_image(ctx.device.logical, ctx.swapchain.handle,
                              ctx.device.image_available_sems[ctx.current_frame], &ctx.img_idx)) return false;
    if (!r_reset_begin_cmd_buff(ctx.device.cmd_buffs[ctx.current_frame])) return 1;
    r_cmd_begin_render_pass(ctx.device.cmd_buffs[ctx.current_frame],
                            ctx.swapchain.render_pass, ctx.swapchain.framebuffers[ctx.img_idx],
                            ctx.swapchain.extent,
                            bg_color.r / 255.0f,
                            bg_color.g / 255.0f,
                            bg_color.b / 255.0f,
                            bg_color.a / 255.0f);
    return true;
}

void poll_input_events();

bool end_drawing()
{
    vkCmdEndRenderPass(ctx.device.cmd_buffs[ctx.current_frame]);
    if (!RVK(vkEndCommandBuffer(ctx.device.cmd_buffs[ctx.current_frame]))) return false;
    if (!r_submit(ctx.device, ctx.current_frame)) return false;
    if (!r_present(ctx.device.queue, ctx.device.render_finished_sems[ctx.current_frame],
                   ctx.img_idx, ctx.swapchain.handle)) return false;

    if (ctx.multiple_frames_in_flight)
        ctx.current_frame = (ctx.current_frame + 1) % RVK_MAX_FRAMES_IN_FLIGHT;
    end_timer();
    poll_input_events();

    return true;
}

void enable_multiple_frames_in_flight()
{
    ctx.multiple_frames_in_flight = true;
}

static void wait_time(double seconds)
{
    if (seconds <= 0) return;

    /* prepare for partial busy wait loop */
    double destination_time = get_time() + seconds;
    double sleep_secs = seconds - seconds * 0.05;

    /* for now wait time only supports linux */
#if defined(__linux__)
    struct timespec req = {0};
    time_t sec = sleep_secs;
    long nsec = (sleep_secs - sec) * 1000000000L;
    req.tv_sec = sec;
    req.tv_nsec = nsec;

    while (nanosleep(&req, &req) == -1) continue;
#endif

#if defined(_WIN32)
    Sleep((unsigned long)(sleep_secs * 1000.0));
#endif

    /* partial busy wait loop */
    while (get_time() < destination_time) {}
}

void begin_timer()
{
    ctx.time.curr   = get_time();
    ctx.time.update = ctx.time.curr - ctx.time.prev;
    ctx.time.prev   = ctx.time.curr;
}

void end_timer()
{
    ctx.time.curr = get_time();
    ctx.time.draw = ctx.time.curr - ctx.time.prev;
    ctx.time.prev = ctx.time.curr;
    ctx.time.frame = ctx.time.update + ctx.time.draw;

    if (ctx.time.frame < ctx.time.target) {
        wait_time(ctx.time.target - ctx.time.frame);

        ctx.time.curr = get_time();
        double wait = ctx.time.curr - ctx.time.prev;
        ctx.time.prev = ctx.time.curr;
        ctx.time.frame += wait;
    }

    ctx.time.frame_count++;
}

double get_frame_time()
{
    return ctx.time.frame;	
}

double get_time()
{
    return glfwGetTime();
}

Core_Context get_core_context()
{
    return ctx;
}

void begin_mode_3D(Camera camera)
{
    double aspect = ctx.swapchain.extent.width / (double)ctx.swapchain.extent.height;
    matrices.proj  = MatrixPerspective(camera.fovy * DEG2RAD, aspect, Z_NEAR, Z_FAR);
    matrices.proj.m5 *= -1.0f; // Vulkan
    matrices.view = MatrixLookAt(camera.position, camera.target, camera.up);
    matrices.view_proj = MatrixMultiply(matrices.view, matrices.proj);

    push_matrix();
}

void end_mode_3D()
{
    pop_matrix();

    while (mat_stack_p > 0) {
        pop_matrix();
        r_log(RVK_WARNING, "more matrix pushes than pops");
    }
}

void push_matrix()
{
    if (mat_stack_p < MAX_MAT_STACK) {
        if (mat_stack_p) {
            mat_stack[mat_stack_p] = mat_stack[mat_stack_p - 1];
            mat_stack_p++;
        } else {
            mat_stack[mat_stack_p++] = MatrixIdentity();
        }
    } else {
        r_log(RVK_ERROR, "matrix stack overflow");
    }
}

void pop_matrix()
{
    if (mat_stack_p > 0)
        mat_stack_p--;
    else
        r_log(RVK_ERROR, "matrix stack underflow");
}

Matrix get_model_view_projection()
{
    Matrix model = MatrixIdentity();
    if (mat_stack_p) model = mat_stack[mat_stack_p - 1];
    return MatrixMultiply(model, matrices.view_proj);
}

Matrix get_view_projection()
{
    return matrices.view_proj;
}

Matrix get_projection()
{
    return matrices.proj;
}

Matrix get_model()
{
    Matrix model = MatrixIdentity();
    if (mat_stack_p) model = mat_stack[mat_stack_p - 1];
    return model;
}

VkCommandBuffer get_current_cmd_buff()
{
    return ctx.device.cmd_buffs[ctx.current_frame];
}

void set_viewport_scissor()
{
    r_cmd_set_viewport_scissor(ctx.device.cmd_buffs[ctx.current_frame], ctx.swapchain.extent);
}

void wait_idle()
{
    vkQueueWaitIdle(ctx.device.queue);
}

Vector3 get_camera_forward(Camera *camera)
{
    return Vector3Normalize(Vector3Subtract(camera->target, camera->position));
}

Vector3 get_camera_up(Camera *camera)
{
    return Vector3Normalize(camera->up);
}

Vector3 get_camera_right(Camera *camera)
{
    Vector3 forward = get_camera_forward(camera);
    Vector3 up = get_camera_up(camera);
    return Vector3CrossProduct(forward, up);
}

void camera_move_forward(Camera *camera, float distance)
{
    Vector3 forward = get_camera_forward(camera);
    forward = Vector3Scale(forward, distance);
    camera->position = Vector3Add(camera->position, forward);
    camera->target = Vector3Add(camera->target, forward);
}

void camera_move_right(Camera *camera, float distance)
{
    Vector3 right = get_camera_right(camera);
    right = Vector3Scale(right, distance);
    camera->position = Vector3Add(camera->position, right);
    camera->target = Vector3Add(camera->target, right);
}

void camera_move_up(Camera *camera, float distance)
{
    Vector3 up = get_camera_up(camera);
    up = Vector3Scale(up, distance);
    camera->position = Vector3Add(camera->position, up);
    camera->target = Vector3Add(camera->target, up);
}

Vector2 get_mouse_delta()
{
    Vector2 delta = {
        .x = mouse.curr_pos.x - mouse.prev_pos.x,
        .y = mouse.curr_pos.y - mouse.prev_pos.y
    };
    return delta;
}

void camera_yaw(Camera *camera, float angle)
{
    Vector3 up = get_camera_up(camera);
    Vector3 target_pos = Vector3Subtract(camera->target, camera->position);
    target_pos = Vector3RotateByAxisAngle(target_pos, up, angle);
    camera->target = Vector3Add(camera->position, target_pos);
}

void camera_roll(Camera *camera, float angle)
{
    Vector3 forward = get_camera_forward(camera);
    camera->up = Vector3RotateByAxisAngle(camera->up, forward, angle);
}

void camera_pitch(Camera *camera, float angle)
{
    Vector3 right = get_camera_right(camera);
    Vector3 target_pos = Vector3Subtract(camera->target, camera->position);
    target_pos = Vector3RotateByAxisAngle(target_pos, right, angle);
    camera->target = Vector3Add(camera->position, target_pos);
}

void camera_move_to_target(Camera *camera, float delta)
{
    float distance = Vector3Distance(camera->position, camera->target);
    distance += delta;
    if (distance <= 0) distance = 0.001f;
    Vector3 forward = get_camera_forward(camera);
    camera->position = Vector3Add(camera->target, Vector3Scale(forward, -distance));
}

bool is_key_down(int key)
{
    bool down = false;

    if ((key > 0) && (key < MAX_KEYBOARD_KEYS)) {
        if (keyboard.curr_key_state[key] == 1) down = true;
    }

    return down;
}

bool is_gamepad_button_pressed(int button)
{
    bool pressed = false;

    if (button < MAX_GAMEPAD_BUTTONS) {
        if (gamepad.prev_button_state[button] == 0 && gamepad.curr_button_state[button] == 1)
            pressed = true;
    }

    return pressed;
}

bool is_gamepad_button_down(int button)
{
    bool pressed = false;

    if (button < MAX_GAMEPAD_BUTTONS) {
        if (gamepad.curr_button_state[button] == 1)
            pressed = true;
    }

    return pressed;
}

float get_gamepad_axis_movement(int axis)
{
    float value = 0;

    if (axis < MAX_GAMEPAD_AXIS && fabsf(gamepad.axis_state[axis]) > 0.1f)
        value = gamepad.axis_state[axis];

    return value;
}

int get_mouse_x()
{
    return (int)mouse.curr_pos.x;
}

int get_mouse_y()
{
    return (int)mouse.curr_pos.y;
}

bool is_mouse_button_down(int button)
{
    return mouse.curr_button_state[button] == 1;
}

float get_mouse_wheel_move()
{
    float result = 0.0f;

    if (fabsf(mouse.curr_wheel_move.x) > fabsf(mouse.curr_wheel_move.y)) result = (float)mouse.curr_wheel_move.x;
    else result = (float)mouse.curr_wheel_move.y;

    return result;
}

void update_camera_free(Camera *camera)
{
    Vector2 delta = get_mouse_delta();

    if (is_mouse_button_down(MOUSE_BUTTON_RIGHT)) {
        camera_yaw(camera, -delta.x * CAMERA_MOUSE_MOVE_SENSITIVITY);
        camera_pitch(camera, -delta.y * CAMERA_MOUSE_MOVE_SENSITIVITY);
    }

    float ft = get_frame_time();
    float move_speed = CAMERA_MOVE_SPEED * ft;

    /* gamepad movement */
    float joy_x = get_gamepad_axis_movement(GAMEPAD_AXIS_RIGHT_X);
    float joy_y = get_gamepad_axis_movement(GAMEPAD_AXIS_RIGHT_Y);
    float joy_x_norm = (fabsf(joy_x) - DEAD_ZONE) / (1.0f - DEAD_ZONE);
    float joy_y_norm = (fabsf(joy_y) - DEAD_ZONE) / (1.0f - DEAD_ZONE);
    if (joy_x >  DEAD_ZONE) camera_yaw(camera,  -joy_x_norm * ft * GAMEPAD_ROT_SENSITIVITY);
    if (joy_y >  DEAD_ZONE) camera_pitch(camera,-joy_y_norm * ft * GAMEPAD_ROT_SENSITIVITY);
    if (joy_x < -DEAD_ZONE) camera_yaw(camera,   joy_x_norm * ft * GAMEPAD_ROT_SENSITIVITY);
    if (joy_y < -DEAD_ZONE) camera_pitch(camera, joy_y_norm * ft * GAMEPAD_ROT_SENSITIVITY);
    float fb = get_gamepad_axis_movement(GAMEPAD_AXIS_LEFT_Y);
    float lr = get_gamepad_axis_movement(GAMEPAD_AXIS_LEFT_X);
    float fb_norm = (fabsf(fb) - DEAD_ZONE) / (1.0f - DEAD_ZONE);
    float lr_norm = (fabsf(lr) - DEAD_ZONE) / (1.0f - DEAD_ZONE);
    if (fb <= -DEAD_ZONE) camera_move_forward(camera,  move_speed * fb_norm);
    if (lr <= -DEAD_ZONE) camera_move_right(camera,   -move_speed * lr_norm);
    if (fb >=  DEAD_ZONE) camera_move_forward(camera, -move_speed * fb_norm);
    if (lr >=  DEAD_ZONE) camera_move_right(camera,    move_speed * lr_norm);
    if (is_gamepad_button_down(GAMEPAD_BUTTON_RIGHT_TRIGGER_2)) camera_move_up(camera, move_speed / 2.0f);
    if (is_gamepad_button_down(GAMEPAD_BUTTON_LEFT_TRIGGER_2))  camera_move_up(camera, -move_speed / 2.0f);

    /* keyboard movement */
    if (is_key_down(KEY_LEFT_SHIFT)) move_speed *= 10.0f;
    if (is_key_down(KEY_W)) camera_move_forward(camera,  move_speed);
    if (is_key_down(KEY_A)) camera_move_right(camera,   -move_speed);
    if (is_key_down(KEY_S)) camera_move_forward(camera, -move_speed);
    if (is_key_down(KEY_D)) camera_move_right(camera,    move_speed);
    if (is_key_down(KEY_E)) camera_move_up(camera,  move_speed);
    if (is_key_down(KEY_Q)) camera_move_up(camera, -move_speed);
    if (is_key_down(KEY_LEFT))  camera_roll(camera, -CAMERA_ROT_SENSITIVITY * ft);
    if (is_key_down(KEY_RIGHT)) camera_roll(camera,  CAMERA_ROT_SENSITIVITY * ft);

    camera_move_to_target(camera, -get_mouse_wheel_move());
}

void key_callback(GLFWwindow *window, int key, int scancode, int action, int mods)
{
    (void)scancode;
    if (key < 0) return;

    switch (action) {
    case GLFW_RELEASE: keyboard.curr_key_state[key] = 0; break;
    case GLFW_PRESS: keyboard.curr_key_state[key] = 1; break;
    case GLFW_REPEAT: keyboard.key_repeat_in_frame[key] = 1; break;
    }

    if (((key == KEY_CAPS_LOCK) && ((mods & GLFW_MOD_CAPS_LOCK) > 0)) ||
        ((key == KEY_NUM_LOCK) && ((mods & GLFW_MOD_NUM_LOCK) > 0))) keyboard.curr_key_state[key] = 1;

    if ((keyboard.key_pressed_queue_count < MAX_KEY_PRESSED_QUEUE) && (action == GLFW_PRESS)) {
        keyboard.key_pressed_queue[keyboard.key_pressed_queue_count] = key;
        keyboard.key_pressed_queue_count++;
    }

    if ((key == keyboard.exit_key) && (action == GLFW_PRESS)) glfwSetWindowShouldClose(window, GLFW_TRUE);
}

void poll_input_events()
{
    /* keyboard */
    keyboard.key_pressed_queue_count = 0;
    keyboard.char_pressed_queue_count = 0;
    for (int i = 0; i < MAX_KEYBOARD_KEYS; i++) {
        keyboard.prev_key_state[i] = keyboard.curr_key_state[i];
        keyboard.key_repeat_in_frame[i] = 0;
    }

    /* mouse */
    mouse.prev_pos = mouse.curr_pos;
    for (int i = 0; i < MAX_MOUSE_BUTTONS; i++)
        mouse.prev_button_state[i] = mouse.curr_button_state[i];
    mouse.prev_wheel_move = mouse.curr_wheel_move;
    mouse.curr_wheel_move = (Vector2){ 0.0f, 0.0f };

    /* gamepad */
    GLFWgamepadstate state = {0};
    glfwGetGamepadState(0, &state); // 0 means only one controller is supported

    /* gamepad buttons */
    for (int i = 0; i < MAX_GAMEPAD_BUTTONS; i++)
        gamepad.prev_button_state[i] = gamepad.curr_button_state[i];
    const unsigned char *buttons = state.buttons;
    for (int i = 0; buttons != NULL && i < GLFW_GAMEPAD_BUTTON_DPAD_LEFT + 1 && i < MAX_GAMEPAD_BUTTONS; i++) {
        int btn = -1;

        switch (i) {
        case GLFW_GAMEPAD_BUTTON_Y:            btn = GAMEPAD_BUTTON_RIGHT_FACE_UP;    break;
        case GLFW_GAMEPAD_BUTTON_B:            btn = GAMEPAD_BUTTON_RIGHT_FACE_RIGHT; break;
        case GLFW_GAMEPAD_BUTTON_A:            btn = GAMEPAD_BUTTON_RIGHT_FACE_DOWN;  break;
        case GLFW_GAMEPAD_BUTTON_X:            btn = GAMEPAD_BUTTON_RIGHT_FACE_LEFT;  break;
        case GLFW_GAMEPAD_BUTTON_LEFT_BUMPER:  btn = GAMEPAD_BUTTON_LEFT_TRIGGER_1;   break;
        case GLFW_GAMEPAD_BUTTON_RIGHT_BUMPER: btn = GAMEPAD_BUTTON_RIGHT_TRIGGER_1;  break;
        case GLFW_GAMEPAD_BUTTON_BACK:         btn = GAMEPAD_BUTTON_MIDDLE_LEFT;      break;
        case GLFW_GAMEPAD_BUTTON_GUIDE:        btn = GAMEPAD_BUTTON_MIDDLE;           break;
        case GLFW_GAMEPAD_BUTTON_START:        btn = GAMEPAD_BUTTON_MIDDLE_RIGHT;     break;
        case GLFW_GAMEPAD_BUTTON_DPAD_UP:      btn = GAMEPAD_BUTTON_LEFT_FACE_UP;     break;
        case GLFW_GAMEPAD_BUTTON_DPAD_RIGHT:   btn = GAMEPAD_BUTTON_LEFT_FACE_RIGHT;  break;
        case GLFW_GAMEPAD_BUTTON_DPAD_DOWN:    btn = GAMEPAD_BUTTON_LEFT_FACE_DOWN;   break;
        case GLFW_GAMEPAD_BUTTON_DPAD_LEFT:    btn = GAMEPAD_BUTTON_LEFT_FACE_LEFT;   break;
        case GLFW_GAMEPAD_BUTTON_LEFT_THUMB:   btn = GAMEPAD_BUTTON_LEFT_THUMB;       break;
        case GLFW_GAMEPAD_BUTTON_RIGHT_THUMB:  btn = GAMEPAD_BUTTON_RIGHT_THUMB;      break;
        default: break;
        }

        if (btn != -1) {
            if (buttons[i] == GLFW_PRESS) {
                gamepad.curr_button_state[btn] = 1;
                gamepad.last_button_pressed = btn;
            } else {
                gamepad.curr_button_state[btn] = 0;
            }
        }
    }

    /* gamepad axes */
    const float *axes = state.axes;
    for (int i = 0; axes != NULL && i < GLFW_GAMEPAD_AXIS_LAST + 1 && i < MAX_GAMEPAD_AXIS; i++)
        gamepad.axis_state[i] = axes[i];

    /* if we want to treat trigger buttons as booleans */
    gamepad.curr_button_state[GAMEPAD_BUTTON_LEFT_TRIGGER_2]  = gamepad.axis_state[GAMEPAD_AXIS_LEFT_TRIGGER] > 0.1f;
    gamepad.curr_button_state[GAMEPAD_BUTTON_RIGHT_TRIGGER_2] = gamepad.axis_state[GAMEPAD_AXIS_RIGHT_TRIGGER] > 0.1f;
}

void mouse_scroll_callback(GLFWwindow *window, double x_offset, double y_offset)
{
    (void)window;
    Vector2 offsets = {.x = x_offset, .y = y_offset};
    mouse.curr_wheel_move = offsets;
}

void mouse_button_callback(GLFWwindow *window, int button, int action, int mods)
{
    (void) window;
    (void) mods;
    mouse.curr_button_state[button] = action;
}

void mouse_cursor_pos_callback(GLFWwindow *window, double x, double y)
{
    (void) window;

    mouse.curr_pos.x = x;
    mouse.curr_pos.y = y;
}

bool is_key_pressed(int key)
{
    bool pressed = false;

    if ((key > 0) && (key < MAX_KEYBOARD_KEYS)) {
        if ((keyboard.prev_key_state[key] == 0) && (keyboard.curr_key_state[key] == 1))
            pressed = true;
    }

    return pressed;
}

void scale(float x, float y, float z)
{
    if (mat_stack_p > 0)
        mat_stack[mat_stack_p - 1] = MatrixMultiply(MatrixScale(x, y, z), mat_stack[mat_stack_p - 1]);
    else
        r_log(RVK_ERROR, "no matrix available to scale");
}

void translate(float x, float y, float z)
{
    if (mat_stack_p > 0)
        mat_stack[mat_stack_p - 1] = MatrixMultiply(MatrixTranslate(x, y, z), mat_stack[mat_stack_p - 1]);
    else
        r_log(RVK_ERROR, "no matrix available to translate");
}

void matrix_cat(Matrix m)
{
    if (mat_stack_p > 0)
        mat_stack[mat_stack_p - 1] = MatrixMultiply(m, mat_stack[mat_stack_p - 1]);
    else
        r_log(RVK_ERROR, "no matrix available to concantenate");
}

int get_fps()
{
    double frame_time = get_frame_time();
    if (frame_time == 0) return 0;
    else return (int)roundf(1.0f / frame_time);
}

void log_fps()
{
    static int fps = -1;
    int curr_fps = get_fps();
    if (curr_fps != fps) {
        printf("FPS: %d (%fms)\n", curr_fps, get_frame_time() * 1000.0f);
        fps = curr_fps;
    }
}
