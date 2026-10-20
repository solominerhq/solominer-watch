#pragma once
#include <lvgl.h>

// Static star fields. One retained object per screen, no timers or animation.
// Labels render above the stars; charging keeps its ring and instructions clear.
namespace clock_stars {
struct Star { int16_t x, y; uint8_t size; uint32_t color; bool sparkle; };
static constexpr Star stars[] = {
    {57, 43, 2, 0x9BAFC8, false},
    {119, 28, 1, 0x465A71, false},
    {249, 38, 1, 0x53667D, false},
    {334, 26, 1, 0x778EA8, false},
    {35, 98, 1, 0x60768E, false},
    {73, 137, 1, 0x465A71, false},
    {24, 179, 1, 0x778EA8, false},
    {49, 238, 2, 0xBDCBDD, true},
    {31, 307, 1, 0x53667D, false},
    {61, 331, 1, 0x405166, false},
    {381, 93, 1, 0x53667D, false},
    {391, 192, 1, 0x465A71, false},
    {367, 258, 2, 0x899FB9, false},
    {348, 334, 2, 0xAABED6, true},
    {387, 314, 1, 0x405166, false},
    {92, 254, 1, 0x405166, false},
    {282, 243, 1, 0x465A71, false},
    {79, 463, 1, 0x53667D, false},
    {324, 473, 1, 0x60768E, false},
    // Brighter accents around the outer field.
    {387, 54, 1, 0x839DBA, false},
    {95, 161, 2, 0xBCCBDD, false},
    {298, 40, 2, 0x718AA7, false},
    {94, 334, 2, 0xA2B9D1, false},
    {50, 311, 1, 0x6A829D, false},
    {132, 329, 2, 0x839DBA, false},
    {174, 246, 2, 0xBCCBDD, false},
    {34, 150, 2, 0x718AA7, false},
    {308, 312, 1, 0xA2B9D1, false},
    {27, 123, 2, 0xD5E1EF, true},
    {364, 281, 2, 0x839DBA, false},
    {332, 47, 2, 0xBCCBDD, false},
    {287, 481, 1, 0x718AA7, false},
    {113, 464, 2, 0xA2B9D1, false},
    {65, 76, 2, 0x6A829D, false},
    {174, 325, 2, 0x839DBA, false},
    {362, 314, 1, 0xBCCBDD, false},
    {151, 326, 2, 0x718AA7, false},
    {212, 39, 2, 0xA2B9D1, false},
    {132, 48, 2, 0x6A829D, false},
    {287, 18, 1, 0x839DBA, false},
    {39, 221, 2, 0xBCCBDD, false},
    {179, 57, 2, 0x718AA7, false},
    {21, 280, 2, 0xA2B9D1, false},
    {323, 299, 1, 0x6A829D, false},
    {21, 69, 2, 0x839DBA, false},
    {79, 228, 2, 0xBCCBDD, false},
    {357, 54, 2, 0x718AA7, false},
    {140, 26, 1, 0xA2B9D1, false},
    {189, 308, 2, 0x6A829D, false},
    {228, 244, 2, 0xD5E1EF, true},
    {234, 297, 2, 0xBCCBDD, false},
    {75, 32, 1, 0x718AA7, false},
    {129, 246, 2, 0xA2B9D1, false},
    {87, 288, 2, 0x6A829D, false},
    {391, 277, 2, 0x839DBA, false},
    {72, 271, 1, 0xBCCBDD, false},
    {54, 135, 2, 0x718AA7, false},
    {250, 247, 2, 0xA2B9D1, false},
    {61, 217, 2, 0x6A829D, false},
    {48, 275, 1, 0x839DBA, false},
    // Continue beneath the transparent status rows; no empty horizontal strip.
    {368, 390, 1, 0x718AA7, false},
    {169, 393, 2, 0x839DBA, false},
    {135, 453, 2, 0x60768E, false},
    {308, 380, 1, 0xA2B9D1, false},
    {278, 400, 2, 0x6A829D, false},
    {367, 431, 2, 0x718AA7, false},
    {44, 390, 1, 0x839DBA, false},
    {380, 367, 2, 0x60768E, false},
    {327, 424, 2, 0xA2B9D1, false},
    {89, 413, 1, 0x6A829D, false},
    {198, 414, 2, 0x718AA7, false},
    {78, 387, 2, 0x839DBA, false},
    {291, 359, 1, 0x60768E, false},
    {212, 369, 2, 0xA2B9D1, false},
    {130, 376, 2, 0x6A829D, false},
    {60, 414, 1, 0x718AA7, false},
    {241, 389, 2, 0x839DBA, false},
    {190, 453, 2, 0x60768E, false},
    {166, 432, 1, 0xA2B9D1, false},
    {286, 445, 2, 0x6A829D, false},
    {252, 451, 2, 0x718AA7, false},
    {57, 443, 1, 0x839DBA, false},
    {318, 359, 2, 0x60768E, false},
    {330, 381, 2, 0xA2B9D1, false},
    // Faint points continue behind date/time and through the center.
    // No text-shaped exclusions; foreground glyphs naturally cover stars.
    {125, 91, 1, 0x53667D, false},
    {166, 120, 1, 0x465A71, false},
    {209, 81, 2, 0x53667D, false},
    {255, 104, 1, 0x405166, false},
    {298, 76, 1, 0x60768E, false},
    {346, 119, 1, 0x465A71, false},
    {369, 154, 1, 0x53667D, false},
    {324, 177, 2, 0x465A71, false},
    {281, 147, 1, 0x60768E, false},
    {237, 196, 1, 0x465A71, false},
    {194, 161, 1, 0x53667D, false},
    {146, 187, 2, 0x465A71, false},
    {111, 216, 1, 0x405166, false},
    {178, 218, 1, 0x60768E, false},
    {273, 216, 2, 0x465A71, false},
    {354, 218, 1, 0x53667D, false},
    {310, 127, 1, 0x405166, false},
    {218, 132, 1, 0x60768E, false},
    {157, 73, 1, 0x465A71, false},
    {119, 139, 1, 0x53667D, false},
    {359, 79, 2, 0x465A71, false},
    {123, 275, 1, 0x53667D, false},
    {168, 288, 2, 0x465A71, false},
    {214, 272, 1, 0x60768E, false},
    {277, 280, 1, 0x405166, false},
    {145, 306, 1, 0x53667D, false},
    {267, 327, 1, 0x465A71, false},
    {215, 341, 2, 0x53667D, false},
    {119, 354, 1, 0x405166, false},
    {237, 355, 1, 0x53667D, false},
};
static constexpr Star splashStars[] = {
    {57, 43, 2, 0x9BAFC8, false},
    {119, 28, 1, 0x465A71, false},
    {249, 38, 1, 0x53667D, false},
    {334, 26, 1, 0x778EA8, false},
    {97, 82, 2, 0xBDCBDD, true},
    {284, 69, 1, 0x465A71, false},
    {26, 131, 1, 0x60768E, false},
    {377, 111, 1, 0x778EA8, false},
    {43, 195, 1, 0x53667D, false},
    {388, 181, 1, 0x405166, false},
    {31, 272, 2, 0x899FB9, false},
    {378, 281, 1, 0x60768E, false},
    {47, 354, 1, 0x778EA8, false},
    {372, 341, 1, 0x465A71, false},
    {86, 156, 1, 0x405166, false},
    {350, 147, 1, 0x53667D, false},
    {91, 344, 1, 0x405166, false},
    {316, 382, 1, 0x53667D, false},
    {61, 431, 1, 0x60768E, false},
    {153, 449, 1, 0x405166, false},
    {269, 421, 1, 0x465A71, false},
    {339, 436, 2, 0xAABED6, true},
    {117, 473, 1, 0x465A71, false},
    {291, 477, 1, 0x53667D, false},
    // Denser static points, kept clear of the native UI.
    {67, 18, 1, 0x839DBA, false},
    {225, 425, 2, 0xBCCBDD, false},
    {380, 414, 2, 0x718AA7, false},
    {106, 432, 2, 0xA2B9D1, false},
    {198, 462, 1, 0x6A829D, false},
    {198, 69, 2, 0x839DBA, false},
    {298, 83, 2, 0xD5E1EF, true},
    {34, 233, 2, 0x718AA7, false},
    {360, 85, 1, 0xA2B9D1, false},
    {56, 454, 2, 0x6A829D, false},
    {260, 20, 2, 0x839DBA, false},
    {106, 58, 2, 0xBCCBDD, false},
    {353, 49, 1, 0x718AA7, false},
    {295, 41, 2, 0xA2B9D1, false},
    {39, 316, 2, 0x6A829D, false},
    {77, 439, 2, 0x839DBA, false},
    {265, 439, 1, 0xBCCBDD, false},
    {48, 87, 2, 0x718AA7, false},
    {95, 455, 2, 0xA2B9D1, false},
    {37, 375, 2, 0x6A829D, false},
    {304, 462, 1, 0x839DBA, false},
    {371, 183, 2, 0xBCCBDD, false},
    {53, 66, 2, 0x718AA7, false},
    {387, 96, 2, 0xD5E1EF, true},
    {37, 289, 1, 0x6A829D, false},
    {319, 477, 2, 0x839DBA, false},
    {177, 40, 2, 0xBCCBDD, false},
    {290, 426, 2, 0x718AA7, false},
    {123, 416, 1, 0xA2B9D1, false},
    {28, 43, 2, 0x6A829D, false},
    {384, 370, 2, 0x839DBA, false},
    {381, 144, 2, 0xBCCBDD, false},
    {326, 51, 1, 0x718AA7, false},
    {365, 429, 2, 0xA2B9D1, false},
    {369, 461, 2, 0x6A829D, false},
    {346, 419, 2, 0x839DBA, false},
    {42, 422, 1, 0xBCCBDD, false},
    {349, 479, 2, 0x718AA7, false},
    {139, 24, 2, 0xD5E1EF, true},
    {239, 67, 2, 0x6A829D, false},
    {207, 86, 1, 0x839DBA, false},
    {242, 433, 2, 0xBCCBDD, false},
    {20, 183, 2, 0x718AA7, false},
    {390, 50, 2, 0xA2B9D1, false},
    // Continue behind the transparent astronaut; do not reserve a rectangular hole.
    {281, 278, 1, 0x839DBA, false},
    {75, 201, 2, 0xBCCBDD, false},
    {139, 220, 2, 0x718AA7, false},
    {295, 225, 2, 0xA2B9D1, false},
    {120, 261, 1, 0x6A829D, false},
    {144, 295, 2, 0x839DBA, false},
    {334, 351, 2, 0xBCCBDD, false},
    {236, 188, 2, 0x718AA7, true},
    {264, 240, 1, 0xA2B9D1, false},
    {322, 193, 2, 0x6A829D, false},
    {286, 390, 2, 0x839DBA, false},
    {111, 375, 2, 0xBCCBDD, false},
    {188, 170, 1, 0x718AA7, false},
    {232, 319, 2, 0xA2B9D1, false},
    {348, 258, 2, 0x6A829D, false},
    {209, 366, 2, 0x839DBA, false},
    {308, 155, 1, 0xBCCBDD, false},
    {313, 125, 2, 0x718AA7, false},
    {227, 126, 2, 0xA2B9D1, false},
    {275, 159, 2, 0x6A829D, false},
    {187, 392, 1, 0x839DBA, false},
    {348, 303, 2, 0xBCCBDD, false},
    {126, 125, 2, 0x718AA7, false},
    {85, 383, 2, 0xA2B9D1, false},
    {139, 376, 1, 0x6A829D, false},
    {178, 356, 2, 0x839DBA, false},
    {245, 153, 2, 0xBCCBDD, false},
    {70, 247, 2, 0x718AA7, false},
    {258, 374, 1, 0xA2B9D1, false},
    {263, 338, 2, 0x6A829D, true},
    {229, 234, 2, 0x839DBA, false},
    {200, 232, 2, 0xBCCBDD, false},
    {118, 309, 1, 0x718AA7, false},
    {271, 119, 2, 0xA2B9D1, false},
    {148, 152, 2, 0x6A829D, false},
    {173, 195, 2, 0x839DBA, false},
    {120, 153, 1, 0xBCCBDD, false},
    {107, 204, 2, 0x718AA7, false},
};
// All charging stars sit beyond the ring (center 205,221; radius 160),
// with a clear lower strip for the power-button instruction.
static constexpr Star chargingStars[] = {
    {64, 37, 1, 0x60768E, false}, {164, 29, 1, 0x405166, false},
    {302, 32, 1, 0x53667D, false}, {355, 65, 1, 0x405166, false},
    {24, 123, 1, 0x465A71, false}, {386, 133, 1, 0x53667D, false},
    {18, 229, 2, 0x687F99, true}, {392, 272, 1, 0x405166, false},
    {30, 343, 1, 0x465A71, false}, {75, 390, 1, 0x53667D, false},
    {337, 393, 2, 0x687F99, true}, {370, 366, 1, 0x405166, false},
    {117, 408, 1, 0x405166, false}, {290, 410, 1, 0x465A71, false},
    {65, 477, 1, 0x405166, false}, {345, 477, 1, 0x53667D, false},
    // Denser static points, kept clear of the native UI.
    {73, 365, 1, 0x839DBA, false},
    {29, 207, 2, 0xBCCBDD, false},
    {309, 64, 2, 0x718AA7, false},
    {99, 360, 2, 0xA2B9D1, false},
    {16, 390, 1, 0x6A829D, false},
    {92, 403, 2, 0x839DBA, false},
    {372, 284, 2, 0xBCCBDD, false},
    {354, 324, 2, 0x718AA7, false},
    {285, 387, 1, 0xA2B9D1, false},
    {25, 49, 2, 0x6A829D, false},
    {19, 187, 2, 0x839DBA, false},
    {35, 155, 2, 0xBCCBDD, false},
    {392, 182, 1, 0x718AA7, false},
    {138, 52, 2, 0xA2B9D1, false},
    {29, 80, 2, 0x6A829D, false},
    {34, 257, 2, 0xD5E1EF, true},
    {367, 50, 1, 0xBCCBDD, false},
    {231, 475, 2, 0x718AA7, false},
    {106, 27, 2, 0xA2B9D1, false},
    {346, 90, 2, 0x6A829D, false},
    {381, 339, 1, 0x839DBA, false},
    {140, 397, 2, 0xBCCBDD, false},
    {217, 403, 2, 0x718AA7, false},
    {385, 60, 2, 0xA2B9D1, false},
    {47, 21, 1, 0x6A829D, false},
    {343, 372, 2, 0x839DBA, false},
};
struct Field { const Star* points; uint16_t count; uint32_t sparkleColor; };
static constexpr Field clockField{stars, sizeof(stars) / sizeof(Star), 0x5C708A};
static constexpr Field splashField{splashStars, sizeof(splashStars) / sizeof(Star), 0x5C708A};
static constexpr Field chargingField{chargingStars, sizeof(chargingStars) / sizeof(Star), 0x334256};
// Shared drawing path lets the celebration use the exact same static field.
inline void paint(lv_layer_t* layer, const Field* field = &clockField,
                  int originX=0, int originY=0, lv_opa_t opacity=LV_OPA_COVER) {
    lv_draw_rect_dsc_t dsc;
    lv_draw_rect_dsc_init(&dsc);
    dsc.bg_opa = opacity;
    if (opacity == LV_OPA_TRANSP) return;
    for (uint16_t i = 0; i < field->count; ++i) {
        const auto& star = field->points[i];
        const int x = originX + star.x, y = originY + star.y;
        if (star.sparkle) {
            dsc.bg_color = lv_color_hex(field->sparkleColor);
            dsc.radius = 0;
            lv_area_t horizontal = {x - 3, y, x + 4, y};
            lv_area_t vertical = {x, y - 3, x, y + 4};
            lv_draw_rect(layer, &dsc, &horizontal);
            lv_draw_rect(layer, &dsc, &vertical);
        }
        dsc.bg_color = lv_color_hex(star.color);
        dsc.radius = star.size / 2;
        lv_area_t point = {x, y, x + star.size - 1, y + star.size - 1};
        lv_draw_rect(layer, &dsc, &point);
    }
}
inline void draw(lv_event_t* event) {
    auto* object = static_cast<lv_obj_t*>(lv_event_get_target(event));
    lv_area_t origin; lv_obj_get_coords(object, &origin);
    auto* field = static_cast<const Field*>(lv_event_get_user_data(event));
    paint(lv_event_get_layer(event), field ? field : &clockField, origin.x1, origin.y1,
          lv_obj_get_style_opa_recursive(object, LV_PART_MAIN));
}
inline void create(lv_obj_t* parent, const Field* field = &clockField) {
    auto* background = lv_obj_create(parent);
    if (!background) return;
    lv_obj_remove_style_all(background);
    lv_obj_set_pos(background, 0, 0);
    lv_obj_set_size(background, 410, 502);
    lv_obj_clear_flag(background, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(background, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(background, draw, LV_EVENT_DRAW_MAIN, const_cast<Field*>(field));
}
}
