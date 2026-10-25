#include "render/ui_svg.hpp"

#include <cmath>
#include <cstdio>
#include <string>

#pragma warning(push, 0)
#include <nanosvg/nanosvg.h>
#include <nanosvg/nanosvgrast.h>
#pragma warning(pop)

namespace od::ui {

struct Svg::Impl {
    NSVGimage* image = nullptr;
    ~Impl() {
        if (image) nsvgDelete(image);
    }
};

Svg::Svg() : impl_(std::make_unique<Impl>()) {}
Svg::~Svg() = default;

bool Svg::load(const std::filesystem::path& file) {
    if (impl_->image) {
        nsvgDelete(impl_->image);
        impl_->image = nullptr;
    }
    impl_->image = nsvgParseFromFile(file.string().c_str(), "px", 96.0f);
    return loaded();
}

bool Svg::parse(const std::string& text) {
    if (impl_->image) {
        nsvgDelete(impl_->image);
        impl_->image = nullptr;
    }
    std::string copy = text; // nanosvg parses in place
    impl_->image = nsvgParse(copy.data(), "px", 96.0f);
    return loaded();
}

bool Svg::loaded() const { return impl_->image && impl_->image->width > 0 && impl_->image->height > 0; }
float Svg::width() const { return loaded() ? impl_->image->width : 0.0f; }
float Svg::height() const { return loaded() ? impl_->image->height : 0.0f; }

std::vector<uint32_t> Svg::rasterize(float scale, int& w, int& h) const {
    w = h = 0;
    if (!loaded() || scale <= 0.0f) return {};
    w = int(std::ceil(impl_->image->width * scale));
    h = int(std::ceil(impl_->image->height * scale));
    if (w <= 0 || h <= 0 || w > 8192 || h > 8192) return {};
    std::vector<uint32_t> px(size_t(w) * size_t(h), 0u);
    NSVGrasterizer* r = nsvgCreateRasterizer();
    if (!r) return {};
    nsvgRasterize(r, impl_->image, 0.0f, 0.0f, scale, reinterpret_cast<unsigned char*>(px.data()), w, h, w * 4);
    nsvgDeleteRasterizer(r);
    return px;
}

} // namespace od::ui
