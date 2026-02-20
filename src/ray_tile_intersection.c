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
    r.direction = Vector3Scale(r.direction, r.max_distance);
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

typedef struct {
    Vector3 center;
    float length;
} Tile;

int main()
{
    init_window(1600, 900, "ray tile intersection");

    Camera main_camera = {
        .position = {2.0f, 1.0f, 1.0f},
        .target = {0.0f, 0.0f, 0.0f},
        .up = {0.0f, 1.0f, 0.0f},
        .fovy = 45.0f,
    };
    Camera head = {
        .position = {0.0f, 0.0f, 0.0f},
        .target = {0.0f, 0.0f, -1.0f},
        .up = {0.0f, 1.0f, 0.0f},
        .fovy = 45.0f,
    };
    Camera *controlling_camera = &main_camera;

    Plane front_wall = {
        .normal = {0.0, 0.0, 1.0f},
        .point  = {0.0, 0.0, -0.5f}
    };
    Plane left_wall = {
        .normal = { 1.0f, 0.0f, 0.0f},
        .point  = {-0.5f, 0.0f, 0.0f}
    };
    Plane right_wall = {
        .normal = {-1.0f, 0.0f, 0.0f},
        .point  = {0.5f, 0.0f, 0.0f}
    };
    Plane floor = {
        .normal = {0.0f, 1.0f, 0.0f},
        .point  = {0.0f, -0.5f, 0.0f}
    };
    AABB_2D aabb = {
        .min = {-0.5f, -0.5f},
        .max = { 0.5f,  0.5f},
    };

    Tile front_tiles[TILES_PER_SIDE*TILES_PER_SIDE];
    Tile left_tiles[TILES_PER_SIDE*TILES_PER_SIDE];
    Tile right_tiles[TILES_PER_SIDE*TILES_PER_SIDE];
    Tile floor_tiles[TILES_PER_SIDE*TILES_PER_SIDE];
    float tile_length = 1.0f/TILES_PER_SIDE;
    float start_x = aabb.min.x + tile_length*0.5;
    float start_y = aabb.min.y + tile_length*0.5;
    for (int y = 0; y < TILES_PER_SIDE; y++) {
        for (int x = 0; x < TILES_PER_SIDE; x++) {
            front_tiles[x + y*TILES_PER_SIDE] = (Tile){
                .center = {
                    .x = start_x + tile_length*x,
                    .y = start_y + tile_length*y,
                    .z = -0.5f,
                },
                .length = tile_length
            };
            left_tiles[x + y*TILES_PER_SIDE] = (Tile){
                .center = {
                    .x = -0.5f,
                    .y = start_y + tile_length*y,
                    .z = start_x + tile_length*x,
                },
                .length = tile_length
            };
            right_tiles[x + y*TILES_PER_SIDE] = (Tile){
                .center = {
                    .x = 0.5f,
                    .y = start_y + tile_length*y,
                    .z = start_x + tile_length*x,
                },
                .length = tile_length
            };
            floor_tiles[x + y*TILES_PER_SIDE] = (Tile){
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
            .max_distance = 10.0f,
        };

        if (is_key_pressed(KEY_C))
            controlling_camera = (controlling_camera == &main_camera) ? &head : &main_camera;

        update_camera_free(controlling_camera);

        begin_drawing(BLUE);
        begin_mode_3D(main_camera);
            // TODO: uncomment this line for a weird bug
            draw_wireframe_box_from_mat_stack(BLACK);

            push_matrix();
                Matrix m = MatrixInvert(MatrixLookAt(head.position, head.target, head.up));
                matrix_cat(m);
                draw_line((Vector3){0.0f, 0.0f, 0.0f}, (Vector3){0.0f, 0.0f, -10.0f}, GREEN);
            pop_matrix();


            Vector3 point = {0.0f, 0.0f, 0.0f};
            float tile_fudge_factor = 0.98;
            // front wall
            int intersected_idx = -1;
            if (ray_plane_intersection_point(head_dir, front_wall, &point)) {
                if (in_2D_bounds((Vector2){point.x, point.y}, aabb)) {
                    float norm_x = point.x - aabb.min.x;
                    float norm_y = point.y - aabb.min.y;
                    int tile_x_idx = norm_x*TILES_PER_SIDE;
                    int tile_y_idx = norm_y*TILES_PER_SIDE;
                    intersected_idx = tile_x_idx + tile_y_idx*TILES_PER_SIDE;

                    push_matrix();
                        translate(point.x, point.y, point.z);
                        scale(0.01, 0.01, 0.01);
                        if (!draw_wireframe_box_from_mat_stack(RED)) return 1;
                    pop_matrix();
                }
            }
            for (int i = 0; i < TILES_PER_SIDE*TILES_PER_SIDE; i++) {
                push_matrix();
                    translate(front_tiles[i].center.x, front_tiles[i].center.y, front_tiles[i].center.z);
                    scale(tile_length*tile_fudge_factor, tile_length*tile_fudge_factor, 0.0);
                    draw_wireframe_box_from_mat_stack((i == intersected_idx) ? GREEN : RED);
                pop_matrix();
            }
            intersected_idx = -1;
            // left wall
            if (ray_plane_intersection_point(head_dir, left_wall, &point)) {
                if (in_2D_bounds((Vector2){point.z, point.y}, aabb)) {
                    float norm_x = point.z - aabb.min.x; // x in left wall's coordinate space
                    float norm_y = point.y - aabb.min.y;
                    int tile_x_idx = norm_x*TILES_PER_SIDE;
                    int tile_y_idx = norm_y*TILES_PER_SIDE;
                    intersected_idx = tile_x_idx + tile_y_idx*TILES_PER_SIDE;

                    push_matrix();
                        translate(point.x, point.y, point.z);
                        scale(0.01, 0.01, 0.01);
                        draw_wireframe_box_from_mat_stack(RED);
                    pop_matrix();
                }
            }
            for (int i = 0; i < TILES_PER_SIDE*TILES_PER_SIDE; i++) {
                push_matrix();
                    translate(left_tiles[i].center.x, left_tiles[i].center.y, left_tiles[i].center.z);
                    scale(0.0, tile_length*tile_fudge_factor, tile_length*tile_fudge_factor);
                    draw_wireframe_box_from_mat_stack((i == intersected_idx) ? GREEN : RED);
                pop_matrix();
            }
            intersected_idx = -1;
            // right wall
            if (ray_plane_intersection_point(head_dir, right_wall, &point)) {
                if (in_2D_bounds((Vector2){point.z, point.y}, aabb)) {
                    float norm_x = point.z - aabb.min.x; // x in right wall's coordinate space
                    float norm_y = point.y - aabb.min.y;
                    int tile_x_idx = norm_x*TILES_PER_SIDE;
                    int tile_y_idx = norm_y*TILES_PER_SIDE;
                    intersected_idx = tile_x_idx + tile_y_idx*TILES_PER_SIDE;

                    push_matrix();
                        translate(point.x, point.y, point.z);
                        scale(0.01, 0.01, 0.01);
                        draw_wireframe_box_from_mat_stack(RED);
                    pop_matrix();
                }
            }
            for (int i = 0; i < TILES_PER_SIDE*TILES_PER_SIDE; i++) {
                push_matrix();
                    translate(right_tiles[i].center.x, right_tiles[i].center.y, right_tiles[i].center.z);
                    scale(0.0, tile_length*tile_fudge_factor, tile_length*tile_fudge_factor);
                    draw_wireframe_box_from_mat_stack((i == intersected_idx) ? GREEN : RED);
                pop_matrix();
            }
            intersected_idx = -1;
            // floor
            if (ray_plane_intersection_point(head_dir, floor, &point)) {
                if (in_2D_bounds((Vector2){point.x, point.z}, aabb)) {
                    float norm_x = point.x - aabb.min.x; // x in right wall's coordinate space
                    float norm_y = point.z - aabb.min.y;
                    int tile_x_idx = norm_x*TILES_PER_SIDE;
                    int tile_y_idx = norm_y*TILES_PER_SIDE;
                    intersected_idx = tile_x_idx + tile_y_idx*TILES_PER_SIDE;

                    push_matrix();
                        translate(point.x, point.y, point.z);
                        scale(0.01, 0.01, 0.01);
                        draw_wireframe_box_from_mat_stack(RED);
                    pop_matrix();
                }
            }
            for (int i = 0; i < TILES_PER_SIDE*TILES_PER_SIDE; i++) {
                push_matrix();
                    translate(floor_tiles[i].center.x, floor_tiles[i].center.y, floor_tiles[i].center.z);
                    scale(tile_length*tile_fudge_factor, 0, tile_length*tile_fudge_factor);
                    draw_wireframe_box_from_mat_stack((i == intersected_idx) ? GREEN : RED);
                pop_matrix();
            }

        end_mode_3D();
        end_drawing();
    }

    close_window();

    return 0;
}
