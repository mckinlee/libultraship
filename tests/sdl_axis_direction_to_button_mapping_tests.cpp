#include <gtest/gtest.h>

#include "ship/controller/controldevice/controller/mapping/sdl/SDLAxisDirectionToButtonMapping.h"

namespace Ship {
namespace {

TEST(SDLAxisDirectionToButtonMappingTest, NormalizesFullDirectionalTravel) {
    EXPECT_EQ(SDLAxisDirectionToButtonMapping::NormalizeAnalogValue(0, POSITIVE), 0);
    EXPECT_EQ(SDLAxisDirectionToButtonMapping::NormalizeAnalogValue(SDL_JOYSTICK_AXIS_MAX, POSITIVE), 255);
    EXPECT_EQ(SDLAxisDirectionToButtonMapping::NormalizeAnalogValue(SDL_JOYSTICK_AXIS_MIN, NEGATIVE), 255);
    EXPECT_EQ(SDLAxisDirectionToButtonMapping::NormalizeAnalogValue(SDL_JOYSTICK_AXIS_MIN, POSITIVE), 0);
    EXPECT_EQ(SDLAxisDirectionToButtonMapping::NormalizeAnalogValue(SDL_JOYSTICK_AXIS_MAX, NEGATIVE), 0);
    EXPECT_EQ(SDLAxisDirectionToButtonMapping::NormalizeAnalogValue(1234, 0), 0);
}

TEST(SDLAxisDirectionToButtonMappingTest, RoundsHalfTravelToNearestByte) {
    EXPECT_EQ(SDLAxisDirectionToButtonMapping::NormalizeAnalogValue(16384, POSITIVE), 128);
    EXPECT_EQ(SDLAxisDirectionToButtonMapping::NormalizeAnalogValue(-16384, NEGATIVE), 128);
}

} // namespace
} // namespace Ship
