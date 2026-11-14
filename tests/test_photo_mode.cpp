// The photo mode's camera, when it may open and when the game runs again, the picture's file name.
#include "check.hpp"
#include "app/photo_mode.hpp"

#include <cmath>
#include <fstream>

using namespace od::app;

static bool near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }

static void enter_keeps_the_view() {
    od::Camera c{};
    c.eye[0] = 10.0f;
    c.eye[1] = 20.0f;
    c.eye[2] = 30.0f;
    c.yaw = 0.5f;
    c.pitch = -0.2f;
    c.fovY = 0.9f;
    PhotoMode p;
    CHECK(p.gameRuns());
    p.enter(c, 400.0f);
    CHECK(p.active && !p.gameRuns());
    CHECK(near(p.cam.eye[0], 10.0f) && near(p.cam.eye[1], 20.0f) && near(p.cam.eye[2], 30.0f));
    CHECK(near(p.cam.yaw, 0.5f) && near(p.cam.pitch, -0.2f) && near(p.cam.fovY, 0.9f));
    CHECK(near(p.speed, 400.0f));
}

static void leave_waits_for_the_release() {
    PhotoMode p;
    p.enter(od::Camera{}, 100.0f);
    p.leave();
    CHECK(!p.active && p.resuming && !p.gameRuns());
    p.resuming = false;
    CHECK(p.gameRuns());
}

static void moves_along_the_view() {
    PhotoMode p;
    od::Camera c{};
    c.yaw = 0.0f; // looking down +Z
    p.enter(c, 100.0f);
    PhotoInput in;
    in.move[2] = 1.0f;
    p.step(in, 0.5f);
    CHECK(near(p.cam.eye[0], 0.0f) && near(p.cam.eye[1], 0.0f) && near(p.cam.eye[2], 50.0f));
    in = {};
    in.move[0] = 1.0f; // right of +Z is +X
    p.step(in, 1.0f);
    CHECK(near(p.cam.eye[0], 100.0f) && near(p.cam.eye[2], 50.0f));
    in = {};
    in.move[1] = 1.0f;
    in.fast = true;
    p.step(in, 1.0f);
    CHECK(near(p.cam.eye[1], 100.0f * PhotoMode::kFastScale));
    // looking straight up, forward climbs
    p.cam.pitch = 1.5f;
    const float y = p.cam.eye[1];
    in = {};
    in.move[2] = 1.0f;
    p.step(in, 1.0f);
    CHECK(p.cam.eye[1] > y + 99.0f * std::sin(1.5f));
}

static void look_zoom_roll_limits() {
    PhotoMode p;
    p.enter(od::Camera{}, 100.0f);
    PhotoInput in;
    in.look[1] = 10.0f;
    p.step(in, 0.0f);
    CHECK(near(p.cam.pitch, PhotoMode::kMaxPitch));
    in = {};
    in.roll = -5.0f;
    p.step(in, 0.0f);
    CHECK(near(p.cam.roll, -PhotoMode::kMaxRoll));
    in = {};
    in.zoom = 50.0f;
    p.step(in, 0.0f);
    CHECK(near(p.cam.fovY, PhotoMode::kMinFov));
    in.zoom = -50.0f;
    p.step(in, 0.0f);
    CHECK(near(p.cam.fovY, PhotoMode::kMaxFov));
    p.cam.fovY = 1.0f;
    in.zoom = 0.5f;
    p.step(in, 0.0f);
    CHECK(near(p.cam.fovY, std::exp(-0.5f)));
}

static void allowed() {
    CHECK(photoModeAllowed(true, false, false, false));
    CHECK(!photoModeAllowed(false, false, false, false)); // not in play
    CHECK(!photoModeAllowed(true, true, false, false));   // the title's menus
    CHECK(!photoModeAllowed(true, false, true, false));   // co-op
    CHECK(!photoModeAllowed(true, false, false, true));   // a scripted run
}

static void file_names(const char* tmpArg) {
    const std::filesystem::path dir = std::filesystem::path(tmpArg) / "test_photo_mode";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    std::tm t{};
    t.tm_year = 2026 - 1900;
    t.tm_mon = 10;
    t.tm_mday = 3;
    t.tm_hour = 7;
    t.tm_min = 5;
    t.tm_sec = 9;
    const std::filesystem::path a = photoPath(dir, t);
    CHECK(a.filename() == "opendash_20261103_070509.png");
    std::ofstream(a).put('x');
    const std::filesystem::path b = photoPath(dir, t);
    CHECK(b.filename() == "opendash_20261103_070509_2.png");
    std::ofstream(b).put('x');
    CHECK(photoPath(dir, t).filename() == "opendash_20261103_070509_3.png");
    std::filesystem::remove_all(dir);
}

int main(int argc, char** argv) {
    enter_keeps_the_view();
    leave_waits_for_the_release();
    moves_along_the_view();
    look_zoom_roll_limits();
    allowed();
    file_names(argc > 1 ? argv[1] : std::filesystem::temp_directory_path().string().c_str());
    return testResult("test_photo_mode");
}
