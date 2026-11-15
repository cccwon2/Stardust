#pragma once

#include "Play.h"

// Pluto snipe (pluto_rescout_frame): a second probe scout of the enemy main and natural around 3:20.
// The first worker scout leaves before Pluto's starport / control tower / machine shop exist, so its 1-1-1 vulture drop
// was not seen in the 2026-11-15 KnockOut game. The probe visits the enemy main, then the natural, then is released back
// to mining. It gives up when it is badly damaged or after pluto_rescout_max_frames. What it sees feeds the normal enemy
// unit tracking (drop risk, two factory, etc.).
class PlutoRescout : public Play
{
public:
    explicit PlutoRescout(int maxFrames)
            : Play("PlutoRescout")
            , maxFrames(maxFrames)
            , startFrame(-1)
            , stage(Stage::Main)
            , scout(nullptr)
    {}

    void update() override;

    void disband(const std::function<void(const MyUnit)> &removedUnitCallback,
                 const std::function<void(const MyUnit)> &movableUnitCallback) override;

    void removeUnit(const MyUnit &unit) override;

private:
    enum class Stage { Main, Natural, Done };

    int maxFrames;
    int startFrame;
    Stage stage;
    MyUnit scout;

    void finish(const char *reason);
};
