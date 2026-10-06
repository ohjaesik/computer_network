#pragma once
#include <algorithm>

// [assignment6] Win32/MFC와 분리한 스크롤 범위 계산이다. 실제 UI와 작은 화면 회귀 검사가 같은 계산을 쓴다.
namespace DialogScrollLayout {
    struct VIEWPORT { int width, height, maximumX, maximumY; bool horizontal, vertical; };
    inline int Clamp(long long position, int maximum) {
        return static_cast<int>((std::max)(0LL, (std::min)(position, static_cast<long long>(maximum))));
    }
    inline VIEWPORT Calculate(int contentWidth, int contentHeight, int clientWidth, int clientHeight,
        int verticalBarWidth, int horizontalBarHeight) {
        bool horizontal = false, vertical = false;
        // [assignment6] 세로 바가 너비를 줄여 가로 바도 필요해지는 경우까지 두 축을 함께 수렴시킨다.
        for (int pass = 0; pass < 3; ++pass) {
            const int width = (std::max)(1, clientWidth - (vertical ? verticalBarWidth : 0));
            const int height = (std::max)(1, clientHeight - (horizontal ? horizontalBarHeight : 0));
            horizontal = contentWidth > width; vertical = contentHeight > height;
        }
        const int width = (std::max)(1, clientWidth - (vertical ? verticalBarWidth : 0));
        const int height = (std::max)(1, clientHeight - (horizontal ? horizontalBarHeight : 0));
        return {width, height, (std::max)(0, contentWidth - width), (std::max)(0, contentHeight - height), horizontal, vertical};
    }
    enum class ACTION { LineBack, LineForward, PageBack, PageForward, First, Last, Thumb, None };
    inline int Next(int current, int maximum, int line, int page, ACTION action, int thumb = 0) {
        long long requested = current;
        switch (action) {
        case ACTION::LineBack: requested -= line; break;
        case ACTION::LineForward: requested += line; break;
        case ACTION::PageBack: requested -= page; break;
        case ACTION::PageForward: requested += page; break;
        case ACTION::First: requested = 0; break;
        case ACTION::Last: requested = maximum; break;
        case ACTION::Thumb: requested = thumb; break;
        default: break;
        }
        return Clamp(requested, maximum);
    }
    inline int Reveal(int current, int maximum, int page, int begin, int end, int padding) {
        padding = (std::min)(padding, page / 4);
        if (end - begin + 2 * padding > page) {
            // [assignment6] 편집창 자체가 viewport보다 크면 위/왼쪽 시작 부분을 보여준다.
            if (begin < current + padding || begin >= current + page - padding) return Clamp(begin - padding, maximum);
        } else {
            if (begin < current + padding) return Clamp(begin - padding, maximum);
            if (end > current + page - padding) return Clamp(end - page + padding, maximum);
        }
        return current;
    }
}
