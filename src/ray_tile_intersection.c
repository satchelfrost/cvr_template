#include "cvr.h"

typedef struct {
    Vector3 normal;
    Vector3 point;
} Plane;

typedef struct {
    Vector3 origin;
    Vector3 direction;
    float max_distance;
} Ray;

bool ray_plane_intersection_point(Ray r, Plane p, Vector3 *point)
{
    r.direction = Vector3Normalize(r.direction);
    float t = Vector3DotProduct(p.normal, Vector3Subtract(p.point, r.origin))/Vector3DotProduct(p.normal, r.direction);
    if (t >= 0.0f && t <= r.max_distance) {
        *point = Vector3Add(r.origin, Vector3Scale(r.direction, t));
        return true;
    }
    return false;
}

bool in_2D_bounds(Vector2 point, AABB_2D aabb)
{
    return point.x > aabb.min.x && point.x < aabb.max.x &&
           point.y > aabb.min.x && point.y < aabb.max.y;
}

#define TILES_PER_SIDE 3
#define DIST_THRESH_1 0.6
#define DIST_THRESH_2 0.9

typedef struct {
    Vector3 center;
    float length;
} Tile;

typedef enum {
    SURFACE_FRONT,
    SURFACE_LEFT,
    SURFACE_RIGHT,
    SURFACE_FLOOR,
    SURFACE_COUNT,
    SURFACE_NONE,
} Surface;

Plane surfaces[SURFACE_COUNT] = {
    {
        .normal = {0.0, 0.0, 1.0f},
        .point  = {0.0, 0.0, -0.5f}
    },
    {
        .normal = { 1.0f, 0.0f, 0.0f},
        .point  = {-0.5f, 0.0f, 0.0f}
    },
    {
        .normal = {-1.0f, 0.0f, 0.0f},
        .point  = {0.5f, 0.0f, 0.0f}
    },
    {
        .normal = {0.0f, 1.0f, 0.0f},
        .point  = {0.0f, -0.5f, 0.0f}
    },
};

AABB_2D aabb = {
    .min = {-0.5f, -0.5f},
    .max = { 0.5f,  0.5f},
};

/* point (of intersection) is valid if returned surface is not SURFACE_NONE */
Surface intersected_surface(Ray ray, Vector3 *point)
{
    for (int i = 0; i < SURFACE_COUNT; i++) {
        if (ray_plane_intersection_point(ray, surfaces[i], point)) {
            Vector2 point_2D = {0.0f, 0.0f};

            /* convert point to plane coordinates */
            switch (i) {
            case SURFACE_FRONT: point_2D = (Vector2){point->x, point->y}; break;
            case SURFACE_LEFT:  point_2D = (Vector2){point->z, point->y}; break;
            case SURFACE_RIGHT: point_2D = (Vector2){point->z, point->y}; break;
            case SURFACE_FLOOR: point_2D = (Vector2){point->x, point->z}; break;
            default: break;
            }

            if (in_2D_bounds(point_2D, aabb)) return i;
        }
    }

    return SURFACE_NONE;
}

/* value of negative 1 indicates out of bounds */
int tile_index(Surface surface, Vector3 point)
{
    int index = -1;
    Vector2 point_2D = {0.0f, 0.0f};

    /* convert point to plane coordinates */
    switch (surface) {
    case SURFACE_FRONT: point_2D = (Vector2){point.x, point.y}; break;
    case SURFACE_LEFT:  point_2D = (Vector2){point.z, point.y}; break;
    case SURFACE_RIGHT: point_2D = (Vector2){point.z, point.y}; break;
    case SURFACE_FLOOR: point_2D = (Vector2){point.x, point.z}; break;
    default: break;
    }

    float norm_x = point_2D.x - aabb.min.x;
    float norm_y = point_2D.y - aabb.min.y;
    int tile_x_idx = norm_x*TILES_PER_SIDE;
    int tile_y_idx = norm_y*TILES_PER_SIDE;
    index = tile_x_idx + tile_y_idx*TILES_PER_SIDE;
    if (index >= TILES_PER_SIDE*TILES_PER_SIDE) index = -1;

    return index;
}

