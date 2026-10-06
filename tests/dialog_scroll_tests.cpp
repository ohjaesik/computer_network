#include "DialogScrollLayout.h"
#include <cassert>
#include <iostream>

using namespace DialogScrollLayout;
int main() {
    auto view = Calculate(1000, 900, 1000, 900, 17, 17);
    assert(!view.horizontal && !view.vertical && !view.maximumX && !view.maximumY);
    // [assignment6] 세로 스크롤만 시작해도 가로 폭이 좁아져 두 번째 바가 필요할 수 있다.
    view = Calculate(1000, 900, 1000, 600, 17, 17);
    assert(view.horizontal && view.vertical && view.width == 983 && view.height == 583);
    assert(view.maximumX == 17 && view.maximumY == 317);
    view = Calculate(1000, 900, 1100, 600, 17, 17);
    assert(!view.horizontal && view.vertical && view.maximumX == 0);
    view = Calculate(1000, 900, 700, 1000, 17, 17);
    assert(view.horizontal && !view.vertical && view.maximumY == 0);

    // [assignment6] 100~250% 크기의 원본 UI, 작은 창/큰 창 모두 아래/오른쪽 설정 끝까지 접근 가능해야 한다.
    for (int scale : {100, 125, 150, 175, 200, 250}) {
        const int contentWidth = 1240 * scale / 100, contentHeight = 1050 * scale / 100;
        for (int width : {520, 800, 1000, 1280, 1920, 4000}) for (int height : {360, 600, 720, 960, 1080, 3000}) {
            view = Calculate(contentWidth, contentHeight, width, height, 17 * scale / 100, 17 * scale / 100);
            assert(view.horizontal == (contentWidth > view.width) && view.vertical == (contentHeight > view.height));
            assert(view.maximumX + view.width >= contentWidth && view.maximumY + view.height >= contentHeight);
            assert(Next(0, view.maximumX, 16, view.width, ACTION::Last) == view.maximumX);
            assert(Next(0, view.maximumY, 24, view.height, ACTION::Last) == view.maximumY);
            // Resizing back to a large client removes bars and clamps a formerly scrolled position to zero.
            const auto expanded = Calculate(contentWidth, contentHeight, 5000, 5000, 40, 40);
            assert(!expanded.horizontal && !expanded.vertical && Clamp(view.maximumY, expanded.maximumY) == 0);
        }
    }
    assert(Next(10, 500, 24, 250, ACTION::LineBack) == 0);
    assert(Next(490, 500, 24, 250, ACTION::LineForward) == 500);
    assert(Next(400, 500, 24, 250, ACTION::PageBack) == 150);
    assert(Next(400, 500, 24, 250, ACTION::PageForward) == 500);
    assert(Next(400, 500, 24, 250, ACTION::First) == 0);
    assert(Next(0, 100000, 24, 250, ACTION::Thumb, 75000) == 75000);
    assert(Clamp(-1000000000LL, 500) == 0 && Clamp(1000000000LL, 500) == 500);

    // [assignment6] 아래 설정으로 Tab 이동 및 위 화면으로 Shift+Tab 이동 모두 대상이 보이게 한다.
    int position = Reveal(0, 600, 400, 800, 830, 8);
    assert(position == 438 && 800 >= position && 830 <= position + 400);
    position = Reveal(position, 600, 400, 20, 40, 8);
    assert(position == 12);
    assert(Reveal(0, 600, 400, 20, 40, 8) == 0);
    assert(Reveal(500, 600, 400, 0, 600, 8) == 0);
    std::cout << "PASS: 216 viewport/DPI cases, coupled scrollbars, reachable bottom/right edges, resize reset, 32-bit thumb and focus reveal\n";
}
