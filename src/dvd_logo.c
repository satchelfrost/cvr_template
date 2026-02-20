#include "cvr.h"
#include "raymath.h"

#define SPEED 60

int main()
{
    if (!init_window(400, 400, "basic window")) return 1;

    Vector2 pos = {50, 50};
    Vector2 dir = {1, 2};
    Vector2 rect = {100, 50};

    while (!window_should_close()) {
        float dt = get_frame_time();
        float nx = pos.x + dir.x*SPEED*dt;
        if (nx > 400.0f - rect.x || nx < 0.0f) dir.x *= -1.0f;
        else pos.x = nx;
        float ny = pos.y + dir.y*SPEED*dt;
        if (ny > 400.0f - rect.y || ny < 0.0f) dir.y *= -1.0;
        else pos.y = ny;

        begin_drawing(BLUE);
            draw_rectangle(pos.x, pos.y, rect.x, rect.y, RED);
        end_drawing();
    }

    close_window();
    return 0;
}