int main()
{
    init_window(1600, 900, "ray tile intersection");

    Camera main_camera = {
        .position = {0.352557, 0.584382, 1.544438},
        .target = {0.002612, -0.415623, -0.664073},
        .up       = {0.0, 1.0, 0.0},
        .fovy     = 45.0f,
    };
    Camera head = {
        .position = {0.0f, 0.0f, 0.0f},
        .target   = {0.0f, 0.0f, -1.0f},
        .up       = {0.0f, 1.0f, 0.0f},
        .fovy     = 45.0f,
    };
    Camera *controlling_camera = &head;

    Tile tiles[SURFACE_COUNT][TILES_PER_SIDE*TILES_PER_SIDE];
    float tile_length = 1.0f/TILES_PER_SIDE;
    float start_x = aabb.min.x + tile_length*0.5;
    float start_y = aabb.min.y + tile_length*0.5;
    for (int y = 0; y < TILES_PER_SIDE; y++) {
        for (int x = 0; x < TILES_PER_SIDE; x++) {
            tiles[SURFACE_FRONT][x + y*TILES_PER_SIDE] = (Tile){
                .center = {
                    .x = start_x + tile_length*x,
                    .y = start_y + tile_length*y,
                    .z = -0.5f,
                },
                .length = tile_length
            };
            tiles[SURFACE_LEFT][x + y*TILES_PER_SIDE] = (Tile){
                .center = {
                    .x = -0.5f,
                    .y = start_y + tile_length*y,
                    .z = start_x + tile_length*x,
                },
                .length = tile_length
            };
            tiles[SURFACE_RIGHT][x + y*TILES_PER_SIDE] = (Tile){
                .center = {
                    .x = 0.5f,
                    .y = start_y + tile_length*y,
                    .z = start_x + tile_length*x,
                },
                .length = tile_length
            };
            tiles[SURFACE_FLOOR][x + y*TILES_PER_SIDE] = (Tile){
                .center = {
                    .x = start_x + tile_length*x,
                    .y = -0.5f,
                    .z = start_y + tile_length*y,
                },
                .length = tile_length
            };
        }
    }

    while (!window_should_close()) {
        Ray head_dir = {
            .origin = head.position,
            .direction = Vector3Subtract(head.target, head.position),
            .max_distance = 4.0f,
        };

        if (is_key_pressed(KEY_C))
            controlling_camera = (controlling_camera == &main_camera) ? &head : &main_camera;

        if (is_key_pressed(KEY_P)) {
            printf(".position = {%f, %f, %f}\n", main_camera.position.x, main_camera.position.y, main_camera.position.z);
            printf(".target = {%f, %f, %f}\n",   main_camera.target.x,   main_camera.target.y,   main_camera.target.z);
            printf(".up = {%f, %f, %f}\n",       main_camera.up.x,       main_camera.up.y,       main_camera.up.z);
        }

        update_camera_free(controlling_camera);

        begin_drawing(BLACK);
        begin_mode_3D(main_camera);
            push_matrix();
                Matrix m = MatrixInvert(MatrixLookAt(head.position, head.target, head.up));
                matrix_cat(m);
                draw_line((Vector3){0.0f, 0.0f, 0.0f}, (Vector3){0.0f, 0.0f, -head_dir.max_distance}, WHITE);
            pop_matrix();


            Vector3 point = {0.0f, 0.0f, 0.0f};
            float tile_fudge_factor = 0.98;
            int surface = intersected_surface(head_dir, &point);

            /* draw the point of intersection */
            if (surface != SURFACE_NONE) {
                push_matrix();
                    translate(point.x, point.y, point.z);
                    scale(0.01, 0.01, 0.01);
                    if (!draw_wireframe_box_from_mat_stack(RED)) return 1;
                pop_matrix();
            }

            /* color each tile */
            int intersected_idx = tile_index(surface, point);
            for (int i = 0; i < SURFACE_COUNT; i++) {
                for (int j = 0; j < TILES_PER_SIDE*TILES_PER_SIDE; j++) {
                    push_matrix();
                        translate(tiles[i][j].center.x, tiles[i][j].center.y, tiles[i][j].center.z);
                        float scale_factor = tile_length*tile_fudge_factor;
                        switch (i) {
                        case SURFACE_FRONT: scale(scale_factor, scale_factor,         0.0f); break;
                        case SURFACE_LEFT:  scale(        0.0f, scale_factor, scale_factor); break;
                        case SURFACE_RIGHT: scale(        0.0f, scale_factor, scale_factor); break;
                        case SURFACE_FLOOR: scale(scale_factor,         0.0f, scale_factor); break;
                        default: break;
                        }
                        Color color = BLACK;
                        if (intersected_idx != -1) {
                            if (j == intersected_idx && surface == i) {
                                color = BLUE;
                            } else {
                                float distance = Vector3Distance(point, tiles[i][j].center);
                                if (distance < DIST_THRESH_1)
                                    color = GREEN;
                                if (distance > DIST_THRESH_1 && distance < DIST_THRESH_2)
                                    color = YELLOW;
                                if (distance >= DIST_THRESH_2)
                                    color = RED;
                            }
                        }
                        draw_wireframe_box_from_mat_stack(color);
                    pop_matrix();
                }
            }


        end_mode_3D();
        end_drawing();
    }

    close_window();

    return 0;
}
